// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrayIcon.h"

#include "../models/connectionmodel.h"

#include <QAction>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QSystemTrayIcon>
#include <QWindow>

TrayIcon::TrayIcon(ConnectionModel *connection, QQmlApplicationEngine *engine, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
    , m_engine(engine)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_menu = new QMenu;
    m_statusAction = m_menu->addAction(QString());
    m_statusAction->setEnabled(false);
    m_menu->addSeparator();
    m_connectAction = m_menu->addAction(QString());
    connect(m_connectAction, &QAction::triggered, this, [this]() {
        if (m_connection)
            m_connection->toggleConnection();
    });
    m_showAction = m_menu->addAction(QString());
    connect(m_showAction, &QAction::triggered, this, &TrayIcon::toggleWindow);
    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(QString());
    // quit() unwinds main() normally, so the disconnect and proxy restore still run.
    connect(m_quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_tray = new QSystemTrayIcon(this);
    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger)
                    toggleWindow();
            });
    // The menu is rebuilt each time it opens, so "Show"/"Hide" matches the window as it is
    // now — it can be hidden or shown from outside (the window manager, the close button).
    connect(m_menu, &QMenu::aboutToShow, this, &TrayIcon::updateState);

    if (m_connection) {
        connect(m_connection, &ConnectionModel::statusChanged, this, &TrayIcon::updateState);
        connect(m_connection, &ConnectionModel::statsChanged, this, &TrayIcon::updateState);
        connect(m_connection, &ConnectionModel::reconnectingChanged, this, &TrayIcon::updateState);
        connect(m_connection, &ConnectionModel::connectionFailed, this, [this](const QString &error) {
            notifyIfHidden(tr("AI Booster"),
                           m_connection && m_connection->reconnecting()
                               ? tr("Connection lost. Reconnecting…")
                               : tr("Could not connect: %1").arg(error));
        });
    }

    retranslate();
    m_tray->show();
}

TrayIcon::~TrayIcon()
{
    // The menu has no QObject parent (QSystemTrayIcon does not own its context menu).
    delete m_menu;
}

QWindow *TrayIcon::window() const
{
    if (!m_engine)
        return nullptr;
    const QList<QObject *> roots = m_engine->rootObjects();
    return roots.isEmpty() ? nullptr : qobject_cast<QWindow *>(roots.first());
}

void TrayIcon::showWindow()
{
    QWindow *w = window();
    if (!w)
        return;
    w->show();
    if (w->windowStates() & Qt::WindowMinimized)
        w->setWindowStates(w->windowStates() & ~Qt::WindowMinimized);
    w->raise();
    w->requestActivate();
}

void TrayIcon::toggleWindow()
{
    QWindow *w = window();
    if (!w)
        return;
    if (w->isVisible() && !(w->windowStates() & Qt::WindowMinimized))
        w->hide();
    else
        showWindow();
}

void TrayIcon::retranslate()
{
    if (!m_tray)
        return;
    m_quitAction->setText(tr("Quit"));
    updateState();
}

void TrayIcon::updateState()
{
    if (!m_tray || !m_connection)
        return;

    QString status;
    QString iconPath;
    switch (m_connection->status()) {
    case ConnectionModel::Connected:
        status = m_connection->activeProxyName().isEmpty()
            ? tr("Connected")
            : tr("Connected — %1").arg(m_connection->activeProxyName());
        iconPath = QStringLiteral(":/AiBooster/qml/icons/tray-on.svg");
        m_connectAction->setText(tr("Disconnect"));
        break;
    case ConnectionModel::Connecting:
        status = tr("Connecting…");
        iconPath = QStringLiteral(":/AiBooster/qml/icons/tray-connecting.svg");
        m_connectAction->setText(tr("Cancel"));
        break;
    case ConnectionModel::Disconnecting:
        status = tr("Disconnecting…");
        iconPath = QStringLiteral(":/AiBooster/qml/icons/tray-connecting.svg");
        m_connectAction->setText(tr("Disconnect"));
        break;
    case ConnectionModel::Disconnected:
        status = m_connection->reconnecting() ? tr("Connection lost — reconnecting")
                                              : tr("Not connected");
        iconPath = m_connection->reconnecting()
            ? QStringLiteral(":/AiBooster/qml/icons/tray-connecting.svg")
            : QStringLiteral(":/AiBooster/qml/icons/tray-off.svg");
        m_connectAction->setText(tr("Connect"));
        break;
    }
    m_connectAction->setEnabled(m_connection->status() != ConnectionModel::Disconnecting);
    m_statusAction->setText(status);

    const QWindow *w = window();
    const bool shown = w && w->isVisible() && !(w->windowStates() & Qt::WindowMinimized);
    m_showAction->setText(shown ? tr("Hide Window") : tr("Show Window"));

    m_tray->setIcon(QIcon(iconPath));
    m_tray->setToolTip(tr("AI Booster — %1").arg(status));
}

void TrayIcon::notifyIfHidden(const QString &title, const QString &message)
{
    // With the window on screen the banner already says it; a notification on top is noise.
    const QWindow *w = window();
    if (!m_tray || (w && w->isVisible() && !(w->windowStates() & Qt::WindowMinimized)))
        return;
    m_tray->showMessage(title, message, QSystemTrayIcon::Warning);
}
