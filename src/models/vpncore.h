// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef VPNCORE_H
#define VPNCORE_H

#include <QObject>
#include <QString>

class CoreProcess;

/// Drives a connection end to end: fetch the subscription, validate it, write the files
/// the engine reads, supervise the engine process, and point the desktop's proxy at it.
///
/// The engine runs as a SEPARATE PROCESS (see CoreProcess). Earlier desktop builds of this
/// client dlopen()ed the engine and called its C ABI in-process; that is gone. Besides the
/// licensing boundary it buys, out-of-process means a wedged or crashing engine surfaces as
/// a clean error here instead of taking the user interface down with it.
class VpnCore : public QObject
{
    Q_OBJECT

public:
    enum CoreStatus {
        Idle,
        Starting,
        Running,
        Stopping,
        Error,
    };
    Q_ENUM(CoreStatus)

    explicit VpnCore(QObject *parent = nullptr);
    ~VpnCore() override;

    /// Connect from config content already in hand: an active profile's stored payload, or
    /// the free nodes' subscription body.
    void connectVpnWithConfig(const QString &configContent);

    /// Stops a running engine, or abandons one that is still starting. Returns at once;
    /// disconnected() follows when the engine has gone. Returns false when there was
    /// nothing to stop, in which case no signal follows.
    bool disconnectVpn();

    /// Where the engine's per-run files live. The ONLY definition of these paths — the proxy
    /// list reads the running config back from here, and a second hardcoded copy silently
    /// breaks that reader the day the directory is renamed. Go through these instead of
    /// rebuilding the path.
    static QString engineDir();
    static QString runningConfigPath();

    CoreStatus coreStatus() const { return m_coreStatus; }
    QString lastError() const { return m_lastError; }

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);
    void statusMessage(const QString &message);
    /// One line of the engine's own output. Client-side progress goes to statusMessage().
    void engineLogLine(const QString &line);

private:
    void launchEngine(const QString &configContent);
    void handleEngineReady();
    void handleEngineFailure(const QString &error);
    void markConnected();
    void finishStopping();
    void setCoreStatus(CoreStatus status);
    void fail(const QString &message);


    CoreProcess *m_core;

    CoreStatus m_coreStatus = Idle;
    QString m_lastError;
    quint16 m_mixedPort = 0;
    /// Whether the user asked for the desktop proxy to be redirected. Captured when the
    /// engine is launched and consulted once it reports ready.
    bool m_wantSystemProxy = false;
};

#endif // VPNCORE_H
