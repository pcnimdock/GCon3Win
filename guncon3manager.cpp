#include "guncon3manager.h"
#include "driverinstaller.h"
#include <QDebug>
#include <QTimer>
#include <QSettings>
#include <vector>
#include <QRegularExpression>

#include <windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <usbiodef.h>
#include <winusb.h>
#include <cfgmgr32.h>
#include <dbt.h>

#define GUNCON3_VID 0x0b9a
#define GUNCON3_PID 0x0800

// ─────────────────────────────────────────────────────────────────────────────
// Ventana oculta Win32 para WM_DEVICECHANGE
// ─────────────────────────────────────────────────────────────────────────────
class GunCon3Manager::WatcherWindow
{
public:
    WatcherWindow(GunCon3Manager *mgr) : m_mgr(mgr)
    {
        WNDCLASSW wc   = {};
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.lpszClassName = L"GunCon3Watcher";
        RegisterClassW(&wc);

        m_hwnd = CreateWindowExW(0, L"GunCon3Watcher", L"", 0,
                                 0,0,0,0, HWND_MESSAGE, nullptr,
                                 GetModuleHandle(nullptr), nullptr);
        if (!m_hwnd) return;

        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(mgr));

        DEV_BROADCAST_DEVICEINTERFACE_W filter = {};
        filter.dbcc_size       = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        filter.dbcc_classguid  = GUID_DEVINTERFACE_USB_DEVICE;

        m_notify = RegisterDeviceNotificationW(
            m_hwnd, &filter,
            DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
    }

    ~WatcherWindow()
    {
        if (m_notify) UnregisterDeviceNotification(m_notify);
        if (m_hwnd)   DestroyWindow(m_hwnd);
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_DEVICECHANGE &&
            (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE))
        {
            auto *mgr = reinterpret_cast<GunCon3Manager*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (mgr)
                QTimer::singleShot(400, mgr, &GunCon3Manager::scan);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    GunCon3Manager *m_mgr    { nullptr };
    HWND            m_hwnd   { nullptr };
    HDEVNOTIFY      m_notify { nullptr };
};

// ─────────────────────────────────────────────────────────────────────────────
// Obtener la ruta física del puerto USB (port path)
//
// El devicePath WinUSB tiene la forma:
//   \\?\USB#VID_0B9A&PID_0800#5&1a2b3c4d&0&4#{...guid...}
//
// La parte "5&1a2b3c4d&0&4" es el Instance ID del nodo en el árbol PnP.
// Con CfgMgr32 subimos al nodo padre (el hub USB) y obtenemos su Device ID,
// que identifica el puerto físico de forma estable entre reinicios.
//
// Formato resultado: "USB\ROOT_HUB30#4&1a2b3c4d&0#PORT_4"  (ejemplo)
// ─────────────────────────────────────────────────────────────────────────────
QString GunCon3Manager::getPortPath(const QString &devicePath)
{
    // Extraer el Instance ID del devicePath:
    // \\?\USB#VID_0B9A&PID_0800#<INSTANCE_ID>#{GUID}
    // Tomamos la parte entre el 3er '#' y el 4º '#'
    QString path = devicePath;
    // Quitar prefijo \\?\ y convertir # a \ para que coincida con PnP
    path.remove(0, 4);           // quita "\\?\"
    path.replace('#', '\\');     // \\?\USB#VID...#INST#{GUID} → USB\VID...\INST\{GUID}

    // Eliminar el sufijo GUID (último componente entre llaves)
    int lastBackslash = path.lastIndexOf('\\');
    if (lastBackslash >= 0)
        path = path.left(lastBackslash);   // → "USB\VID_0B9A&PID_0800\5&1a2b3c4d&0&4"

    // Buscar el nodo en el árbol PnP por su Device Instance ID
    std::wstring wInstanceId = path.toStdWString();
    DEVINST devInst = 0;
    if (CM_Locate_DevNodeW(&devInst, const_cast<DEVINSTID_W>(wInstanceId.c_str()),
                           CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
    {
        qWarning() << "[Manager] CM_Locate_DevNodeW falló para:" << path;
        return path;   // fallback: usar el propio path del dispositivo
    }

    // Subir al nodo padre (el hub USB que contiene este puerto)
    DEVINST parentInst = 0;
    if (CM_Get_Parent(&parentInst, devInst, 0) != CR_SUCCESS)
        return path;

    // Obtener el Device ID del padre — ej: "USB\ROOT_HUB30\4&1a2b3c4d&0"
    WCHAR parentId[MAX_PATH] = {};
    if (CM_Get_Device_IDW(parentInst, parentId, MAX_PATH, 0) != CR_SUCCESS)
        return path;

    // Obtener el número de puerto del dispositivo hijo dentro del hub.
    // Está codificado en el último componente del Instance ID del hijo:
    // "USB\VID_0B9A&PID_0800\5&1a2b3c4d&0&PORT" → el último "&PORT"
    // En la práctica el último segmento después del último '&' es el puerto.
    QString instanceId = path;
    int lastAmp = instanceId.lastIndexOf('&');
    QString portNum = (lastAmp >= 0)
                      ? instanceId.mid(lastAmp + 1)
                      : "0";

    // Componer la ruta final: "<ParentDeviceID>#PORT_<N>"
    // Esta cadena identifica unívocamente el PUERTO FÍSICO.
    QString portPath = QString::fromWCharArray(parentId) + "#PORT_" + portNum;
    portPath = portPath.toUpper();

    qDebug() << "[Manager] Port path de" << devicePath.left(60) << "→" << portPath;
    return portPath;
}

// ─────────────────────────────────────────────────────────────────────────────
// Enumeración: devuelve {devicePath, portPath} de todas las GunCon3
// ─────────────────────────────────────────────────────────────────────────────
QList<QPair<QString,QString>> GunCon3Manager::enumGunCons()
{
    QList<QPair<QString,QString>> result;

    GUID guid = GUID_DEVINTERFACE_USB_DEVICE;
    HDEVINFO hdi = SetupDiGetClassDevsW(&guid, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hdi == INVALID_HANDLE_VALUE) return result;

    SP_DEVICE_INTERFACE_DATA ifd;
    ifd.cbSize = sizeof(ifd);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hdi, nullptr, &guid, i, &ifd); ++i)
    {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(hdi, &ifd, nullptr, 0, &needed, nullptr);

        std::vector<BYTE> buf(needed);
        auto *detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(hdi, &ifd, detail, needed, nullptr, nullptr))
            continue;

        HANDLE h = CreateFileW(detail->DevicePath,
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                               nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        WINUSB_INTERFACE_HANDLE wu = nullptr;
        bool isGunCon = false;
        if (WinUsb_Initialize(h, &wu)) {
            USB_DEVICE_DESCRIPTOR desc = {};
            ULONG len = 0;
            if (WinUsb_GetDescriptor(wu, USB_DEVICE_DESCRIPTOR_TYPE,
                                     0, 0, reinterpret_cast<PUCHAR>(&desc),
                                     sizeof(desc), &len)
                && desc.idVendor == GUNCON3_VID
                && desc.idProduct == GUNCON3_PID)
            {
                isGunCon = true;
            }
            WinUsb_Free(wu);
        }
        CloseHandle(h);

        if (isGunCon) {
            QString devPath  = QString::fromWCharArray(detail->DevicePath);
            QString portPath = getPortPath(devPath);
            result.append({devPath, portPath});
        }
    }

    SetupDiDestroyDeviceInfoList(hdi);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Persistencia de asignaciones puerto → índice de jugador
// ─────────────────────────────────────────────────────────────────────────────
int GunCon3Manager::loadAssignment(const QString &portPath) const
{
    QSettings s(kSettingsFile, QSettings::IniFormat);
    QString key = portPath;
    key.replace(QRegularExpression("[^a-zA-Z0-9]"), "_");
    QVariant v = s.value("ports/" + key);
    return v.isValid() ? v.toInt() : -1;
}

void GunCon3Manager::saveAssignment(const QString &portPath, int index)
{
    QSettings s(kSettingsFile, QSettings::IniFormat);
    QString key = portPath;
    key.replace(QRegularExpression("[^a-zA-Z0-9]"), "_");
    s.setValue("ports/" + key, index);
    qDebug() << "[Manager] Asignacion guardada: puerto" << portPath << "→ P" << (index+1);
}

int GunCon3Manager::nextFreeIndex() const
{
    // Buscar el menor índice entero no ocupado actualmente
    QSet<int> used;
    for (const auto &info : m_infos)
        if (info.playerIdx >= 0) used.insert(info.playerIdx);
    for (int i = 0; ; ++i)
        if (!used.contains(i)) return i;
}

// ─────────────────────────────────────────────────────────────────────────────
// GunCon3Manager público
// ─────────────────────────────────────────────────────────────────────────────
GunCon3Manager::GunCon3Manager(QObject *parent) : QObject(parent)
{
    m_watcher = new WatcherWindow(this);
}

GunCon3Manager::~GunCon3Manager()
{
    for (auto &info : m_infos) {
        if (info.gun) { info.gun->close(); delete info.gun; }
    }
    m_infos.clear();
    m_guns.clear();
    delete m_watcher;
}

void GunCon3Manager::scan()
{
    auto present = enumGunCons();

    // ── Añadir nuevas ─────────────────────────────────────────────────────
    QSet<QString> presentPaths;
    for (const auto &[devPath, portPath] : present) {
        presentPaths.insert(devPath);
        if (!m_infos.contains(devPath))
            addGun(devPath, portPath);
    }

    // ── Eliminar desaparecidas ────────────────────────────────────────────
    for (auto it = m_infos.begin(); it != m_infos.end(); ) {
        if (!presentPaths.contains(it.key())) {
            removeGun(it.key());
            it = m_infos.begin();  // reiniciar iterador tras borrar
        } else {
            ++it;
        }
    }
}

void GunCon3Manager::addGun(const QString &devicePath, const QString &portPath)
{
    // ── Determinar índice de jugador ──────────────────────────────────────
    int idx = loadAssignment(portPath);
    if (idx < 0) {
        // Puerto nuevo: asignar el siguiente libre y persistir
        idx = nextFreeIndex();
        saveAssignment(portPath, idx);
        qDebug() << "[Manager] Puerto nuevo, asignado P" << (idx+1) << ":" << portPath;
    } else {
        qDebug() << "[Manager] Puerto reconocido, restaurado P" << (idx+1) << ":" << portPath;
    }

    // ── Crear y abrir la pistola ──────────────────────────────────────────
    auto *gun = new GunCon3(devicePath, this);

    connect(gun, &GunCon3::disconnected, this, [this, devicePath]() {
        removeGun(devicePath);
    });

    if (!gun->open()) {
        // open() falla si la pistola no tiene WinUSB asignado.
        // Instalar el driver y relanzar un scan tras 2 s.
        delete gun;
        qDebug() << "[Manager] open() fallo, intentando instalar WinUSB...";
        QString err;
        if (!installWinUsbDriver(err))
            qWarning() << "[Manager] No se pudo instalar WinUSB:" << err;
        else
            QTimer::singleShot(2000, this, &GunCon3Manager::scan);
        return;
    }

    GunInfo info;
    info.gun       = gun;
    info.portPath  = portPath;
    info.playerIdx = idx;

    m_infos[devicePath] = info;
    m_guns[devicePath]  = gun;

    emit gunConnected(devicePath, gun, idx);
    qDebug() << "[Manager] GunCon3 P" << (idx+1) << "conectada:" << devicePath.left(50);
}

void GunCon3Manager::removeGun(const QString &devicePath)
{
    auto it = m_infos.find(devicePath);
    if (it == m_infos.end()) return;

    int idx = it->playerIdx;
    if (it->gun) {
        it->gun->close();
        it->gun->deleteLater();
    }
    m_infos.erase(it);
    m_guns.remove(devicePath);

    emit gunDisconnected(devicePath, idx);
    qDebug() << "[Manager] GunCon3 P" << (idx+1) << "desconectada.";
}

int GunCon3Manager::playerIndex(const QString &devicePath) const
{
    auto it = m_infos.constFind(devicePath);
    return (it != m_infos.constEnd()) ? it->playerIdx : -1;
}

void GunCon3Manager::reassignPlayerIndex(const QString &devicePath, int newIndex)
{
    auto it = m_infos.find(devicePath);
    if (it == m_infos.end()) return;

    // Si otro dispositivo ya tiene ese índice, intercambiar
    for (auto &other : m_infos) {
        if (&other != &it.value() && other.playerIdx == newIndex) {
            int oldIdx = it->playerIdx;
            other.playerIdx = oldIdx;
            saveAssignment(other.portPath, oldIdx);
            emit playerIndexChanged(other.portPath, oldIdx);
            qDebug() << "[Manager] Intercambio: otro dispositivo ahora es P" << (oldIdx+1);
            break;
        }
    }

    it->playerIdx = newIndex;
    saveAssignment(it->portPath, newIndex);
    emit playerIndexChanged(devicePath, newIndex);
    qDebug() << "[Manager] Reasignado P" << (newIndex+1) << ":" << devicePath.left(50);
}
