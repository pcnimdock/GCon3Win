#ifndef TRAYAPP_H
#define TRAYAPP_H

#include <QCoreApplication>
#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include "guncon3.h"
#include "guncon3vjoybridge.h"
#include "calibrador.h"
#include "mouseemulator.h"

class TrayApp : public QObject
{
    Q_OBJECT
public:
    explicit TrayApp(QObject *parent = nullptr);

private slots:
    void calibrar();
    void aplicarCalibracion(QRect crudo, QRect real);

private:
    void mostrarErrorYSalir();
    QSystemTrayIcon *trayIcon;
    QMenu *menu;
    QAction *calibrarAction;
    QAction *salirAction;
    QAction *mouseToggleAction;
    bool mouseHabilitado = false;

    GunCon3 *gun;
    GunCon3VJoyBridge *bridge;
    QRect rectCrudo;
    QRect rectReal;
    MouseEmulator *mouse;
    QString errorPendiente;

    int intentos = 0;
    const int maxIntentos = 10;
    void intentarReconectar();
};

#endif // TRAYAPP_H
