/*
 * driverinstaller.cpp
 *
 * Instala WinUSB como driver para la GunCon3 sin necesitar ningún instalador
 * externo. Funciona generando un INF temporal y usando SetupAPI para asociar
 * el driver WinUSB (incluido en Windows 8.1+) al VID/PID del dispositivo.
 *
 * Requiere UAC (elevación) la primera vez. Las siguientes no necesitan nada.
 */

#include "driverinstaller.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QStandardPaths>

#include <windows.h>
#include <setupapi.h>
#include <newdev.h>       // UpdateDriverForPlugAndPlayDevicesW
#include <cfgmgr32.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "cfgmgr32.lib")

#define GUNCON3_VID  "0B9A"
#define GUNCON3_PID  "0800"
// Hardware ID que Windows usa para identificar el dispositivo
#define GUNCON3_HWID L"USB\\VID_0B9A&PID_0800"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Devuelve true si la GunCon3 ya tiene WinUSB asignado
static bool guncon3AlreadyHasWinUsb()
{
    // Buscamos el dispositivo por su Hardware ID y comprobamos el driver activo
    HDEVINFO hdi = SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
                                        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (hdi == INVALID_HANDLE_VALUE)
        return false;

    SP_DEVINFO_DATA did;
    did.cbSize = sizeof(did);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hdi, i, &did); ++i) {
        // Leer HardwareID
        WCHAR hwid[512] = {};
        if (!SetupDiGetDeviceRegistryPropertyW(hdi, &did, SPDRP_HARDWAREID,
                                               nullptr,
                                               reinterpret_cast<PBYTE>(hwid),
                                               sizeof(hwid), nullptr))
            continue;

        // hwid puede tener múltiples IDs separados por '\0'
        bool found = false;
        for (WCHAR *p = hwid; *p; p += wcslen(p) + 1) {
            if (_wcsicmp(p, GUNCON3_HWID) == 0) {
                found = true;
                break;
            }
        }
        if (!found) continue;

        // Leer el driver asignado (Service)
        WCHAR service[256] = {};
        SetupDiGetDeviceRegistryPropertyW(hdi, &did, SPDRP_SERVICE,
                                          nullptr,
                                          reinterpret_cast<PBYTE>(service),
                                          sizeof(service), nullptr);

        SetupDiDestroyDeviceInfoList(hdi);
        // WinUSB service = "WinUSB"
        return (_wcsicmp(service, L"WinUSB") == 0);
    }

    SetupDiDestroyDeviceInfoList(hdi);
    return false;   // Dispositivo no encontrado (no está conectado)
}

// ─────────────────────────────────────────────────────────────────────────────
// Genera el INF temporal en %TEMP%\gcon3_winusb\
// ─────────────────────────────────────────────────────────────────────────────
static QString generateInf(QString &errorMsg)
{
    QString tmpDir = QDir::tempPath() + "/gcon3_winusb";
    QDir().mkpath(tmpDir);
    QString infPath = tmpDir + "/gcon3.inf";

    QFile f(infPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMsg = "No se pudo crear el INF temporal: " + infPath;
        return {};
    }

    QTextStream ts(&f);
    ts <<
R"([Version]
Signature   = "$Windows NT$"
Class       = USB
ClassGuid   = {36FC9E60-C465-11CF-8056-444553540000}
Provider    = %ManufacturerName%
DriverVer   = 01/01/2024,1.0.0.0
CatalogFile = gcon3.cat

[Manufacturer]
%ManufacturerName% = Standard,NTamd64

[Standard.NTamd64]
%DeviceName% = USB_Install, USB\VID_0B9A&PID_0800

[USB_Install]
Include     = winusb.inf
Needs       = WINUSB.NT

[USB_Install.Services]
Include     = winusb.inf
AddService  = WinUSB,0x00000002,WinUSB_ServiceInstall

[WinUSB_ServiceInstall]
DisplayName     = %WinUSB_SvcDesc%
ServiceType     = 1
StartType       = 3
ErrorControl    = 1
ServiceBinary   = %12%\WinUSB.sys

[USB_Install.Wdf]
KmdfService = WinUSB, WinUSB_wdfsect

[WinUSB_wdfsect]
KmdfLibraryVersion = 1.15

[USB_Install.HW]
AddReg = Dev_AddReg

[Dev_AddReg]
HKR,,DeviceInterfaceGUIDs,0x10000,"{5D57F983-3D3A-4B10-A785-B47B3EF2E8B4}"

[Strings]
ManufacturerName = "Namco"
DeviceName       = "GunCon 3"
WinUSB_SvcDesc   = "WinUSB Driver Service"
)";
    f.close();
    return infPath;
}

// ─────────────────────────────────────────────────────────────────────────────
// Relanza el ejecutable actual con privilegios de administrador (UAC)
// pasando el argumento --install-driver. Retorna true si se lanzó OK.
// ─────────────────────────────────────────────────────────────────────────────
static bool relaunchAsAdmin()
{
    QString exe = QCoreApplication::applicationFilePath();
    std::wstring wexe = exe.toStdWString();

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize       = sizeof(sei);
    sei.lpVerb       = L"runas";
    sei.lpFile       = wexe.c_str();
    sei.lpParameters = L"--install-driver";
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.nShow        = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED)
            return false;  // Usuario canceló UAC
        return false;
    }

    // Esperamos a que el proceso elevado termine (máx 30 s)
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 30000);
        CloseHandle(sei.hProcess);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Función pública
// ─────────────────────────────────────────────────────────────────────────────
bool installWinUsbDriver(QString &errorMsg)
{
    // 1. Si ya tiene WinUSB (o el dispositivo no está conectado), nada que hacer
    if (guncon3AlreadyHasWinUsb()) {
        qDebug() << "[DriverInstaller] GunCon3 ya tiene WinUSB asignado.";
        return true;
    }

    // 2. ¿El proceso actual es ya administrador?
    bool isAdmin = false;
    {
        HANDLE token = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            TOKEN_ELEVATION elev;
            DWORD sz = sizeof(elev);
            if (GetTokenInformation(token, TokenElevation, &elev, sz, &sz))
                isAdmin = (elev.TokenIsElevated != 0);
            CloseHandle(token);
        }
    }

    // 3. Si NO somos admin, el argumento --install-driver se pasa al proceso
    //    elevado. Si SÍ somos admin (o nos elevamos), instalamos el driver.

    // ¿Estamos en el proceso elevado?
    bool installMode = false;
    const auto args = QCoreApplication::arguments();
    for (const auto &a : args)
        if (a == "--install-driver") { installMode = true; break; }

    if (!isAdmin && !installMode) {
        // Relanzamos con UAC y esperamos
        qDebug() << "[DriverInstaller] Solicitando elevación UAC...";
        if (!relaunchAsAdmin()) {
            errorMsg = "El usuario canceló la elevación de privilegios.\n"
                       "El driver WinUSB no pudo instalarse.";
            return false;
        }
        // Después de que el proceso elevado terminó, comprobamos de nuevo
        if (guncon3AlreadyHasWinUsb())
            return true;
        errorMsg = "La instalación del driver falló en el proceso elevado.";
        return false;
    }

    // ─── A partir de aquí somos admin ───────────────────────────────────────

    // 4. Generamos el INF
    QString infPath = generateInf(errorMsg);
    if (infPath.isEmpty())
        return false;

    qDebug() << "[DriverInstaller] INF generado en:" << infPath;

    // 5. Llamamos a UpdateDriverForPlugAndPlayDevices
    std::wstring wInf = infPath.toStdWString();
    BOOL reboot = FALSE;

    // HWID del dispositivo tal como aparece en el administrador de dispositivos
    BOOL ok = UpdateDriverForPlugAndPlayDevicesW(
        nullptr,                    // hwnd (sin ventana padre)
        GUNCON3_HWID,               // Hardware ID
        wInf.c_str(),               // ruta al INF
        INSTALLFLAG_FORCE,          // forzar aunque ya haya uno
        &reboot
        );

    if (!ok) {
        DWORD err = GetLastError();
        errorMsg = QString("UpdateDriverForPlugAndPlayDevicesW falló. "
                           "Código: 0x%1").arg(err, 8, 16, QChar('0'));
        qWarning() << "[DriverInstaller]" << errorMsg;
        return false;
    }

    qDebug() << "[DriverInstaller] Driver instalado correctamente."
             << (reboot ? "(se requiere reinicio)" : "");

    // Si estábamos en modo --install-driver (proceso hijo), salimos aquí
    // para que el proceso padre pueda continuar.
    if (installMode)
        QCoreApplication::quit();

    return true;
}
