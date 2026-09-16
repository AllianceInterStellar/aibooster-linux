// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSocketNotifier>

#include <csignal>
#include <cstdlib>
#include <unistd.h>

#include "models/connectionmodel.h"
#include "models/logsmodel.h"
#include "models/profilelistmodel.h"
#include "models/proxylistmodel.h"
#include "models/settingsmodel.h"
#include "platform/SystemProxy.h"
#include "services/AccountManager.h"

namespace {

/// Self-pipe so SIGINT/SIGTERM reach the Qt event loop. Only async-signal-safe calls may
/// happen inside the handler itself, which rules out doing the proxy restore there.
int g_signalPipe[2] = {-1, -1};

void forwardSignal(int)
{
    const char byte = 1;
    // Deliberately unchecked: there is nothing useful to do if this fails, and a signal
    // handler may not report errors.
    ssize_t ignored = ::write(g_signalPipe[1], &byte, 1);
    Q_UNUSED(ignored)
}

/// Quit cleanly on Ctrl-C and on `systemctl`/session-manager shutdown, so the destructors
/// that put the system proxy back actually run. Without this the process is torn down with
/// the desktop still redirected at a port that is about to disappear.
void installSignalHandling(QGuiApplication *app)
{
    if (::pipe(g_signalPipe) != 0)
        return;

    auto *notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, app);
    QObject::connect(notifier, &QSocketNotifier::activated, app, [app](QSocketDescriptor fd) {
        char byte;
        ssize_t ignored = ::read(static_cast<int>(fd), &byte, 1);
        Q_UNUSED(ignored)
        app->quit();
    });

    struct sigaction action {};
    action.sa_handler = forwardSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGTERM, &action, nullptr);
    ::sigaction(SIGHUP, &action, nullptr);
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AiBooster"));
    app.setOrganizationName(QStringLiteral("AiBooster"));
    app.setApplicationVersion(QStringLiteral(AIBOOSTER_VERSION));
    // Lets the shell match the window to packaging/aibooster.desktop, which is what puts a
    // real name and icon in the task switcher instead of a generic Wayland placeholder.
    app.setDesktopFileName(QStringLiteral("io.allianceinterstellar.AiBooster"));
    app.setWindowIcon(QIcon(QStringLiteral(":/AiBooster/packaging/aibooster.svg")));

    installSignalHandling(&app);

    // Before anything can connect: if a previous run was killed while the desktop proxy was
    // redirected at our engine, put it back now. Otherwise the user's machine stays pointed
    // at a dead port and nothing on it can reach the network. See SystemProxy.h.
    if (SystemProxy::recoverFromPreviousRun())
        qInfo("Restored system proxy settings left behind by a previous run");

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;

    auto *account = new AccountManager(&engine);
    auto *conn = new ConnectionModel(&engine);
    auto *proxies = new ProxyListModel(&engine);
    auto *profiles = new ProfileListModel(&engine);
    auto *logs = new LogsModel(&engine);
    auto *settings = new SettingsModel(&engine);

    qmlRegisterSingletonInstance("AiBooster.Models", 1, 0, "AccountManager", account);
    qmlRegisterSingletonInstance("AiBooster.Models", 1, 0, "ConnectionModel", conn);
    qmlRegisterSingletonInstance("AiBooster.Models", 1, 0, "ProxyListModel", proxies);
    qmlRegisterSingletonInstance("AiBooster.Models", 1, 0, "ProfileListModel", profiles);
    qmlRegisterSingletonInstance("AiBooster.Models", 1, 0, "LogsModel", logs);
    qmlRegisterSingletonInstance("AiBooster.Models", 1, 0, "SettingsModel", settings);

    const QUrl url(QStringLiteral("qrc:/AiBooster/qml/Main.qml"));
    // objectCreated + null check rather than objectCreationFailed, which is Qt 6.4+ and
    // would lock out Ubuntu 22.04 (Qt 6.2), supported until 2027. Same effect: a QML failure
    // exits non-zero, which is what CI's start-up check keys on.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *object, const QUrl &objectUrl) {
            if (!object && url == objectUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.load(url);

    const int rc = app.exec();

    // Belt and braces: the ConnectionModel/VpnCore destructors revert too, but an early
    // return path that skips them must not leave the desktop redirected.
    SystemProxy::instance().revert();
    return rc;
}
