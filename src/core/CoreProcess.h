// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AIBOOSTER_COREPROCESS_H
#define AIBOOSTER_COREPROCESS_H

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

/// Supervises the VPN engine as a **separate process**.
///
/// The engine (hiddify-core / sing-box) is GPL-3.0 licensed and is NOT part of this
/// repository. This client never links it, dlopen()s it, or embeds it: it spawns the
/// engine binary and talks to it exclusively over loopback sockets (the Clash API and
/// the mixed inbound). That boundary is deliberate — it keeps the two programs separate
/// works, and it means a wedged engine can be killed without taking the UI with it.
///
/// Consequently the engine binary is a *runtime dependency* the user supplies. See
/// locateBinary() for where we look and README.md for how to install it.
class CoreProcess : public QObject
{
    Q_OBJECT

public:
    enum State {
        Stopped,
        Starting,
        Running,
        Stopping,
        Failed,
    };
    Q_ENUM(State)

    explicit CoreProcess(QObject *parent = nullptr);
    ~CoreProcess() override;

    /// Absolute path of the engine binary, or an empty string when it cannot be found.
    /// Search order (first hit wins):
    ///   1. $AIBOOSTER_CORE                       — explicit override, used by CI and packagers
    ///   2. <app dir>/aibooster-core              — portable / AppImage layout
    ///   3. <app dir>/../libexec/aibooster/…      — a prefix install from this source tree
    ///   4. /usr/libexec/aibooster/…, /usr/lib/…  — distro package layout
    ///   5. $PATH
    static QString locateBinary();

    /// Human-readable list of the places searched, for the "engine not found" message.
    static QStringList searchedLocations();

    State state() const { return m_state; }
    QString lastError() const { return m_lastError; }

    /// Spawns the engine with `run -c <configPath> -d <settingsPath>`.
    ///
    /// Returns as soon as the child is spawned; readiness is asynchronous and reported by
    /// ready(). "The process is alive" is NOT readiness — the engine binds its gRPC port
    /// before it has even read the config, so a config it rejects would still look healthy.
    ///
    /// Readiness is therefore the PROXY INBOUND being open: that is the port traffic
    /// actually goes through, and the engine binds it only after accepting the config and
    /// starting the inbound. Measured against a real engine build: a rejected config binds
    /// nothing, an accepted one has the proxy port up within a couple of seconds and
    /// carries traffic immediately.
    ///
    /// Deliberately NOT the Clash API port. That listener blocks on downloading its
    /// external web UI and never came up at all in 50 s of observation, so waiting on it
    /// means never reporting a connection that is in fact working.
    void start(const QString &configPath, const QString &settingsPath, quint16 proxyPort);

    /// Asks the engine to exit (SIGTERM), escalating to SIGKILL if it ignores that.
    void stop();

signals:
    void ready();
    void stoppedCleanly();
    void failed(const QString &error);
    void logLine(const QString &line);
    void stateChanged(State state);

private:
    void setState(State state);
    void pollReadiness();
    void drainOutput();
    void handleFinished(int exitCode, QProcess::ExitStatus status);

    QProcess m_process;
    QTimer m_readyTimer;
    QString m_lastError;
    QString m_stderrTail;
    State m_state = Stopped;
    quint16 m_proxyPort = 0;
    int m_readyAttempts = 0;
};

#endif // AIBOOSTER_COREPROCESS_H
