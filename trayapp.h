#ifndef TRAYAPP_H
#define TRAYAPP_H

#include <QObject>
#include <QMap>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QSettings>
#include "guncon3manager.h"
#include "virtualmouse.h"
#include "calibrador.h"

class TrayApp : public QObject
{
    Q_OBJECT
public:
    explicit TrayApp(QObject *parent = nullptr);
    ~TrayApp();

private slots:
    void onGunConnected(const QString &path, GunCon3 *gun, int playerIdx);
    void onGunDisconnected(const QString &path, int playerIdx);
    void onPlayerIndexChanged(const QString &path, int newIndex);
    void calibrarPistola(const QString &path);
    void reassignGun(const QString &path, int newIndex);

private:
    void buildTrayMenu();
    void updateTrayMenu();
    void saveCalibration(const QString &path, const VirtualMouse::CalibData &d);
    void loadCalibration(const QString &path, VirtualMouse *mouse);

    struct GunEntry {
        GunCon3      *gun        { nullptr };
        VirtualMouse *mouse      { nullptr };
        int           playerIdx  { -1 };
        QMenu        *subMenu    { nullptr };
        QPoint        lastScreen { 16383, 16383 };  // última posición válida (centro)
    };

    GunCon3Manager         *m_manager       { nullptr };
    QMap<QString, GunEntry> m_entries;
    QSystemTrayIcon        *m_tray          { nullptr };
    QMenu                  *m_menu          { nullptr };
    QAction                *m_noGunsAction  { nullptr };
    QAction                *m_quitAction    { nullptr };
};

#endif // TRAYAPP_H
