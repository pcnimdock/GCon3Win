#include "calibrador.h"
#include <QPainter>
#include <QDebug>

Calibrador::Calibrador(QWidget *parent) : QWidget(parent), puntoActual(0), esperandoDisparo(true)
{
    //setFixedSize(800, 600);
    showFullScreen();
    setWindowTitle("Calibración GunCon3");

        puntosObjetivo = {
            QPoint(50, 50),
            QPoint(width() - 50, 50),
            QPoint(width() - 50, height() - 50),
            QPoint(50, height() - 50),
            QPoint(width() / 2, height() / 2)
        };
}

void Calibrador::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    p.setBrush(Qt::red);
    p.setPen(Qt::NoPen);
    if (puntoActual < puntosObjetivo.size())
        p.drawEllipse(puntosObjetivo[puntoActual], 15, 15);

    p.setPen(Qt::white);
    p.drawText(10, 20, QString("Punto %1 de %2").arg(puntoActual+1).arg(puntosObjetivo.size()));
}

void Calibrador::setAim(QPoint aim, quint32 buttons)
{
    if (!esperandoDisparo) return;
    if (buttons & 0x00002000) { // TRIGGER
        capturados << aim;
        esperandoDisparo = false;
        QTimer::singleShot(800, this, [this]() {
            esperandoDisparo = true;
            siguientePunto();
        });
    }
}

void Calibrador::siguientePunto()
{
    puntoActual++;
    if (puntoActual >= puntosObjetivo.size()) {
        QRect crudo, real;
        for (const QPoint &p : capturados)
            crudo |= QRect(p, QSize(1, 1));
        for (const QPoint &p : puntosObjetivo)
            real |= QRect(p, QSize(1, 1));
        emit calibracionCompleta(crudo, real);
        close();
    }
    update();
}
