#ifndef MOUSEEMULATOR_H
#define MOUSEEMULATOR_H

#include <QObject>
#include <QPoint>

class MouseEmulator
{
public:
    explicit MouseEmulator(QObject *parent = nullptr);
    ~MouseEmulator();
    static void moverRaton(const QPoint &pos);             // mueve a una posición absoluta
    static void clicIzquierdo();
    static void clicDerecho();
    static void dobleClicIzquierdo();
};

#endif // MOUSEEMULATOR_H
