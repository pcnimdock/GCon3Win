#ifndef GUNCON3VJOYBRIDGE_H
#define GUNCON3VJOYBRIDGE_H

#include <QObject>
#include <QPoint>
#include <windows.h>
#include "vjoyinterface.h"

class GunCon3VJoyBridge : public QObject
{
    Q_OBJECT
public:
    explicit GunCon3VJoyBridge(QObject *parent = nullptr);
    ~GunCon3VJoyBridge();

    bool initialize(int deviceId = 1);
    void update(QPoint aim, QPoint stickA, QPoint stickB, quint32 buttons);

private:
    int vjoyId;
    bool available;
};

#endif // GUNCON3VJOYBRIDGE_H
