#include "calibrador.h"
#include <QPainter>
#include <QScreen>
#include <QApplication>
#include <QDebug>

Calibrador::Calibrador(QWidget *parent) : QWidget(parent)
{
    showFullScreen();
    setWindowTitle("Calibracion GunCon3");

    // Puntos de mira en píxeles (esquinas con margen + centro)
    int W = width(), H = height();
    int M = 60;  // margen en píxeles
    m_targetsPx = {
        QPoint(M,   M),
        QPoint(W-M, M),
        QPoint(W-M, H-M),
        QPoint(M,   H-M),
        QPoint(W/2, H/2),
    };

    // Convertir a [0..32767]
    for (const QPoint &p : m_targetsPx) {
        int sx = (W > 1) ? p.x() * 32767 / (W - 1) : 0;
        int sy = (H > 1) ? p.y() * 32767 / (H - 1) : 0;
        m_targets32k.append(QPoint(sx, sy));
    }

    m_captureTimer = new QTimer(this);
    m_captureTimer->setSingleShot(true);
}

void Calibrador::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    if (m_current >= m_targetsPx.size()) return;

    QPoint pt = m_targetsPx[m_current];

    // Cruz exterior blanca
    p.setPen(QPen(Qt::white, 2));
    p.drawLine(pt.x() - 25, pt.y(), pt.x() + 25, pt.y());
    p.drawLine(pt.x(), pt.y() - 25, pt.x(), pt.y() + 25);

    // Círculo interior rojo
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::red);
    p.drawEllipse(pt, 10, 10);

    // Texto de instrucción
    p.setPen(Qt::white);
    p.setFont(QFont("Arial", 14));
    p.drawText(10, 30,
        QString("Punto %1 de %2 — Apunta y dispara")
            .arg(m_current + 1).arg(m_targetsPx.size()));

    if (m_lastStatusWarning) {
        p.setPen(Qt::yellow);
        p.setFont(QFont("Arial", 13));
        p.drawText(10, 55, "Apunta directamente a la pantalla (ambos LEDs deben ser visibles)");
    } else if (!m_zSamples.isEmpty()) {
        int zAvg = 0;
        for (int v : m_zSamples) zAvg += v;
        zAvg /= m_zSamples.size();
        p.setPen(Qt::white);
        p.drawText(10, 55,
            QString("z = %1  (mantén la distancia de juego)").arg(zAvg));
    }
}

void Calibrador::setAim(QPoint cam, int z, GunCon3::GunStatus status, quint32 buttons)
{
    if (!m_waitingShot) return;

    // Acumular muestras de z solo cuando ambos LEDs son visibles
    if (status == GunCon3::GunStatus::IN_RANGE && z > 0) {
        m_zSamples.append(z);
        if (m_zSamples.size() > 10) m_zSamples.removeFirst();
    }
    update();

    // Solo aceptar disparo con ambos LEDs visibles (IN_RANGE)
    // Si el jugador dispara con ONE_REF u OUT_OF_RANGE, ignorar
    if (!(buttons & 0x00002000)) return;  // no es trigger

    if (status != GunCon3::GunStatus::IN_RANGE) {
        // Avisar visualmente que no se puede calibrar en este estado
        // (el repintado mostrará un mensaje de advertencia)
        m_lastStatusWarning = true;
        update();
        return;
    }
    m_lastStatusWarning = false;

    m_waitingShot = false;
    m_capturedCam.append(cam);
    m_capturedZ.append(z > 0 ? z : 1);

    qDebug() << "[Calibrador] Punto" << m_current + 1
             << "cam=" << cam << "z=" << z;

    m_captureTimer->singleShot(700, this, [this]() {
        m_waitingShot = true;
        m_zSamples.clear();
        m_lastStatusWarning = false;
        nextPoint();
    });
}

void Calibrador::nextPoint()
{
    m_current++;
    if (m_current >= m_targetsPx.size()) {
        // Calibración completa
        emit calibrationPoints(m_capturedCam, m_targets32k, m_capturedZ);
        close();
    }
    update();
}
