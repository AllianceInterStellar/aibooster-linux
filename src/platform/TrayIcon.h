// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AIBOOSTER_TRAYICON_H
#define AIBOOSTER_TRAYICON_H

#include <QObject>
#include <QPointer>

class ConnectionModel;
class QAction;
class QMenu;
class QQmlApplicationEngine;
class QSystemTrayIcon;
class QWindow;

/// The status icon in the desktop's tray: connection state at a glance, connect and
/// disconnect without opening the window, and somewhere for the app to live while the
/// window is closed — a VPN client that quits when its window closes drops the tunnel.
///
/// Uses QSystemTrayIcon, which speaks StatusNotifierItem over D-Bus (KDE, and GNOME with
/// the AppIndicator extension) and falls back to the XEmbed tray. Where neither exists,
/// available() is false and closing the window quits, as before.
class TrayIcon : public QObject
{
    Q_OBJECT

    /// Whether this desktop has a tray to live in. Decided once, at start-up.
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    TrayIcon(ConnectionModel *connection, QQmlApplicationEngine *engine,
             QObject *parent = nullptr);
    ~TrayIcon() override;

    bool available() const { return m_tray != nullptr; }

    /// Brings the window back (from the tray, or from behind other windows).
    Q_INVOKABLE void showWindow();

public slots:
    /// Re-reads every user-visible string, after the UI language changes.
    void retranslate();

private:
    QWindow *window() const;
    void toggleWindow();
    void updateState();
    void notifyIfHidden(const QString &title, const QString &message);

    QPointer<ConnectionModel> m_connection;
    QQmlApplicationEngine *m_engine;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_statusAction = nullptr;
    QAction *m_connectAction = nullptr;
    QAction *m_showAction = nullptr;
    QAction *m_quitAction = nullptr;
};

#endif // AIBOOSTER_TRAYICON_H
