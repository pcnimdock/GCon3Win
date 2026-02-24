#include "trayapp.h"
#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QRegularExpression>

static QString playerLabel(int idx)
{
    return (idx >= 0) ? QString("P%1").arg(idx + 1) : "Sin asignar";
}

static QString gunMenuLabel(int playerIdx)
{
    return QString("GunCon3 [%1]").arg(playerLabel(playerIdx));
}

static QString settingsKey(const QString &path)
{
    QString k = path;
    k.replace(QRegularExpression("[^a-zA-Z0-9]"), "_");
    return k;
}

// ─────────────────────────────────────────────────────────────────────────────
TrayApp::TrayApp(QObject *parent) : QObject(parent)
{
    m_tray = new QSystemTrayIcon(QIcon(":/icono.ico"), this);
    m_tray->setToolTip("GunCon3Win");
    buildTrayMenu();
    m_tray->show();

    m_manager = new GunCon3Manager(this);
    connect(m_manager, &GunCon3Manager::gunConnected,    this, &TrayApp::onGunConnected);
    connect(m_manager, &GunCon3Manager::gunDisconnected, this, &TrayApp::onGunDisconnected);
    connect(m_manager, &GunCon3Manager::playerIndexChanged, this, &TrayApp::onPlayerIndexChanged);

    m_manager->scan();

    if (m_entries.isEmpty())
        m_tray->showMessage("GunCon3Win",
            "No se encontro ninguna GunCon3.\nConectala y se detectara automaticamente.",
            QSystemTrayIcon::Information, 3000);
}

TrayApp::~TrayApp()
{
    for (auto &e : m_entries) delete e.mouse;
}

// ─────────────────────────────────────────────────────────────────────────────
void TrayApp::onGunConnected(const QString &path, GunCon3 *gun, int playerIdx)
{
    GunEntry entry;
    entry.gun       = gun;
    entry.playerIdx = playerIdx;
    entry.mouse     = new VirtualMouse(this);

    if (!entry.mouse->initialize())
        qWarning() << "[TrayApp] VirtualMouse no disponible para P" << (playerIdx+1);

    // Cargar calibración guardada
    loadCalibration(path, entry.mouse);

    // Conectar datos de la pistola → ratón virtual
    // La señal ahora incluye z y status
    connect(gun, &GunCon3::gunDataReceived, this,
        [this, path](QPoint rawCam, int z, GunCon3::GunStatus status,
                     QPoint, QPoint, quint32 buttons)
    {
        auto it = m_entries.find(path);
        if (it == m_entries.end() || !it->mouse || !it->mouse->isAvailable()) return;

        QPoint screen = it->mouse->applyCalibration(rawCam, z, status);

        // applyCalibration devuelve (-1,-1) cuando status == OUT_OF_RANGE:
        // congelar posición en la última válida pero seguir enviando botones
        // (permite recargar apuntando fuera de pantalla)
        if (screen.x() < 0)
            screen = it->lastScreen;
        else
            it->lastScreen = screen;

        it->mouse->sendReport(screen, buttons);
    });

    m_entries[path] = entry;
    updateTrayMenu();

    m_tray->showMessage("GunCon3Win",
        QString("GunCon3 conectada como %1").arg(playerLabel(playerIdx)),
        QSystemTrayIcon::Information, 2000);
}

void TrayApp::onGunDisconnected(const QString &path, int playerIdx)
{
    auto it = m_entries.find(path);
    if (it == m_entries.end()) return;
    delete it->mouse;
    m_entries.erase(it);
    updateTrayMenu();
    m_tray->showMessage("GunCon3Win",
        QString("%1 desconectada").arg(playerLabel(playerIdx)),
        QSystemTrayIcon::Warning, 2000);
}

void TrayApp::onPlayerIndexChanged(const QString &path, int newIndex)
{
    auto it = m_entries.find(path);
    if (it != m_entries.end()) it->playerIdx = newIndex;
    updateTrayMenu();
}

// ─────────────────────────────────────────────────────────────────────────────
// Reasignación
// ─────────────────────────────────────────────────────────────────────────────
void TrayApp::reassignGun(const QString &path, int newIndex)
{
    int currentIndex = m_manager->playerIndex(path);
    if (currentIndex == newIndex) return;

    QString otherPath;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it)
        if (it.key() != path && it->playerIdx == newIndex) { otherPath = it.key(); break; }

    QString msg = QString("Asignar esta GunCon3 como %1?").arg(playerLabel(newIndex));
    if (!otherPath.isEmpty())
        msg += QString("\n(La GunCon3 actual en %1 pasara a ser %2)")
               .arg(playerLabel(newIndex)).arg(playerLabel(currentIndex));

    if (QMessageBox::question(nullptr, "Reasignar jugador", msg,
                              QMessageBox::Yes|QMessageBox::No) != QMessageBox::Yes) return;

    m_manager->reassignPlayerIndex(path, newIndex);
}

// ─────────────────────────────────────────────────────────────────────────────
// Calibración
// ─────────────────────────────────────────────────────────────────────────────
void TrayApp::calibrarPistola(const QString &path)
{
    auto it = m_entries.find(path);
    if (it == m_entries.end()) return;

    GunCon3      *gun   = it->gun;
    VirtualMouse *mouse = it->mouse;

    Calibrador *c = new Calibrador();
    c->setAttribute(Qt::WA_DeleteOnClose);

    // Alimentar el calibrador con datos crudos (cam + z + status)
    connect(gun, &GunCon3::gunDataReceived, c,
        [c](QPoint cam, int z, GunCon3::GunStatus status,
            QPoint, QPoint, quint32 buttons) {
            c->setAim(cam, z, status, buttons);
        });

    // Cuando termina, pasar los puntos al VirtualMouse
    connect(c, &Calibrador::calibrationPoints, this,
        [this, path, mouse](QVector<QPoint> cam,
                            QVector<QPoint> screen,
                            QVector<int>    zValues)
    {
        if (!mouse) return;

        mouse->clearCalibrationPoints();
        for (int i = 0; i < cam.size(); ++i)
            mouse->addCalibrationPoint(cam[i], screen[i], zValues[i]);

        if (mouse->commitCalibration())
            saveCalibration(path, mouse->calibrationData());
        else
            QMessageBox::warning(nullptr, "Calibracion",
                "No se pudo calcular la calibracion.\n"
                "Asegurate de mantener la pistola estable al disparar.");
    });

    c->show();
}

// ─────────────────────────────────────────────────────────────────────────────
// Persistencia de calibración
// ─────────────────────────────────────────────────────────────────────────────
void TrayApp::saveCalibration(const QString &path, const VirtualMouse::CalibData &d)
{
    if (!d.valid) return;
    QSettings s("calibracion.ini", QSettings::IniFormat);
    QString k = "calib/" + settingsKey(path);
    s.setValue(k + "/cx",   d.cx);
    s.setValue(k + "/cy",   d.cy);
    s.setValue(k + "/zRef", d.zRef);
    for (int i = 0; i < 9; ++i)
        s.setValue(k + QString("/H%1").arg(i), d.H[i]);
    qDebug() << "[TrayApp] Calibracion guardada para:" << path.left(40);
}

void TrayApp::loadCalibration(const QString &path, VirtualMouse *mouse)
{
    if (!mouse) return;
    QSettings s("calibracion.ini", QSettings::IniFormat);
    QString k = "calib/" + settingsKey(path);

    if (!s.contains(k + "/cx")) return;  // sin calibración guardada

    VirtualMouse::CalibData d;
    d.cx   = s.value(k + "/cx").toDouble();
    d.cy   = s.value(k + "/cy").toDouble();
    d.zRef = s.value(k + "/zRef").toDouble();
    for (int i = 0; i < 9; ++i)
        d.H[i] = s.value(k + QString("/H%1").arg(i)).toDouble();
    d.valid = (d.zRef > 0);

    if (d.valid) {
        mouse->setCalibrationData(d);
        qDebug() << "[TrayApp] Calibracion cargada para:" << path.left(40)
                 << "zRef=" << d.zRef;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú
// ─────────────────────────────────────────────────────────────────────────────
void TrayApp::buildTrayMenu()
{
    m_menu = new QMenu();
    m_noGunsAction = m_menu->addAction("Sin pistolas conectadas");
    m_noGunsAction->setEnabled(false);
    m_menu->addSeparator();
    m_quitAction = m_menu->addAction("Salir", qApp, &QCoreApplication::quit);
    m_tray->setContextMenu(m_menu);
}

void TrayApp::updateTrayMenu()
{
    for (QAction *a : m_menu->actions())
        if (a != m_noGunsAction && a != m_quitAction && !a->isSeparator())
            { m_menu->removeAction(a); delete a; }

    if (m_entries.isEmpty()) {
        m_noGunsAction->setVisible(true);
    } else {
        m_noGunsAction->setVisible(false);

        QList<QString> paths = m_entries.keys();
        std::sort(paths.begin(), paths.end(), [this](const QString &a, const QString &b) {
            return m_entries[a].playerIdx < m_entries[b].playerIdx;
        });

        for (const QString &path : paths) {
            GunEntry &e = m_entries[path];
            QMenu *sub = m_menu->addMenu(gunMenuLabel(e.playerIdx));
            e.subMenu  = sub;

            bool mouseOk = e.mouse && e.mouse->isAvailable();
            bool calibOk = e.mouse && e.mouse->hasCalibration();

            QAction *s1 = sub->addAction(mouseOk ? "HID: activo" : "HID: no disponible");
            s1->setEnabled(false);
            QAction *s2 = sub->addAction(calibOk ? "Calibracion: OK" : "Calibracion: pendiente");
            s2->setEnabled(false);

            sub->addSeparator();
            sub->addAction("Calibrar", [this, path]() { calibrarPistola(path); });

            sub->addSeparator();
            QMenu *rm = sub->addMenu("Asignar como...");
            int maxIdx = (int)m_entries.size();
            for (int i = 0; i <= maxIdx; ++i) {
                if (i == e.playerIdx) continue;
                bool busy = false;
                for (auto &oe : m_entries) if (oe.playerIdx == i) { busy = true; break; }
                QString lbl = playerLabel(i) + (busy ? " (intercambiar)" : "");
                rm->addAction(lbl, [this, path, i]() { reassignGun(path, i); });
            }
        }
    }

    m_menu->removeAction(m_quitAction);
    m_menu->addSeparator();
    m_menu->addAction(m_quitAction);
}
