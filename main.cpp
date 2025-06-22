#include <QApplication>
#include "trayapp.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    TrayApp tray;
    return app.exec();
}
