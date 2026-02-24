#ifndef GUNCON3MANAGER_H
#define GUNCON3MANAGER_H

#include <QObject>
#include <QMap>
#include <QList>
#include <QString>
#include "guncon3.h"

/*
 * GunCon3Manager
 *
 * Detecta todas las GunCon3 conectadas y les asigna un índice de jugador
 * (0 = P1, 1 = P2, ...) de forma persistente basándose en el puerto USB
 * físico donde están conectadas.
 *
 * Lógica de asignación:
 *   1. Al conectar una pistola, se obtiene su "port path" (ruta física en el
 *      árbol de hubs USB, ej: "USB\ROOT_HUB30#4&...&0#PORT_1").
 *      Esto identifica el PUERTO FÍSICO, no el dispositivo.
 *
 *   2. Se busca ese port path en gunassign.ini:
 *      - Si existe → se restaura el índice guardado.
 *      - Si no existe → se asigna el siguiente índice libre y se guarda.
 *
 *   3. El usuario puede reasignar manualmente desde el menú de la bandeja.
 *      La nueva asignación se persiste en gunassign.ini.
 *
 * Esto garantiza que si P1 siempre se conecta en el puerto USB izquierdo
 * y P2 en el derecho, el orden es siempre correcto independientemente de
 * en qué orden se conecten o cuántas veces se reinicie la app.
 */
class GunCon3Manager : public QObject
{
    Q_OBJECT
public:
    explicit GunCon3Manager(QObject *parent = nullptr);
    ~GunCon3Manager();

    // Escanea dispositivos conectados y gestiona altas/bajas
    void scan();

    // Reasigna manualmente el índice de jugador de una pistola.
    // Si otro dispositivo tenía ese índice, se intercambian.
    void reassignPlayerIndex(const QString &devicePath, int newIndex);

    // Devuelve el índice de jugador de una pistola (-1 si no está registrada)
    int playerIndex(const QString &devicePath) const;

    // Devuelve las pistolas activas
    const QMap<QString, GunCon3*>& guns() const { return m_guns; }

signals:
    // pistola nueva operativa: incluye su índice de jugador asignado
    void gunConnected(const QString &devicePath, GunCon3 *gun, int playerIndex);
    void gunDisconnected(const QString &devicePath, int playerIndex);
    // el índice de jugador de una pistola cambió (reasignación manual)
    void playerIndexChanged(const QString &devicePath, int newIndex);

private:
    // Información sobre un dispositivo conectado
    struct GunInfo {
        GunCon3 *gun       { nullptr };
        QString  portPath;   // ruta física del puerto USB
        int      playerIdx { -1 };
    };

    void addGun(const QString &devicePath, const QString &portPath);
    void removeGun(const QString &devicePath);

    // Devuelve {devicePath, portPath} de todas las GunCon3 presentes
    static QList<QPair<QString,QString>> enumGunCons();

    // Obtiene la ruta física del puerto a partir del devicePath WinUSB
    static QString getPortPath(const QString &devicePath);

    // Persistencia de asignaciones puerto→índice
    int  loadAssignment(const QString &portPath) const;
    void saveAssignment(const QString &portPath, int index);
    int  nextFreeIndex() const;

    QMap<QString, GunInfo>  m_infos;   // devicePath → GunInfo
    QMap<QString, GunCon3*> m_guns;    // devicePath → GunCon3* (para compatibilidad)

    class WatcherWindow;
    WatcherWindow *m_watcher { nullptr };

    static constexpr const char* kSettingsFile = "gunassign.ini";
};

#endif // GUNCON3MANAGER_H
