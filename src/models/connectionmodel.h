#ifndef CONNECTIONMODEL_H
#define CONNECTIONMODEL_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>

class ClashApi;
class FreeProfiles;
class VpnCore;

class ConnectionModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Status status READ status WRITE setStatus NOTIFY statusChanged)
    Q_PROPERTY(QString uploadSpeed READ uploadSpeed NOTIFY statsChanged)
    Q_PROPERTY(QString downloadSpeed READ downloadSpeed NOTIFY statsChanged)
    Q_PROPERTY(QString uploadTotal READ uploadTotal NOTIFY statsChanged)
    Q_PROPERTY(QString downloadTotal READ downloadTotal NOTIFY statsChanged)
    Q_PROPERTY(QString ipAddress READ ipAddress NOTIFY statsChanged)
    Q_PROPERTY(QString activeProxyName READ activeProxyName NOTIFY statsChanged)
    Q_PROPERTY(QString activeProxyType READ activeProxyType NOTIFY statsChanged)
    Q_PROPERTY(QString activeProxyCountry READ activeProxyCountry NOTIFY statsChanged)
    Q_PROPERTY(QString statusLog READ statusLog NOTIFY statusLogChanged)
    /// Sticky copy of the last session notice. The banner lives on the Home screen, whose Loader is
    /// destroyed the moment another tab is selected, so a notice delivered only as a signal was lost
    /// outright for anyone who browsed away while the connect was still dialling.
    Q_PROPERTY(QString sessionNotice READ sessionNotice NOTIFY sessionNoticeChanged)
    Q_PROPERTY(bool sessionNoticeIsError READ sessionNoticeIsError NOTIFY sessionNoticeChanged)

public:
    enum Status {
        Disconnected,
        Connecting,
        Connected,
        Disconnecting
    };
    Q_ENUM(Status)

    explicit ConnectionModel(QObject *parent = nullptr);

    /// Last-constructed instance — ProxyListModel needs the status to know whether the core is
    /// there to accept a node switch.
    static ConnectionModel *instance() { return s_instance; }

    Status status() const;
    void setStatus(Status status);

    QString uploadSpeed() const;
    QString downloadSpeed() const;
    QString uploadTotal() const;
    QString downloadTotal() const;
    QString ipAddress() const;
    QString activeProxyName() const;
    QString activeProxyType() const;
    QString activeProxyCountry() const;
    QString statusLog() const;
    QString sessionNotice() const;
    bool sessionNoticeIsError() const;

    Q_INVOKABLE void toggleConnection();
    /// User-initiated re-attempt from the notice banner: tears the current session down (if
    /// any) and re-runs the config-source picker. Only ever called from an explicit click —
    /// we never drop traffic on our own.
    Q_INVOKABLE void retryConnection();
    /// The banner's ✕ — the user has read the notice.
    Q_INVOKABLE void dismissSessionNotice();

signals:
    void statusChanged();
    void statsChanged();
    void statusLogChanged();
    void sessionNoticeChanged();
    /// The connect attempt failed. statusLog alone is not enough — nothing binds it, so a
    /// failed connect used to leave the button snapping back with no explanation at all.
    void connectionFailed(const QString &error);

private:
    /// Picks the config source: the active user profile, otherwise the free nodes.
    void startConnect();
    void connectFree();
    void onVpnConnected();
    void onVpnDisconnected();
    void onVpnError(const QString &error);
    void onVpnStatusMessage(const QString &message);
    void setSessionNotice(const QString &message, bool isError);

    /// Auto Connect: dial out once at launch when the setting is on.
    void autoConnectAtLaunch();

    void resetStats();
    void startStatsPolling();
    void stopStatsPolling();
    void pollStats();
    /// Public IP as seen from the far end of the tunnel, fetched THROUGH the local mixed
    /// inbound so the answer is the exit node's even when neither TUN nor the system proxy is on.
    void lookupIpAddress(int endpointIndex = 0);

    VpnCore *m_vpnCore = nullptr;
    FreeProfiles *m_freeProfiles = nullptr;
    ClashApi *m_clashApi = nullptr;
    QNetworkAccessManager *m_ipLookup = nullptr;
    QTimer m_statsTimer;
    /// Counters from the previous poll, for the per-second speeds.
    qint64 m_lastUploadTotal = 0;
    qint64 m_lastDownloadTotal = 0;
    qint64 m_lastPollMs = 0;
    bool m_havePreviousSample = false;
    /// The stats poll runs once a second; only the first failure of a streak is logged.
    bool m_statsFailureLogged = false;
    /// Name shown as the active proxy once the pending connect succeeds.
    QString m_pendingProxyName;
    /// Set by retryConnection() so the disconnect it triggers rolls straight into a connect.
    bool m_reconnectAfterDisconnect = false;

    Status m_status = Disconnected;
    QString m_uploadSpeed = "0 B/s";
    QString m_downloadSpeed = "0 B/s";
    QString m_uploadTotal = "0 B";
    QString m_downloadTotal = "0 B";
    QString m_ipAddress = "--";
    QString m_activeProxyName;
    QString m_activeProxyType;
    QString m_activeProxyCountry;
    QString m_statusLog;
    QString m_sessionNotice;
    bool m_sessionNoticeIsError = false;

    static ConnectionModel *s_instance;
};

#endif
