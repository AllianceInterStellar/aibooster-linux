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
    /// ready(). "The process is alive" is NOT readiness — the engine parses the config and
    /// binds its listeners after start-up, and a config it rejects makes it exit moments
    /// later. We therefore poll the Clash API port and only then call it running.
    void start(const QString &configPath, const QString &settingsPath, quint16 clashApiPort);

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
    quint16 m_clashApiPort = 0;
    int m_readyAttempts = 0;
};

#endif // AIBOOSTER_COREPROCESS_H
