#ifndef CALIBRADOR_H
#define CALIBRADOR_H

#include <QWidget>
#include <QVector>
#include <QPoint>
#include <QTimer>

class Calibrador : public QWidget
{
    Q_OBJECT
public:
    explicit Calibrador(QWidget *parent = nullptr);
    void setAim(QPoint aim, quint32 buttons);

signals:
    void calibracionCompleta(QRect crudo, QRect real);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QPoint> puntosObjetivo;
    QVector<QPoint> capturados;
    int puntoActual;
    bool esperandoDisparo;

    void siguientePunto();
};

#endif // CALIBRADOR_H
