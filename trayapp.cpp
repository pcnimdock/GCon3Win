#include "trayapp.h"
#include <QSettings>
#include <QDebug>
#include <QMessageBox>

TrayApp::TrayApp(QObject *parent) : QObject(parent)
{
    gun = new GunCon3(this);
    bridge = new GunCon3VJoyBridge(this);
    mouse = new MouseEmulator(this);

    connect(gun, &GunCon3::gunDataReceived, this, [this](QPoint aim, QPoint stickA, QPoint stickB, quint32 buttons) {
        if (!rectCrudo.isNull() && !rectReal.isNull()) {
            QPoint p = aim;
            int x = (p.x()*1.0 - rectCrudo.left()*1.0) * 65535.0 / rectCrudo.width()*1.0 - 32767.0;
            int y = (p.y()*1.0 - rectCrudo.top()*1.0) * 65535.0 / rectCrudo.height()*1.0 - 32767.0;
            y*=-1;
            aim = QPoint(x, y);
        }



        bridge->update(aim, stickA, stickB, buttons);
        int mX = (LONG)((aim.x() + 32768) * 32767 / 65535.0);
        int mY = (LONG)((aim.y() + 32768) * 32767 / 65535.0);
        // Mover ratón
        if(mouseHabilitado==true)
        {
        mouse->moverRaton(QPoint(mX,mY));

        // Si se pulsa el trigger, hacer clic
        if (buttons & 0x00002000) {
            mouse->clicIzquierdo();
            }
        }

    });

    connect(gun, &GunCon3::errorOccurred, this, [this](const QString &err) {
        errorPendiente = err;
        QTimer::singleShot(0, this, &TrayApp::mostrarErrorYSalir);
    });
    if (!bridge->initialize()) {
        qWarning() << "[ERROR] No se pudo inicializar vJoy";
        errorPendiente = "[ERROR] No se pudo inicializar vJoy";
        QTimer::singleShot(0, this, &TrayApp::mostrarErrorYSalir);
    }

    gun->open();

    //trayIcon = new QSystemTrayIcon(QIcon::fromTheme("input-gaming"), this);
    trayIcon = new QSystemTrayIcon(QIcon(":/icono.ico"), this);
    //trayIcon = new QSystemTrayIcon(QIcon(":/icono.png"), this);
    menu = new QMenu();
    calibrarAction = menu->addAction("Calibrar GunCon3", this, &TrayApp::calibrar);

    mouseToggleAction = menu->addAction("Habilitar ratón", this, [this]() {
    mouseHabilitado = !mouseHabilitado;
    mouseToggleAction->setText(mouseHabilitado ? "Deshabilitar ratón" : "Habilitar ratón");
    });

    salirAction = menu->addAction("Salir", qApp, &QCoreApplication::quit);

    trayIcon->setContextMenu(menu);
    trayIcon->show();

    QSettings s("calibracion.ini", QSettings::IniFormat);
    rectCrudo = s.value("calib/crudo").toRect();
    rectReal = s.value("calib/real").toRect();

}

void TrayApp::calibrar()
{
    Calibrador *c = new Calibrador();
    c->setAttribute(Qt::WA_DeleteOnClose);
    connect(gun, &GunCon3::gunDataReceived, c, [c](QPoint aim, QPoint, QPoint, quint32 buttons){
        c->setAim(aim, buttons);
    });
    connect(c, &Calibrador::calibracionCompleta, this, &TrayApp::aplicarCalibracion);
    c->show();
}

void TrayApp::aplicarCalibracion(QRect crudo, QRect real)
{
    rectCrudo = crudo;
    rectReal = real;

    QSettings s("calibracion.ini", QSettings::IniFormat);
    s.setValue("calib/crudo", crudo);
    s.setValue("calib/real", real);
}


void TrayApp::mostrarErrorYSalir()
{
    if (errorPendiente.contains("desconectada", Qt::CaseInsensitive)) {
        intentos = 0;
        intentarReconectar();  // Lanzamos el primer intento
    } else {
        if(intentos <1)
        {
        trayIcon->hide();
        QMessageBox::critical(nullptr, "Error crítico", errorPendiente);
            qApp->quit();
        }
    }
}

void TrayApp::intentarReconectar()
{
    qDebug() << QString("[INFO] Intento de reconexión %1/%2...").arg(intentos + 1).arg(maxIntentos);

                if (!gun->reconnect()) {
            intentos++;
            if (intentos < maxIntentos) {
                QTimer::singleShot(1000, this, &TrayApp::intentarReconectar);
            } else {
                trayIcon->hide();
                QMessageBox::critical(nullptr, "Error", "No se pudo reconectar GunCon3 tras 10 intentos.");
                qApp->quit();
            }
    } else {
            qDebug() << "[INFO] Reconexión exitosa.";
                intentos = 0;
    }
}
