/*
 * driverinstaller.cpp
 *
 * Instala WinUSB como driver para la GunCon3 sin necesitar ningún instalador
 * externo. Solo actúa cuando la pistola está conectada y no tiene WinUSB.
 */

#include "driverinstaller.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>

#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "advapi32.lib")

#define GUNCON3_HWID L"USB\\VID_0B9A&PID_0800"

// ─────────────────────────────────────────────────────────────────────────────
// Estado del driver de la GunCon3
// ─────────────────────────────────────────────────────────────────────────────
enum class GunConDriverState {
    NotConnected,  // pistola no enchufada → no hacer nada
    HasWinUsb,     // enchufada y con WinUSB → todo OK
    NeedsDriver,   // enchufada pero sin WinUSB → instalar
};

static GunConDriverState checkGunConDriverState()
{
    HDEVINFO hdi = SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
                                        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (hdi == INVALID_HANDLE_VALUE)
        return GunConDriverState::NotConnected;

    SP_DEVINFO_DATA did;
    did.cbSize = sizeof(did);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hdi, i, &did); ++i) {
        WCHAR hwid[512] = {};
        if (!SetupDiGetDeviceRegistryPropertyW(hdi, &did, SPDRP_HARDWAREID,
                                               nullptr,
                                               reinterpret_cast<PBYTE>(hwid),
                                               sizeof(hwid), nullptr))
            continue;

        bool found = false;
        for (WCHAR *p = hwid; *p; p += wcslen(p) + 1)
            if (_wcsicmp(p, GUNCON3_HWID) == 0) { found = true; break; }
        if (!found) continue;

        // Pistola enchufada — leer su driver activo
        WCHAR service[256] = {};
        SetupDiGetDeviceRegistryPropertyW(hdi, &did, SPDRP_SERVICE, nullptr,
                                          reinterpret_cast<PBYTE>(service),
                                          sizeof(service), nullptr);
        SetupDiDestroyDeviceInfoList(hdi);

        return (_wcsicmp(service, L"WinUSB") == 0)
               ? GunConDriverState::HasWinUsb
               : GunConDriverState::NeedsDriver;
    }

    SetupDiDestroyDeviceInfoList(hdi);
    return GunConDriverState::NotConnected;
}

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
static bool isRunningAsAdmin()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elev;
    DWORD sz = sizeof(elev);
    bool admin = false;
    if (GetTokenInformation(token, TokenElevation, &elev, sz, &sz))
        admin = (elev.TokenIsElevated != 0);
    CloseHandle(token);
    return admin;
}

static bool relaunchAsAdmin()
{
    std::wstring wexe = QCoreApplication::applicationFilePath().toStdWString();
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize       = sizeof(sei);
    sei.lpVerb       = L"runas";
    sei.lpFile       = wexe.c_str();
    sei.lpParameters = L"--install-driver";
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.nShow        = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) return false;
    if (sei.hProcess) { WaitForSingleObject(sei.hProcess, 30000); CloseHandle(sei.hProcess); }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool installWinUsbDriver(QString &errorMsg)
{
    GunConDriverState state = checkGunConDriverState();

    // Sin pistola conectada: no hay nada que instalar, no es un error
    if (state == GunConDriverState::NotConnected) {
        qDebug() << "[DriverInstaller] GunCon3 no conectada, omitiendo instalacion.";
        return true;
    }

    // Ya tiene WinUSB: perfecto
    if (state == GunConDriverState::HasWinUsb) {
        qDebug() << "[DriverInstaller] GunCon3 ya tiene WinUSB.";
        return true;
    }

    // Necesita driver — comprobar si somos admin
    if (!isRunningAsAdmin()) {
        qDebug() << "[DriverInstaller] Solicitando elevacion UAC...";
        if (!relaunchAsAdmin()) {
            errorMsg = "El usuario cancelo la elevacion de privilegios.\n"
                       "Conecta la GunCon3 y ejecuta la app como Administrador\n"
                       "para instalar el driver WinUSB.";
            return false;
        }
        // El proceso elevado ya instaló; verificar resultado
        if (checkGunConDriverState() == GunConDriverState::HasWinUsb)
            return true;
        errorMsg = "La instalacion del driver fallo en el proceso elevado.";
        return false;
    }

    // Somos admin: instalar directamente
    QString infPath = generateInf(errorMsg);
    if (infPath.isEmpty()) return false;

    qDebug() << "[DriverInstaller] Instalando WinUSB desde:" << infPath;

    std::wstring wInf = infPath.toStdWString();
    BOOL reboot = FALSE;
    BOOL ok = UpdateDriverForPlugAndPlayDevicesW(
        nullptr, GUNCON3_HWID, wInf.c_str(), INSTALLFLAG_FORCE, &reboot);

    if (!ok) {
        DWORD err = GetLastError();
        errorMsg = QString("UpdateDriverForPlugAndPlayDevicesW fallo. "
                           "Codigo: 0x%1").arg(err, 8, 16, QChar('0'));
        qWarning() << "[DriverInstaller]" << errorMsg;
        return false;
    }

    qDebug() << "[DriverInstaller] Driver instalado."
             << (reboot ? "(requiere reinicio)" : "");

    if (QCoreApplication::arguments().contains("--install-driver"))
        QCoreApplication::quit();

    return true;
}
