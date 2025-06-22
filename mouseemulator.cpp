#include "mouseemulator.h"
#include <windows.h>
#include <thread>
#include <chrono>
#include <QDebug>


MouseEmulator::MouseEmulator(QObject *parent)
{

}

MouseEmulator::~MouseEmulator()
{

}

void MouseEmulator::moverRaton(const QPoint &pos)
{
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Invertimos el eje Y
    int correctedY = 32767 - pos.y();
    // Escalar directamente del rango [0..32767] a la resolución de la pantalla
    int xPixel = pos.x() * (screenWidth - 1) / 32767;
    int yPixel = correctedY * (screenHeight - 1) / 32767;

    // Luego convertir esos píxeles a coordenadas absolutas [0..65535] para SendInput
    int absX = xPixel * 65535 / (screenWidth - 1);
    int absY = yPixel * 65535 / (screenHeight - 1);

//    qDebug() << "Entrada GunCon3:" << pos;
//    qDebug() << "Resolución pantalla:" << screenWidth << "x" << screenHeight;
//        qDebug() << "Coordenadas píxel:" << xPixel << "," << yPixel;
//                    qDebug() << "Coordenadas absolutas:" << absX << "," << absY;

    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dx = absX;
    input.mi.dy = absY;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

    SendInput(1, &input, sizeof(INPUT));
    SetCursorPos(xPixel, yPixel); // Esto es opcional si SendInput ya mueve el ratón
}

void MouseEmulator::clicIzquierdo()
{
    INPUT input[2] = {};
    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;

    SendInput(2, input, sizeof(INPUT));
}

void MouseEmulator::clicDerecho()
{
    INPUT input[2] = {};
    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;

    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;

    SendInput(2, input, sizeof(INPUT));
}

void MouseEmulator::dobleClicIzquierdo()
{
    clicIzquierdo();
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Pausa corta entre clics
    clicIzquierdo();
}
