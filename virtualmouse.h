#ifndef VIRTUALMOUSE_H
#define VIRTUALMOUSE_H

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QList>
#include <windows.h>
#include "guncon3.h"

/*
 * VirtualMouse
 *
 * Ratón HID absoluto virtual (uno por pistola) via Virtual HID Framework.
 *
 * ── Calibración con corrección de distancia (z) ───────────────────────────
 *
 * La GunCon3 reporta en cada frame:
 *   (x, y) = centro de los dos LEDs en coordenadas de la cámara (píxeles)
 *   z      = separación en píxeles de los dos LEDs en la imagen
 *
 * z actúa como divisor de escala: cuanto más cerca está la pistola de la
 * pantalla, mayor es z. Sin corregir z, acercarse hace que el cursor vaya
 * al centro y alejarse lo lleva a los bordes.
 *
 * La corrección se implementa en dos pasos:
 *
 *  1. CALIBRACIÓN (a distancia fija z_ref):
 *     Se capturan N puntos (cam_x, cam_y) → (screen_x, screen_y) con la
 *     pistola a la distancia cómoda de juego. Se calcula una homografía 2D
 *     H tal que screen = H * cam.
 *     Se guarda también z_ref = z promedio durante la calibración.
 *
 *  2. CORRECCIÓN POR FRAME:
 *     Para cada frame con z actual:
 *       scale   = z_ref / z          (si z=0, sin corrección)
 *       cx, cy  = centro óptico ≈ promedio de puntos calibración
 *       x_corr  = cx + (cam_x - cx) * scale
 *       y_corr  = cy + (cam_y - cy) * scale
 *     Luego se aplica la homografía H a (x_corr, y_corr).
 *
 * El centro óptico (cx, cy) se estima como el centroide de los puntos de
 * calibración, lo que es una aproximación válida para uso práctico sin
 * necesitar calibración intrínseca de la cámara.
 */
class VirtualMouse : public QObject
{
    Q_OBJECT
public:
    explicit VirtualMouse(QObject *parent = nullptr);
    ~VirtualMouse();

    bool initialize();
    bool isAvailable() const { return m_available; }

    // ── Calibración ───────────────────────────────────────────────────────

    // Añade un punto de calibración: posición cruda de cámara → posición
    // en pantalla [0..32767], con el z medido en ese momento.
    // Llamar al menos 4 veces (esquinas + centro recomendado) antes de
    // commitCalibration().
    void addCalibrationPoint(QPoint cam, QPoint screen, int z);

    // Calcula y activa la homografía a partir de los puntos acumulados.
    // Devuelve false si hay menos de 4 puntos o son colineales.
    bool commitCalibration();

    // Descarta los puntos pendientes (sin tocar la calibración activa)
    void clearCalibrationPoints();

    bool hasCalibration() const { return m_calibrated; }

    // Serialización para persistencia
    struct CalibData {
        double H[9]   {};  // homografía 3x3 (fila mayor)
        double cx     {};  // centro óptico X
        double cy     {};  // centro óptico Y
        double zRef   {};  // z de referencia (distancia de calibración)
        bool   valid  { false };
    };
    CalibData calibrationData() const;
    void setCalibrationData(const CalibData &d);

    // ── Uso por frame ─────────────────────────────────────────────────────

    // Aplica corrección de z + homografía y devuelve coordenadas [0..32767].
    // status indica la fiabilidad del frame:
    //   IN_RANGE    → corrección z + homografía completa
    //   ONE_REF     → z no fiable, se aplica solo homografía sin corrección z
    //   OUT_OF_RANGE → devuelve QPoint(-1,-1) para indicar "no actualizar"
    QPoint applyCalibration(QPoint cam, int z, GunCon3::GunStatus status) const;

    void sendReport(QPoint screenPos, quint32 buttons);

private:
    bool   m_available  { false };
    HANDLE m_device     { INVALID_HANDLE_VALUE };

    // ── Calibración ───────────────────────────────────────────────────────
    struct RawPoint { double camX, camY, scrX, scrY, z; };
    QList<RawPoint> m_pendingPoints;

    bool   m_calibrated { false };
    double m_H[9]       {};   // homografía activa
    double m_cx         {};   // centro óptico estimado
    double m_cy         {};
    double m_zRef       {};   // z de referencia (distancia de calibración)

    // Calcula la homografía usando DLT normalizado + iteración inversa LU
    bool computeHomography(const QList<RawPoint> &pts, double H[9]);

    // Aplica la homografía H a un punto (x,y) → (ox,oy)
    static void applyH(const double H[9], double x, double y,
                       double &ox, double &oy);

    static constexpr quint32 kButtonMap[8] = {
        0x00002000, 0x00000400, 0x00000200, 0x00800000,
        0x00000004, 0x00000002, 0x00400000, 0x00000008,
    };
};

#endif // VIRTUALMOUSE_H
