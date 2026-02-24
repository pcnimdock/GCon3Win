#ifndef DRIVERINSTALLER_H
#define DRIVERINSTALLER_H

#include <QString>
#include <windows.h>

// Instala WinUSB como driver para la GunCon3 (VID 0x0B9A, PID 0x0800)
// si todavía no lo tiene asignado.
// Devuelve true si ya estaba instalado o si la instalación fue exitosa.
// Devuelve false y rellena 'errorMsg' si algo falla.
bool installWinUsbDriver(QString &errorMsg);

#endif // DRIVERINSTALLER_H
