#include <QApplication>
#include "trayapp.h"
#include "driverinstaller.h"
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    // Modo elevado: solo instalar el driver y salir
    if (app.arguments().contains("--install-driver")) {
        QString err;
        bool ok = installWinUsbDriver(err);
        if (!ok)
            QMessageBox::critical(nullptr, "Error instalando driver", err);
        return ok ? 0 : 1;
    }

    TrayApp tray;
    return app.exec();
}
