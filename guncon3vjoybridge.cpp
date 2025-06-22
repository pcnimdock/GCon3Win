#include "guncon3vjoybridge.h"
#include <QDebug>
//extern "C" {
#include "vjoyinterface.h"
//#include "public.h"
//#include "gen-versioninfo.h"
//}
GunCon3VJoyBridge::GunCon3VJoyBridge(QObject *parent)
    : QObject(parent), vjoyId(1), available(false)
{
}

GunCon3VJoyBridge::~GunCon3VJoyBridge()
{
    if (available) {
        RelinquishVJD(vjoyId);
    }
}

bool GunCon3VJoyBridge::initialize(int deviceId)
{
    vjoyId = deviceId;

    if (!vJoyEnabled()) {
        qWarning() << "vJoy no está habilitado.";
            return false;
    }

    VjdStat status = GetVJDStatus(vjoyId);
    if (status != VJD_STAT_FREE && status != VJD_STAT_OWN) {
        qWarning() << "vJoy device" << vjoyId << "no disponible.";
        return false;
    }

    if (!AcquireVJD(vjoyId)) {
        qWarning() << "No se pudo adquirir vJoy device" << vjoyId;
        return false;
    }

    available = true;
    return true;
}

void GunCon3VJoyBridge::update(QPoint aim, QPoint stickA, QPoint stickB, quint32 buttons)
{
    if (!available) return;

    JOYSTICK_POSITION report;
    memset(&report, 0, sizeof(JOYSTICK_POSITION));
    report.bDevice = static_cast<UCHAR>(vjoyId);

    // Mapear Aim (pistola)
    report.wAxisX = (LONG)((aim.x() + 32768) * 32767 / 65535.0);
    report.wAxisY = (LONG)((aim.y() + 32768) * 32767 / 65535.0);

    // Mapear Stick A (por ejemplo eje Z y ZRot)
    report.wAxisZ = static_cast<LONG>((stickA.x() / 255.0) * 32768);
    report.wAxisZRot = static_cast<LONG>((stickA.y() / 255.0) * 32768);

    // Mapear Stick B (por ejemplo eje XRot y YRot)
    report.wAxisXRot = static_cast<LONG>((stickB.x() / 255.0) * 32768);
    report.wAxisYRot = static_cast<LONG>((stickB.y() / 255.0) * 32768);

    // Botones
    //if (buttons & 0x00002000) report.lButtons |= 1 << 0;  // Trigger
    //if (buttons & 0x00000400) report.lButtons |= 1 << 1;  // B1
    //if (buttons & 0x00000200) report.lButtons |= 1 << 2;  // B2
    //if (buttons & 0x00000004) report.lButtons |= 1 << 3;  // A1
    //if (buttons & 0x00000002) report.lButtons |= 1 << 4;  // A2
    //if (buttons & 0x00000008) report.lButtons |= 1 << 5;  // START (C2)
    //if (buttons & 0x00008000) report.lButtons |= 1 << 6;  // SELECT (C1)
    // Botones
    if (buttons & 0x00002000) report.lButtons |= 1 << 0;  // TRIGGER
    if (buttons & 0x00000004) report.lButtons |= 1 << 1;  // A1
    if (buttons & 0x00000002) report.lButtons |= 1 << 2;  // A2
    if (buttons & 0x00800000) report.lButtons |= 1 << 3;  // A3
    if (buttons & 0x00000400) report.lButtons |= 1 << 4;  // B1
    if (buttons & 0x00000200) report.lButtons |= 1 << 5;  // B2
    if (buttons & 0x00400000) report.lButtons |= 1 << 6;  // B3
    if (buttons & 0x00008000) report.lButtons |= 1 << 7;  // C1 (SELECT)
    if (buttons & 0x00000008) report.lButtons |= 1 << 8;  // C2 (START)
    //if (buttons & 0x00000800) report.lButtons |= 1 << 9;  // OUT_OF_RANGE
    //if (buttons & 0x00000100) report.lButtons |= 1 << 10; // ONE_REF

    UpdateVJD(vjoyId, &report);
}
