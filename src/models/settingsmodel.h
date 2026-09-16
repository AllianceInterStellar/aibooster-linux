#ifndef SETTINGSMODEL_H
#define SETTINGSMODEL_H

#include <QObject>
#include <QQmlEngine>

class SettingsModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool systemProxy READ systemProxy WRITE setSystemProxy NOTIFY changed)
    Q_PROPERTY(bool tunMode READ tunMode WRITE setTunMode NOTIFY changed)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY changed)
    Q_PROPERTY(bool enableIPv6 READ enableIPv6 WRITE setEnableIPv6 NOTIFY changed)
    Q_PROPERTY(QString remoteDns READ remoteDns WRITE setRemoteDns NOTIFY changed)
    Q_PROPERTY(QString directDns READ directDns WRITE setDirectDns NOTIFY changed)
    Q_PROPERTY(bool enableFakeDns READ enableFakeDns WRITE setEnableFakeDns NOTIFY changed)
    Q_PROPERTY(QString connectionTestUrl READ connectionTestUrl WRITE setConnectionTestUrl NOTIFY changed)
    Q_PROPERTY(int mixedPort READ mixedPort WRITE setMixedPort NOTIFY changed)
    Q_PROPERTY(int localDnsPort READ localDnsPort WRITE setLocalDnsPort NOTIFY changed)
    Q_PROPERTY(bool enableTlsFragment READ enableTlsFragment WRITE setEnableTlsFragment NOTIFY changed)
    Q_PROPERTY(QString tlsFragmentSize READ tlsFragmentSize WRITE setTlsFragmentSize NOTIFY changed)
    Q_PROPERTY(QString tlsFragmentSleep READ tlsFragmentSleep WRITE setTlsFragmentSleep NOTIFY changed)
    Q_PROPERTY(bool enableMux READ enableMux WRITE setEnableMux NOTIFY changed)
    Q_PROPERTY(QString muxProtocol READ muxProtocol WRITE setMuxProtocol NOTIFY changed)
    Q_PROPERTY(int muxMaxConnections READ muxMaxConnections WRITE setMuxMaxConnections NOTIFY changed)
    Q_PROPERTY(bool warpEnabled READ warpEnabled WRITE setWarpEnabled NOTIFY changed)
    Q_PROPERTY(QString warpMode READ warpMode WRITE setWarpMode NOTIFY changed)
    // ── Routing (config.RouteOptions + region/balancer) ──
    Q_PROPERTY(QString region READ region WRITE setRegion NOTIFY changed)
    Q_PROPERTY(bool bypassLan READ bypassLan WRITE setBypassLan NOTIFY changed)
    Q_PROPERTY(bool blockAds READ blockAds WRITE setBlockAds NOTIFY changed)
    Q_PROPERTY(bool resolveDestination READ resolveDestination WRITE setResolveDestination NOTIFY changed)
    Q_PROPERTY(QString balancerStrategy READ balancerStrategy WRITE setBalancerStrategy NOTIFY changed)

public:
    explicit SettingsModel(QObject *parent = nullptr);

    bool systemProxy() const { return m_systemProxy; }
    void setSystemProxy(bool v) { if (m_systemProxy != v) { m_systemProxy = v; persist(); emit changed(); } }

    bool tunMode() const { return m_tunMode; }
    void setTunMode(bool v) { if (m_tunMode != v) { m_tunMode = v; persist(); emit changed(); } }

    bool autoConnect() const { return m_autoConnect; }
    void setAutoConnect(bool v) { if (m_autoConnect != v) { m_autoConnect = v; persist(); emit changed(); } }

    bool enableIPv6() const { return m_enableIPv6; }
    void setEnableIPv6(bool v) { if (m_enableIPv6 != v) { m_enableIPv6 = v; persist(); emit changed(); } }

    QString remoteDns() const { return m_remoteDns; }
    QString directDns() const { return m_directDns; }

    bool enableFakeDns() const { return m_enableFakeDns; }
    void setEnableFakeDns(bool v) { if (m_enableFakeDns != v) { m_enableFakeDns = v; persist(); emit changed(); } }

    QString connectionTestUrl() const { return m_connectionTestUrl; }
    int mixedPort() const { return m_mixedPort; }
    int localDnsPort() const { return m_localDnsPort; }

    bool enableTlsFragment() const { return m_enableTlsFragment; }
    void setEnableTlsFragment(bool v) { if (m_enableTlsFragment != v) { m_enableTlsFragment = v; persist(); emit changed(); } }

    QString tlsFragmentSize() const { return m_tlsFragmentSize; }
    QString tlsFragmentSleep() const { return m_tlsFragmentSleep; }

    bool enableMux() const { return m_enableMux; }
    void setEnableMux(bool v) { if (m_enableMux != v) { m_enableMux = v; persist(); emit changed(); } }

    QString muxProtocol() const { return m_muxProtocol; }
    int muxMaxConnections() const { return m_muxMaxConnections; }

    bool warpEnabled() const { return m_warpEnabled; }
    void setWarpEnabled(bool v) { if (m_warpEnabled != v) { m_warpEnabled = v; persist(); emit changed(); } }

    QString warpMode() const { return m_warpMode; }

    QString region() const { return m_region; }
    bool bypassLan() const { return m_bypassLan; }
    void setBypassLan(bool v) { if (m_bypassLan != v) { m_bypassLan = v; persist(); emit changed(); } }
    bool blockAds() const { return m_blockAds; }
    void setBlockAds(bool v) { if (m_blockAds != v) { m_blockAds = v; persist(); emit changed(); } }
    bool resolveDestination() const { return m_resolveDestination; }
    void setResolveDestination(bool v) { if (m_resolveDestination != v) { m_resolveDestination = v; persist(); emit changed(); } }
    QString balancerStrategy() const { return m_balancerStrategy; }

    // Setters for the formerly read-only properties (QML could display but not edit them).
    void setRemoteDns(const QString &v) { if (m_remoteDns != v) { m_remoteDns = v; persist(); emit changed(); } }
    void setDirectDns(const QString &v) { if (m_directDns != v) { m_directDns = v; persist(); emit changed(); } }
    void setConnectionTestUrl(const QString &v) { if (m_connectionTestUrl != v) { m_connectionTestUrl = v; persist(); emit changed(); } }
    void setMixedPort(int v);
    void setLocalDnsPort(int v);
    void setTlsFragmentSize(const QString &v);
    void setTlsFragmentSleep(const QString &v);
    void setMuxProtocol(const QString &v) { if (m_muxProtocol != v) { m_muxProtocol = v; persist(); emit changed(); } }
    void setMuxMaxConnections(int v) { if (m_muxMaxConnections != v) { m_muxMaxConnections = v; persist(); emit changed(); } }
    void setWarpMode(const QString &v);
    void setRegion(const QString &v);
    void setBalancerStrategy(const QString &v);

    /// Last-constructed instance — VpnCore needs it to build the core payload.
    static SettingsModel *instance() { return s_instance; }

    /// Clash API the running core exposes. The secret is a fixed string rather than empty:
    /// config.BuildConfig invents a random 16-char secret for an empty `web-secret`, and the
    /// app could then never authenticate against its own core (traffic stats, node switching).
    /// The engine's mixed (HTTP+SOCKS) inbound. Also the port the system proxy is
    /// pointed at, so it must agree with m_mixedPort's initialiser below.
    static constexpr int kDefaultMixedPort = 2334;

    /// The engine's own default outbound re-test interval, in seconds. Written explicitly
    /// because an omitted field reaches the engine as 0, not as "use your default".
    static constexpr int kDefaultUrlTestIntervalSeconds = 600;

    static quint16 clashApiPort() { return 18756; }
    static QString clashApiSecret() { return QStringLiteral("aibooster-clash-api"); }

    /// hiddify-core options JSON. Keys verified against hiddify-core v2/config/hiddify_option.go;
    /// the core MERGES this onto its defaults, so omitting a key keeps the sensible default.
    QByteArray buildHiddifySettingsJson() const;

    /// "<min>-<max>", min <= max — the only shape config.TLSTricks parses.
    static bool isValidRange(const QString &v);

signals:
    void changed();

private:
    void load();
    void persist() const;

    static SettingsModel *s_instance;

    bool m_systemProxy = true;
    bool m_tunMode = false;
    bool m_autoConnect = false;
    bool m_enableIPv6 = false;
    QString m_remoteDns = "udp://1.1.1.1";
    QString m_directDns = "Auto";
    bool m_enableFakeDns = false;
    QString m_connectionTestUrl = "https://www.gstatic.com/generate_204";
    int m_mixedPort = kDefaultMixedPort;
    int m_localDnsPort = 6450;
    bool m_enableTlsFragment = false;
    QString m_tlsFragmentSize = "10-100";
    QString m_tlsFragmentSleep = "50-200";
    bool m_enableMux = false;
    QString m_muxProtocol = "h2mux";
    int m_muxMaxConnections = 8;
    bool m_warpEnabled = false;
    QString m_warpMode = "proxy_over_warp";
    QString m_region = "other";
    bool m_bypassLan = true;
    bool m_blockAds = false;
    bool m_resolveDestination = false;
    QString m_balancerStrategy = "round-robin";
};

#endif
