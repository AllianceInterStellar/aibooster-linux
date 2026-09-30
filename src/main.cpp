// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSocketNotifier>

#include <csignal>
#include <fcntl.h>
#include <cstdlib>
#include <unistd.h>

#include "models/connectionmodel.h"
#include "models/logsmodel.h"
#include "models/profilelistmodel.h"
#include "models/proxylistmodel.h"
#include "models/settingsmodel.h"
#include "platform/SystemProxy.h"
#include "platform/TrayIcon.h"

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
void installSignalHandling(QCoreApplication *app)
{
    // O_CLOEXEC: without it every child — the engine included — inherits both ends of this
    // pipe for its whole lifetime. O_NONBLOCK: a burst of signals that fills the pipe must
    // drop bytes, not block inside the handler.
    if (::pipe2(g_signalPipe, O_CLOEXEC | O_NONBLOCK) != 0)
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

/// Installs the translations for `code` ("" = the system's language), replacing any that
/// were installed before. English is the source language, so for it nothing is loaded.
void applyLanguage(const QString &code, QTranslator *app, QTranslator *qt)
{
    QCoreApplication::removeTranslator(app);
    QCoreApplication::removeTranslator(qt);
    const QLocale locale = code.isEmpty() ? QLocale::system() : QLocale(code);
    // Compiled in by qt_add_translations; see CMakeLists.txt.
    if (app->load(locale, QStringLiteral("aibooster"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(app);
    // Qt's own strings (standard context menus, dialogs), when the system has them.
    if (qt->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                 QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(qt);
}

} // namespace

int main(int argc, char *argv[])
{
    // QApplication rather than QGuiApplication only for the tray icon, which Qt implements
    // in Widgets. The UI itself is all QML.
    QApplication app(argc, argv);
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

    auto *conn = new ConnectionModel(&engine);
    auto *proxies = new ProxyListModel(&engine);
    auto *profiles = new ProfileListModel(&engine);
    auto *logs = new LogsModel(&engine);
    auto *settings = new SettingsModel(&engine);
    QObject::connect(conn, &ConnectionModel::logLine, logs, &LogsModel::appendLine);

    qmlRegisterSingletonInstance("AiBooster.Models", 1, 0, "ConnectionModel", conn);
    qmlRegisterSingletonInstance("AiBooster.Models", 1, 0, "ProxyListModel", proxies);
    qmlRegisterSingletonInstance("AiBooster.Models", 1, 0, "ProfileListModel", profiles);
    qmlRegisterSingletonInstance("AiBooster.Models", 1, 0, "LogsModel", logs);
    qmlRegisterSingletonInstance("AiBooster.Models", 1, 0, "SettingsModel", settings);

    // Before anything is loaded, so the first frame is already in the right language. Then
    // live: the setting takes effect without a restart.
    QTranslator appTranslator;
    QTranslator qtTranslator;
    QString appliedLanguage = settings->language();
    applyLanguage(appliedLanguage, &appTranslator, &qtTranslator);

    auto *tray = new TrayIcon(conn, &engine, &engine);
    QObject::connect(settings, &SettingsModel::changed, &engine, [&]() {
        if (settings->language() == appliedLanguage)
            return;
        appliedLanguage = settings->language();
        applyLanguage(appliedLanguage, &appTranslator, &qtTranslator);
        engine.retranslate();
        tray->retranslate();
    });
    qmlRegisterSingletonInstance("AiBooster.Models", 1, 0, "Tray", tray);
    // With a tray, closing the window can mean "keep running"; Main.qml decides, and quits
    // explicitly when it does not. Without one, closing the window quits as it always did.
    if (tray->available())
        app.setQuitOnLastWindowClosed(false);

    // The real Qt this binary is linked against. The About panel used to print a hardcoded
    // "6.x", which stayed right by being too vague to be wrong.
    engine.rootContext()->setContextProperty(QStringLiteral("qtRuntimeVersion"),
                                             QString::fromLatin1(qVersion()));

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
