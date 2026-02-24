#include "virtualmouse.h"
#include <QDebug>
#include <cstring>
#include <cmath>
#include <vector>

// ─── Descriptor HID: ratón absoluto ──────────────────────────────────────────
static const BYTE kMouseDescriptor[] = {
    0x05,0x01, 0x09,0x02, 0xA1,0x01,
      0x09,0x01, 0xA1,0x00,
        0x09,0x30, 0x09,0x31,
        0x15,0x00, 0x26,0xFF,0x7F,
        0x35,0x00, 0x46,0xFF,0x7F,
        0x75,0x10, 0x95,0x02,
        0x81,0x62,
        0x05,0x09, 0x19,0x01, 0x29,0x08,
        0x15,0x00, 0x25,0x01,
        0x75,0x01, 0x95,0x08,
        0x81,0x02,
      0xC0,
    0xC0
};
static constexpr size_t kReportSize = 5;

#define VHF_DEVICE_TYPE             0x8000u
#define IOCTL_VHF_CREATE_DEVICE     CTL_CODE(VHF_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VHF_DELETE_DEVICE     CTL_CODE(VHF_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VHF_SEND_INPUT_REPORT CTL_CODE(VHF_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push,1)
struct VhfCreateParams { USHORT VendorID, ProductID, VersionNumber, ReportDescriptorLength; };
#pragma pack(pop)

constexpr quint32 VirtualMouse::kButtonMap[8];

// ─────────────────────────────────────────────────────────────────────────────
VirtualMouse::VirtualMouse(QObject *parent) : QObject(parent) {}

VirtualMouse::~VirtualMouse()
{
    if (m_device != INVALID_HANDLE_VALUE) {
        DWORD br = 0;
        DeviceIoControl(m_device, IOCTL_VHF_DELETE_DEVICE,
                        nullptr, 0, nullptr, 0, &br, nullptr);
        CloseHandle(m_device);
    }
}

bool VirtualMouse::initialize()
{
    m_device = CreateFileW(L"\\\\.\\VirtualHidDevice",
                           GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_device == INVALID_HANDLE_VALUE) {
        qWarning() << "[VirtualMouse] VirtualHidDevice no disponible. Error:"
                   << Qt::hex << GetLastError();
        return false;
    }

    const size_t descLen   = sizeof(kMouseDescriptor);
    const size_t totalSize = sizeof(VhfCreateParams) + descLen;
    std::vector<BYTE> buf(totalSize);
    auto *p = reinterpret_cast<VhfCreateParams*>(buf.data());
    p->VendorID               = 0x0B9A;
    p->ProductID              = 0x0801;
    p->VersionNumber          = 0x0100;
    p->ReportDescriptorLength = static_cast<USHORT>(descLen);
    memcpy(buf.data() + sizeof(VhfCreateParams), kMouseDescriptor, descLen);

    DWORD br = 0;
    if (!DeviceIoControl(m_device, IOCTL_VHF_CREATE_DEVICE,
                         buf.data(), static_cast<DWORD>(totalSize),
                         nullptr, 0, &br, nullptr)) {
        qWarning() << "[VirtualMouse] CREATE_DEVICE falló. Error:"
                   << Qt::hex << GetLastError();
        CloseHandle(m_device); m_device = INVALID_HANDLE_VALUE;
        return false;
    }

    m_available = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Homografía (DLT normalizado)
// ─────────────────────────────────────────────────────────────────────────────
// Resuelve Ah = 0 con SVD para 4+ correspondencias.
// ─── Álgebra para homografía DLT ─────────────────────────────────────────────
// Sin dependencias externas. Usa iteración inversa con descomposición LU
// para encontrar el eigenvector de menor eigenvalue de A^T*A.

// Factorización LU con pivoteo parcial (9×9 in-place)
static void luDecomp(double M[81], int p[9])
{
    for (int i=0;i<9;i++) p[i]=i;
    for (int k=0;k<9;k++) {
        int maxIdx=k; double maxVal=std::abs(M[k*9+k]);
        for (int i=k+1;i<9;i++)
            if (std::abs(M[i*9+k])>maxVal) { maxVal=std::abs(M[i*9+k]); maxIdx=i; }
        if (maxIdx!=k) {
            std::swap(p[k],p[maxIdx]);
            for (int j=0;j<9;j++) std::swap(M[k*9+j],M[maxIdx*9+j]);
        }
        if (std::abs(M[k*9+k])<1e-14) M[k*9+k]=1e-14;
        for (int i=k+1;i<9;i++) {
            M[i*9+k]/=M[k*9+k];
            for (int j=k+1;j<9;j++) M[i*9+j]-=M[i*9+k]*M[k*9+j];
        }
    }
}

// Resuelve (L·U) x = b usando la factorización LU con permutación
static void luSolve(const double LU[81], const int p[9],
                    const double b[9], double x[9])
{
    double pb[9];
    for (int i=0;i<9;i++) pb[i]=b[p[i]];
    double y[9];
    for (int i=0;i<9;i++) {
        double s=pb[i];
        for (int j=0;j<i;j++) s-=LU[i*9+j]*y[j];
        y[i]=s;
    }
    for (int i=8;i>=0;i--) {
        double s=y[i];
        for (int j=i+1;j<9;j++) s-=LU[i*9+j]*x[j];
        x[i]=s/LU[i*9+i];
    }
}

bool VirtualMouse::computeHomography(const QList<RawPoint> &pts, double H[9])
{
    int n = pts.size();
    if (n < 4) return false;

    // ── Normalización de Hartley ──────────────────────────────────────────
    double mx=0,my=0,tx=0,ty=0;
    for (int i=0;i<n;i++) { mx+=pts[i].camX; my+=pts[i].camY;
                             tx+=pts[i].scrX; ty+=pts[i].scrY; }
    mx/=n; my/=n; tx/=n; ty/=n;

    double msx=0,msy=0,tsx=0,tsy=0;
    for (int i=0;i<n;i++) {
        msx+=std::abs(pts[i].camX-mx); msy+=std::abs(pts[i].camY-my);
        tsx+=std::abs(pts[i].scrX-tx); tsy+=std::abs(pts[i].scrY-ty);
    }
    msx/=n; msy/=n; tsx/=n; tsy/=n;
    if (msx<1e-6) msx=1.0;
    if (msy<1e-6) msy=1.0;
    if (tsx<1e-6) tsx=1.0;
    if (tsy<1e-6) tsy=1.0;

    // ── Matriz A (2n×9) ───────────────────────────────────────────────────
    std::vector<double> A(2*n*9, 0.0);
    for (int i=0;i<n;i++) {
        double x=(pts[i].camX-mx)/msx, y=(pts[i].camY-my)/msy;
        double u=(pts[i].scrX-tx)/tsx, v=(pts[i].scrY-ty)/tsy;
        double *r0=&A[(2*i)*9], *r1=&A[(2*i+1)*9];
        r0[0]=-x; r0[1]=-y; r0[2]=-1; r0[3]=0;  r0[4]=0;  r0[5]=0;
        r0[6]=u*x; r0[7]=u*y; r0[8]=u;
        r1[0]=0;  r1[1]=0;  r1[2]=0;  r1[3]=-x; r1[4]=-y; r1[5]=-1;
        r1[6]=v*x; r1[7]=v*y; r1[8]=v;
    }

    // ── A^T * A ───────────────────────────────────────────────────────────
    double AtA[81]={};
    for (int i=0;i<2*n;i++)
        for (int j=0;j<9;j++)
            for (int k=0;k<9;k++)
                AtA[j*9+k]+=A[i*9+j]*A[i*9+k];

    // ── Iteración inversa para eigenvector mínimo de A^T*A ────────────────
    // (AtA)^{-1} v → converge al eigenvector del menor eigenvalue
    double LU[81]; memcpy(LU,AtA,sizeof(AtA));
    for (int i=0;i<9;i++) LU[i*9+i]+=1e-10; // regularización mínima
    int perm[9]; luDecomp(LU,perm);

    double v[9]={1,0,0,0,1,0,0,0,1};
    for (int iter=0;iter<300;iter++) {
        double w[9]; luSolve(LU,perm,v,w);
        double norm=0; for(int i=0;i<9;i++) norm+=w[i]*w[i];
        norm=std::sqrt(norm); if(norm<1e-14) break;
        for(int i=0;i<9;i++) v[i]=w[i]/norm;
    }

    // ── Desnormalizar: H_real = T_dst_inv * H_norm * T_src ────────────────
    double Ts[9]={1.0/msx,0,-mx/msx, 0,1.0/msy,-my/msy, 0,0,1};
    double Ti[9]={tsx,0,tx,           0,tsy,ty,            0,0,1};
    double Hn[9]; memcpy(Hn,v,sizeof(v));

    double tmp[9]={};
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) for(int k=0;k<3;k++)
        tmp[i*3+j]+=Hn[i*3+k]*Ts[k*3+j];
    memset(H,0,9*sizeof(double));
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) for(int k=0;k<3;k++)
        H[i*3+j]+=Ti[i*3+k]*tmp[k*3+j];

    if(std::abs(H[8])>1e-14)
        for(int i=0;i<9;i++) H[i]/=H[8];

    return true;
}


void VirtualMouse::applyH(const double H[9], double x, double y,
                           double &ox, double &oy)
{
    double w = H[6]*x + H[7]*y + H[8];
    if (std::abs(w) < 1e-14) { ox = oy = 0; return; }
    ox = (H[0]*x + H[1]*y + H[2]) / w;
    oy = (H[3]*x + H[4]*y + H[5]) / w;
}

// ─────────────────────────────────────────────────────────────────────────────
// API de calibración
// ─────────────────────────────────────────────────────────────────────────────
void VirtualMouse::addCalibrationPoint(QPoint cam, QPoint screen, int z)
{
    m_pendingPoints.append({ (double)cam.x(), (double)cam.y(),
                              (double)screen.x(), (double)screen.y(),
                              (double)z });
}

void VirtualMouse::clearCalibrationPoints()
{
    m_pendingPoints.clear();
}

bool VirtualMouse::commitCalibration()
{
    if (m_pendingPoints.size() < 4) {
        qWarning() << "[VirtualMouse] Calibracion: se necesitan al menos 4 puntos.";
        return false;
    }

    double H[9];
    if (!computeHomography(m_pendingPoints, H)) {
        qWarning() << "[VirtualMouse] Calibracion: fallo al calcular homografia.";
        return false;
    }

    // Centro óptico estimado = centroide de puntos de cámara
    double cx = 0, cy = 0, zSum = 0;
    for (auto &p : m_pendingPoints) {
        cx   += p.camX;
        cy   += p.camY;
        zSum += p.z;
    }
    int n = m_pendingPoints.size();
    m_cx   = cx / n;
    m_cy   = cy / n;
    m_zRef = zSum / n;

    memcpy(m_H, H, sizeof(m_H));
    m_calibrated = true;
    m_pendingPoints.clear();

    qDebug() << "[VirtualMouse] Calibracion completada."
             << "cx=" << m_cx << "cy=" << m_cy << "zRef=" << m_zRef;
    return true;
}

VirtualMouse::CalibData VirtualMouse::calibrationData() const
{
    CalibData d;
    d.valid = m_calibrated;
    if (m_calibrated) {
        memcpy(d.H, m_H, sizeof(m_H));
        d.cx = m_cx; d.cy = m_cy; d.zRef = m_zRef;
    }
    return d;
}

void VirtualMouse::setCalibrationData(const CalibData &d)
{
    if (!d.valid) return;
    memcpy(m_H, d.H, sizeof(m_H));
    m_cx = d.cx; m_cy = d.cy; m_zRef = d.zRef;
    m_calibrated = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Aplicar calibración por frame
// ─────────────────────────────────────────────────────────────────────────────
QPoint VirtualMouse::applyCalibration(QPoint cam, int z, GunCon3::GunStatus status) const
{
    // OUT_OF_RANGE: ningún LED visible → no actualizar posición
    if (status == GunCon3::GunStatus::OUT_OF_RANGE)
        return QPoint(-1, -1);

    if (!m_calibrated) {
        // Sin calibración: mapeo lineal de emergencia
        int x = qBound(0, cam.x() + 32768, 65535) * 32767 / 65535;
        int y = qBound(0, 32767 - (cam.y() + 32768) * 32767 / 65535, 32767);
        return QPoint(x, y);
    }

    double cx = cam.x();
    double cy = cam.y();

    // ── Corrección de distancia (z) ───────────────────────────────────────
    // Solo aplicar si ambos LEDs son visibles (IN_RANGE) y z es válido.
    // ONE_REF: z no es calculable (solo hay un LED), se omite la corrección
    // pero sí se aplica la homografía para dar una posición aproximada.
    if (status == GunCon3::GunStatus::IN_RANGE && z > 0 && m_zRef > 0) {
        double scale = m_zRef / static_cast<double>(z);
        cx = m_cx + (cx - m_cx) * scale;
        cy = m_cy + (cy - m_cy) * scale;
    }

    // Y ya viene negado desde GunCon3::readLoop (cámara vs pantalla)
    double ox, oy;
    applyH(m_H, cx, cy, ox, oy);

    return QPoint(qBound(0, (int)std::round(ox), 32767),
                  qBound(0, (int)std::round(oy), 32767));
}

// ─────────────────────────────────────────────────────────────────────────────
// Envío de reporte HID
// ─────────────────────────────────────────────────────────────────────────────
void VirtualMouse::sendReport(QPoint screenPos, quint32 buttons)
{
    if (!m_available) return;

    BYTE report[kReportSize] = {};
    auto x = static_cast<INT16>(qBound(0, screenPos.x(), 32767));
    auto y = static_cast<INT16>(qBound(0, screenPos.y(), 32767));
    memcpy(report + 0, &x, 2);
    memcpy(report + 2, &y, 2);

    BYTE btns = 0;
    for (int i = 0; i < 8; ++i)
        if (buttons & kButtonMap[i]) btns |= (1u << i);
    report[4] = btns;

    DWORD br = 0;
    DeviceIoControl(m_device, IOCTL_VHF_SEND_INPUT_REPORT,
                    report, static_cast<DWORD>(kReportSize),
                    nullptr, 0, &br, nullptr);
}
