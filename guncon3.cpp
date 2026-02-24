#include "guncon3.h"
#include <QDebug>
#include <initguid.h>
#include <windows.h>
#include <winusb.h>
#include <setupapi.h>
#include <Usbiodef.h>

#define GUNCON3_VID  0x0b9a
#define GUNCON3_PID  0x0800
#define ENDPOINT_IN  0x82
#define ENDPOINT_OUT 0x02

GunCon3::GunCon3(const QString &devicePath, QObject *parent)
    : QObject(parent), m_devicePath(devicePath)
{}

GunCon3::~GunCon3() { close(); }

bool GunCon3::open()
{
    std::wstring wpath = m_devicePath.toStdWString();
    deviceHandle = CreateFileW(wpath.c_str(),
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                               nullptr);
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        qWarning() << "[GunCon3] No se pudo abrir:" << m_devicePath;
        return false;
    }
    if (!WinUsb_Initialize(deviceHandle, &usbHandle)) {
        CloseHandle(deviceHandle); deviceHandle = INVALID_HANDLE_VALUE;
        return false;
    }
    if (!sendKey()) {
        WinUsb_Free(usbHandle);    usbHandle = nullptr;
        CloseHandle(deviceHandle); deviceHandle = INVALID_HANDLE_VALUE;
        return false;
    }
    running = true;
    readerThread = std::thread(&GunCon3::readLoop, this);
    return true;
}

void GunCon3::close()
{
    running = false;
    if (readerThread.joinable()) readerThread.join();
    if (usbHandle)   { WinUsb_Free(usbHandle); usbHandle = nullptr; }
    if (deviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(deviceHandle); deviceHandle = INVALID_HANDLE_VALUE;
    }
}

bool GunCon3::isConnected() const
{
    if (!usbHandle) return false;
    USB_DEVICE_DESCRIPTOR desc; ULONG len = 0;
    return WinUsb_GetDescriptor(usbHandle, USB_DEVICE_DESCRIPTOR_TYPE,
                                0, 0, reinterpret_cast<PUCHAR>(&desc),
                                sizeof(desc), &len)
           && desc.idVendor == GUNCON3_VID && desc.idProduct == GUNCON3_PID;
}

bool GunCon3::sendKey()
{
    ULONG bw = 0;
    return WinUsb_WritePipe(usbHandle, ENDPOINT_OUT,
                            const_cast<PUCHAR>(key), sizeof(key), &bw, nullptr)
           && bw == sizeof(key);
}

void GunCon3::readLoop()
{
    UCHAR buffer[64];
    ULONG transferred = 0;

    while (running) {
        BOOL ok = WinUsb_ReadPipe(usbHandle, ENDPOINT_IN,
                                  buffer, 15, &transferred, nullptr);
        if (!ok || transferred != 15) {
            if (!isConnected()) {
                running = false;
                QMetaObject::invokeMethod(this, [this]() { emit disconnected(); },
                                          Qt::QueuedConnection);
                break;
            }
            continue;
        }

        QByteArray raw(reinterpret_cast<char*>(buffer), transferred);
        QByteArray decoded(15, 0);
        if (!decode(raw, decoded)) continue;

        const uchar* d = reinterpret_cast<const uchar*>(decoded.constData());

        // ── Posición del cañón (coordenadas de cámara) ────────────────────
        int16_t x = (d[4] << 8) | d[3];
        int16_t y = -((d[6] << 8) | d[5]); // invertir Y: cámara vs pantalla

        // ── z: separación en píxeles de los dos LEDs en la imagen ─────────
        // d[7] = byte bajo, d[8] = byte alto (uint16, siempre positivo)
        // Valor 0 significa "pistola fuera de pantalla / LEDs no detectados"
        uint16_t z = (static_cast<uint16_t>(d[8]) << 8) | d[7];

        // ── Analógicos (0-255, centrado en 128) ───────────────────────────
        int16_t ax = 255 - d[9];
        int16_t ay = 255 - d[10];
        int16_t bx = 255 - d[11];
        int16_t by = 255 - d[12];

        quint32 buttons = (d[2] << 16) | (d[1] << 8) | d[0];
        GunCon3::GunStatus status = GunCon3::statusFromButtons(buttons);

        QMetaObject::invokeMethod(this, [=]() {
            emit gunDataReceived(QPoint(x, y), z, status,
                                 QPoint(ax, ay), QPoint(bx, by), buttons);
        }, Qt::QueuedConnection);
    }
}

bool GunCon3::decode(const QByteArray &data, QByteArray &decoded)
{
    if (data.size() != 15) return false;
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

    if (a_sum != key[7]) return false;

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
            if      ((bkey & 3) == 0) byte = (byte - bkey) - keyr;
            else if ((bkey & 3) == 1) byte = (byte + bkey) + keyr;
            else                      byte = (byte ^ bkey) ^ keyr;
        }
        decoded[x] = static_cast<char>(byte & 0xFF);
    }
    return true;
}
