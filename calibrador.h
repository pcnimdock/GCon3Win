#ifndef CALIBRADOR_H
#define CALIBRADOR_H

#include <QWidget>
#include <QVector>
#include <QPoint>
#include <QTimer>
#include "guncon3.h"

/*
 * Calibrador
 *
 * Pantalla completa negra que muestra 5 puntos de mira (esquinas + centro).
 * Por cada punto captura (cam_x, cam_y, z) en el momento del disparo.
 *
 * Al terminar emite calibrationPoints con todos los puntos capturados.
 * La conversión a coordenadas de pantalla [0..32767] se hace aquí, usando
 * la posición en píxeles de cada punto de mira sobre la pantalla.
 */
class Calibrador : public QWidget
{
    Q_OBJECT
public:
    explicit Calibrador(QWidget *parent = nullptr);

    // Alimentar con datos crudos de la pistola (desde la señal gunDataReceived)
    // Solo acepta disparos cuando status == IN_RANGE (ambos LEDs visibles)
    void setAim(QPoint cam, int z, GunCon3::GunStatus status, quint32 buttons);

signals:
    // Emitido al capturar los 5 puntos.
    // cam[i]    = coordenada cruda de cámara capturada
    // screen[i] = posición en pantalla del punto de mira [0..32767]
    // zValues[i]= z capturado en ese disparo
    void calibrationPoints(QVector<QPoint> cam,
                           QVector<QPoint> screen,
                           QVector<int>    zValues);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void nextPoint();

    // Puntos de mira en coordenadas de widget (píxeles)
    QVector<QPoint> m_targetsPx;
    // Puntos de mira convertidos a [0..32767]
    QVector<QPoint> m_targets32k;

    QVector<QPoint> m_capturedCam;
    QVector<int>    m_capturedZ;

    int  m_current       { 0 };
    bool m_waitingShot   { true };

    // Promedio de z durante la ventana de captura
    QVector<int> m_zSamples;
    QTimer      *m_captureTimer { nullptr };
    bool         m_lastStatusWarning { false };
};

#endif // CALIBRADOR_H
