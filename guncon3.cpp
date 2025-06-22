#include "guncon3.h"
#include <QDebug>
#include <QString>
#include <initguid.h>
#include <devpkey.h>
#include <windows.h>
#include <winusb.h>
#include <setupapi.h>

#include <Usb100.h>
#include <Usbiodef.h>
#include <Cfgmgr32.h>

#define GUNCON3_VID 0x0b9a
#define GUNCON3_PID 0x0800
#define ENDPOINT_IN  0x82
#define ENDPOINT_OUT 0x02

GunCon3::GunCon3(QObject *parent)
    : QObject(parent),
    deviceHandle(INVALID_HANDLE_VALUE),
    usbHandle(nullptr),
    running(false)
{
}

GunCon3::~GunCon3()
{
    close();
}

bool GunCon3::open()
{
    if (!initDevice()) {
        emit errorOccurred("No se encontró GunCon 3");
        return false;
    }

    if (!sendKey()) {
        emit errorOccurred("No se pudo enviar clave de activación");
        return false;
    }

    running = true;
    readerThread = std::thread(&GunCon3::readLoop, this);
    return true;
}

void GunCon3::close()
{
    running = false;

    if (readerThread.joinable())
        readerThread.join();

    if (usbHandle) {
        WinUsb_Free(usbHandle);
        usbHandle = nullptr;
    }

    if (deviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(deviceHandle);
        deviceHandle = INVALID_HANDLE_VALUE;
    }
}

bool GunCon3::isConnected() const
{
    if (!usbHandle)
        return false;

    USB_DEVICE_DESCRIPTOR desc;
    ULONG length = 0;

    // Verificamos si el descriptor aún puede leerse
    BOOL result = WinUsb_GetDescriptor(
        usbHandle,
        USB_DEVICE_DESCRIPTOR_TYPE,
        0,
        0,
        reinterpret_cast<PUCHAR>(&desc),
        sizeof(desc),
        &length
        );

    return result && (desc.idVendor == GUNCON3_VID) && (desc.idProduct == GUNCON3_PID);
}

bool GunCon3::reconnect()
{
    qDebug() << "[DEBUG] Llamando a reconnect()...";
    close();
    if (open()) {
        emit reconnected();
        qDebug() << "[INFO] GunCon3 reconectada correctamente.";
        return true;
    } else {
        qWarning() << "[ERROR] Fallo al reconectar GunCon3.";
        return false;
    }
}

bool GunCon3::isRunning() const
{
    return running;
}

bool GunCon3::initDevice()
{
    GUID guid = GUID_DEVINTERFACE_USB_DEVICE;
    HDEVINFO deviceInfo = SetupDiGetClassDevs(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        emit errorOccurred("SetupDiGetClassDevs falló");
        return false;
    }

    SP_DEVICE_INTERFACE_DATA devInterfaceData;
    devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (int index = 0; SetupDiEnumDeviceInterfaces(deviceInfo, nullptr, &guid, index, &devInterfaceData); ++index) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(deviceInfo, &devInterfaceData, nullptr, 0, &requiredSize, nullptr);

        std::vector<BYTE> detailDataBuffer(requiredSize);
        auto* devDetail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(detailDataBuffer.data());
        devDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (!SetupDiGetDeviceInterfaceDetail(deviceInfo, &devInterfaceData, devDetail, requiredSize, nullptr, nullptr))
            continue;

        HANDLE handle = CreateFile(devDetail->DevicePath, GENERIC_WRITE | GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);

        if (handle == INVALID_HANDLE_VALUE)
            continue;

        WINUSB_INTERFACE_HANDLE tempWinUsb;
        if (!WinUsb_Initialize(handle, &tempWinUsb)) {
            CloseHandle(handle);
            continue;
        }

        // Verificar VID/PID
        USB_DEVICE_DESCRIPTOR desc;
        ULONG length = 0;
        if (!WinUsb_GetDescriptor(tempWinUsb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0,
                                  reinterpret_cast<PUCHAR>(&desc), sizeof(desc), &length)) {
            WinUsb_Free(tempWinUsb);
            CloseHandle(handle);
            continue;
        }

        if (desc.idVendor == GUNCON3_VID && desc.idProduct == GUNCON3_PID) {
            deviceHandle = handle;
            usbHandle = tempWinUsb;
            SetupDiDestroyDeviceInfoList(deviceInfo);
            return true;
        }

        // No es el dispositivo correcto
        WinUsb_Free(tempWinUsb);
        CloseHandle(handle);
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
    return false;
}

bool GunCon3::sendKey()
{
    //static const UCHAR key[8] = {0x01, 0x12, 0x6f, 0x32, 0x24, 0x60, 0x17, 0x21};
    ULONG bytesWritten = 0;

    BOOL success = WinUsb_WritePipe(
        usbHandle,
        ENDPOINT_OUT,
        const_cast<PUCHAR>(key),
        sizeof(key),
        &bytesWritten,
        nullptr
        );

    if (!success || bytesWritten != sizeof(key)) {
        emit errorOccurred("Fallo al enviar clave de activación");
        return false;
    }

    return true;
}

void GunCon3::readLoop()
{
    UCHAR buffer[64]; // mayor a 15 para estar seguros
    ULONG transferred = 0;

    while (running) {
        BOOL success = WinUsb_ReadPipe(
            usbHandle,
            ENDPOINT_IN,
            buffer,
            15,               // ✅ Leer exactamente 15 bytes
            &transferred,
            nullptr
            );

        //if (!success || transferred != 15)
        //    continue;
        if (!success || transferred != 15) {
            if (!isConnected()) {
                emit errorOccurred("GunCon3 desconectada");
                running = false;
                break;
            }
            continue;
        }

        QByteArray raw(reinterpret_cast<char*>(buffer), transferred);
        QByteArray decoded(15, 0);

        if (!decode(raw, decoded))
            continue;

        // Datos decodificados: [0]...[12] (posición y botones)
        const uchar* d = reinterpret_cast<const uchar*>(decoded.constData());

        int16_t x = (d[4] << 8) | d[3];
        int16_t y = -((d[6] << 8) | d[5]);
        //para invertir las direcciones de los sticks

        int16_t ax=255-(d[9]);
        int16_t ay=255-(d[10]);
        int16_t bx=255-(d[11]);
        int16_t by=255-(d[12]);
        QPoint aim(x, y);
        QPoint stickA(ax, ay);
        QPoint stickB(bx, by);

        quint32 buttons = (d[2] << 16) | (d[1] << 8) | d[0];

        //emit gunDataReceived(aim, stickA, stickB, buttons);
        QMetaObject::invokeMethod(this, [=]() {
                emit gunDataReceived(aim, stickA, stickB, buttons);
            }, Qt::QueuedConnection);
    }
}
bool GunCon3::decode(const QByteArray &data, QByteArray &decoded)
{
    if (data.size() != 15)
        return false;


    const uchar* d = reinterpret_cast<const uchar*>(data.constData());

    int b_sum = d[13] ^ d[12];
    b_sum = b_sum + d[11] + d[10] - d[9] - d[8];
    b_sum = b_sum ^ d[7];
    b_sum &= 0xFF;

    int a_sum = d[6] ^ b_sum;
    a_sum = a_sum - d[5] - d[4];
    a_sum = a_sum ^ d[3];
    a_sum = a_sum + d[2] + d[1] - d[0];
    a_sum &= 0xFF;

    if (a_sum != key[7]) {
        return false; // checksum inválido
    }

    int key_offset = key[1] ^ key[2];
    key_offset = key_offset - key[3] - key[4];
    key_offset = key_offset ^ key[5];
    key_offset = key_offset + key[6] - key[7];
    key_offset = key_offset ^ d[14];
    key_offset = (key_offset + 0x26) & 0xFF;

    int key_index = 4;

    for (int x = 12; x >= 0; --x) {
        int byte = d[x];
        for (int y = 0; y < 3; ++y) {
            key_offset--;
            int bkey = KEY_TABLE[(key_offset + 0x41)];
            int keyr = key[key_index];
            key_index = (key_index == 1) ? 7 : key_index - 1;

            if ((bkey & 3) == 0)
                byte = (byte - bkey) - keyr;
            else if ((bkey & 3) == 1)
                byte = (byte + bkey) + keyr;
            else
                byte = (byte ^ bkey) ^ keyr;
        }
        decoded[x] = static_cast<char>(byte & 0xFF);
    }

    return true;
}
