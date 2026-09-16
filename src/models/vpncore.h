// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef VPNCORE_H
#define VPNCORE_H

#include <QNetworkAccessManager>
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
        Downloading,
        Starting,
        Running,
        Stopping,
        Error,
    };
    Q_ENUM(CoreStatus)

    explicit VpnCore(QObject *parent = nullptr);
    ~VpnCore() override;

    void connectVpn(const QString &subscriptionUrl);

    /// Connect from config content already in hand (an active profile's stored payload or
    /// the decrypted premium config) — same pipeline as connectVpn minus the download.
    void connectVpnWithConfig(const QString &configContent);

    void disconnectVpn();

    /// Where the engine's per-run files live. The ONLY definition of these paths — three
    /// separate call sites used to hardcode them, so renaming the directory silently broke
    /// the premium-config cleanup (a lapsed subscriber kept a working paid config) and the
    /// proxy list's config reader. Go through these instead of rebuilding the path.
    static QString engineDir();
    static QString runningConfigPath();

    /// True when the payload can actually yield a tunnel (engine JSON with outbounds, or a
    /// share-link/base64 subscription). Public for the headless tests.
    static bool isUsableConfig(const QByteArray &configData);

    CoreStatus coreStatus() const { return m_coreStatus; }
    QString lastError() const { return m_lastError; }

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);
    void statusMessage(const QString &message);

private:
    void downloadSubscription(const QString &url);
    void launchEngine(const QString &configContent);
    void handleEngineReady();
    void handleEngineFailure(const QString &error);
    void teardown();
    void setCoreStatus(CoreStatus status);
    void fail(const QString &message);


    CoreProcess *m_core;
    QNetworkAccessManager m_network;

    CoreStatus m_coreStatus = Idle;
    QString m_lastError;
    quint16 m_mixedPort = 0;
    /// Whether the user asked for the desktop proxy to be redirected. Captured when the
    /// engine is launched and consulted once it reports ready.
    bool m_wantSystemProxy = false;
};

#endif // VPNCORE_H
