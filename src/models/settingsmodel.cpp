#include "settingsmodel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>

SettingsModel *SettingsModel::s_instance = nullptr;

namespace {
/// config.WarpOptions.Mode — the only two values v2/config/builder.go tests for.
const QStringList &warpModes()
{
    static const QStringList modes{QStringLiteral("proxy_over_warp"), QStringLiteral("warp_over_proxy")};
    return modes;
}

/// The geoip-<region>/geosite-<region> rule sets the core downloads (see builder.go).
const QStringList &regions()
{
    static const QStringList list{QStringLiteral("ir"), QStringLiteral("cn"), QStringLiteral("ru"),
                                  QStringLiteral("af"), QStringLiteral("id"), QStringLiteral("tr"),
                                  QStringLiteral("br"), QStringLiteral("other")};
    return list;
}

const QStringList &balancerStrategies()
{
    static const QStringList list{QStringLiteral("round-robin"), QStringLiteral("consistent-hashing"),
                                  QStringLiteral("sticky-sessions")};
    return list;
}
} // namespace

SettingsModel::SettingsModel(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    load();
}

void SettingsModel::load()
{
    QSettings st;
    st.beginGroup("settings");
    m_systemProxy        = st.value("systemProxy", m_systemProxy).toBool();
    m_tunMode            = st.value("tunMode", m_tunMode).toBool();
    m_autoConnect        = st.value("autoConnect", m_autoConnect).toBool();
    m_enableIPv6         = st.value("enableIPv6", m_enableIPv6).toBool();
    m_remoteDns          = st.value("remoteDns", m_remoteDns).toString();
    m_directDns          = st.value("directDns", m_directDns).toString();
    m_enableFakeDns      = st.value("enableFakeDns", m_enableFakeDns).toBool();
    m_connectionTestUrl  = st.value("connectionTestUrl", m_connectionTestUrl).toString();
    m_mixedPort          = st.value("mixedPort", m_mixedPort).toInt();
    m_localDnsPort       = st.value("localDnsPort", m_localDnsPort).toInt();
    m_enableTlsFragment  = st.value("enableTlsFragment", m_enableTlsFragment).toBool();
    m_tlsFragmentSize    = st.value("tlsFragmentSize", m_tlsFragmentSize).toString();
    m_tlsFragmentSleep   = st.value("tlsFragmentSleep", m_tlsFragmentSleep).toString();
    m_enableMux          = st.value("enableMux", m_enableMux).toBool();
    m_muxProtocol        = st.value("muxProtocol", m_muxProtocol).toString();
    m_muxMaxConnections  = st.value("muxMaxConnections", m_muxMaxConnections).toInt();
    m_warpEnabled        = st.value("warpEnabled", m_warpEnabled).toBool();
    m_warpMode           = st.value("warpMode", m_warpMode).toString();
    m_region             = st.value("region", m_region).toString();
    m_bypassLan          = st.value("bypassLan", m_bypassLan).toBool();
    m_blockAds           = st.value("blockAds", m_blockAds).toBool();
    m_resolveDestination = st.value("resolveDestination", m_resolveDestination).toBool();
    m_balancerStrategy   = st.value("balancerStrategy", m_balancerStrategy).toString();
    st.endGroup();

    // Ports are uint16 in the core; an out-of-range value makes the WHOLE options payload
    // fail to unmarshal, which surfaces to the user as "the VPN won't start".
    if (m_mixedPort < 1 || m_mixedPort > 65535) m_mixedPort = 2334;
    if (m_localDnsPort < 1 || m_localDnsPort > 65535) m_localDnsPort = 6450;

    // Anything stored by an older build (or hand-edited) that the core would not recognise
    // falls back to the default rather than being handed over and silently ignored.
    if (!warpModes().contains(m_warpMode)) m_warpMode = warpModes().first();
    if (!regions().contains(m_region)) m_region = QStringLiteral("other");
    if (!balancerStrategies().contains(m_balancerStrategy))
        m_balancerStrategy = balancerStrategies().first();
    if (!isValidRange(m_tlsFragmentSize)) m_tlsFragmentSize = QStringLiteral("10-100");
    if (!isValidRange(m_tlsFragmentSleep)) m_tlsFragmentSleep = QStringLiteral("50-200");
}

void SettingsModel::persist() const
{
    QSettings st;
    st.beginGroup("settings");
    st.setValue("systemProxy", m_systemProxy);
    st.setValue("tunMode", m_tunMode);
    st.setValue("autoConnect", m_autoConnect);
    st.setValue("enableIPv6", m_enableIPv6);
    st.setValue("remoteDns", m_remoteDns);
    st.setValue("directDns", m_directDns);
    st.setValue("enableFakeDns", m_enableFakeDns);
    st.setValue("connectionTestUrl", m_connectionTestUrl);
    st.setValue("mixedPort", m_mixedPort);
    st.setValue("localDnsPort", m_localDnsPort);
    st.setValue("enableTlsFragment", m_enableTlsFragment);
    st.setValue("tlsFragmentSize", m_tlsFragmentSize);
    st.setValue("tlsFragmentSleep", m_tlsFragmentSleep);
    st.setValue("enableMux", m_enableMux);
    st.setValue("muxProtocol", m_muxProtocol);
    st.setValue("muxMaxConnections", m_muxMaxConnections);
    st.setValue("warpEnabled", m_warpEnabled);
    st.setValue("warpMode", m_warpMode);
    st.setValue("region", m_region);
    st.setValue("bypassLan", m_bypassLan);
    st.setValue("blockAds", m_blockAds);
    st.setValue("resolveDestination", m_resolveDestination);
    st.setValue("balancerStrategy", m_balancerStrategy);
    st.endGroup();
}

bool SettingsModel::isValidRange(const QString &v)
{
    const QStringList parts = v.split(QLatin1Char('-'));
    if (parts.size() != 2)
        return false;
    bool lowOk = false, highOk = false;
    const int low = parts.at(0).trimmed().toInt(&lowOk);
    const int high = parts.at(1).trimmed().toInt(&highOk);
    return lowOk && highOk && low >= 0 && low <= high;
}

void SettingsModel::setTlsFragmentSize(const QString &v)
{
    if (!isValidRange(v) || m_tlsFragmentSize == v) return;
    m_tlsFragmentSize = v;
    persist();
    emit changed();
}

void SettingsModel::setTlsFragmentSleep(const QString &v)
{
    if (!isValidRange(v) || m_tlsFragmentSleep == v) return;
    m_tlsFragmentSleep = v;
    persist();
    emit changed();
}

void SettingsModel::setWarpMode(const QString &v)
{
    if (!warpModes().contains(v) || m_warpMode == v) return;
    m_warpMode = v;
    persist();
    emit changed();
}

void SettingsModel::setRegion(const QString &v)
{
    if (!regions().contains(v) || m_region == v) return;
    m_region = v;
    persist();
    emit changed();
}

void SettingsModel::setBalancerStrategy(const QString &v)
{
    if (!balancerStrategies().contains(v) || m_balancerStrategy == v) return;
    m_balancerStrategy = v;
    persist();
    emit changed();
}

void SettingsModel::setMixedPort(int v)
{
    if (v < 1 || v > 65535) return;   // reject rather than poison the core payload
    if (m_mixedPort == v) return;
    m_mixedPort = v;
    persist();
    emit changed();
}

void SettingsModel::setLocalDnsPort(int v)
{
    if (v < 1 || v > 65535) return;
    if (m_localDnsPort == v) return;
    m_localDnsPort = v;
    persist();
    emit changed();
}

namespace {

/// "Auto" (or empty) means "whatever the engine would have chosen". Since the engine no
/// longer fills that in for us on this path, we spell its own documented default out.
/// Keep in sync with the engine's own default options.
QString resolvedDns(const QString &configured)
{
    if (configured.isEmpty() || configured.compare(QLatin1String("Auto"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("1.1.1.1");
    return configured;
}

} // namespace

QByteArray SettingsModel::buildEngineSettingsJson() const
{
    QJsonObject root;

    root["enable-tun"]        = m_tunMode;
    root["set-system-proxy"]  = m_systemProxy;
    root["mixed-port"]        = m_mixedPort;
    root["strict-route"]      = true;
    root["tun-implementation"] = QStringLiteral("mixed");
    root["mtu"]               = 9000;
    root["enable-clash-api"]  = true;
    root["clash-api-port"]    = clashApiPort();
    root["web-secret"]        = clashApiSecret();
    root["log-level"]         = QStringLiteral("info");
    root["connection-test-url"] = m_connectionTestUrl;
    root["enable-fake-dns"]   = m_enableFakeDns;

    // The core's DomainStrategy vocabulary.
    root["ipv6-mode"] = m_enableIPv6 ? QStringLiteral("prefer_ipv4") : QStringLiteral("ipv4_only");

    // How often the engine re-tests outbounds. Omitting it yields 0, which is not "use the
    // default" — see the note on DNS below.
    root["url-test-interval"] = kDefaultUrlTestIntervalSeconds;

    // Routing.
    root["region"]              = m_region;
    root["bypass-lan"]          = m_bypassLan;
    root["block-ads"]           = m_blockAds;
    root["resolve-destination"] = m_resolveDestination;
    root["balancer-strategy"]   = m_balancerStrategy;

    // ⚠️ These are always written, even for "Auto".
    //
    // This document is handed to the engine as a FILE, and the engine unmarshals it into a
    // zero-valued options struct — it does NOT merge it over its own defaults. (The
    // in-process API these settings were originally written for did merge, which is why
    // omitting a field used to mean "keep the default". It no longer does.)
    //
    // So a missing remote-dns-address arrives as the empty string, and the engine rejects
    // the whole config with "invalid server address" — i.e. the client cannot connect at
    // all. Verified against a real engine build; see tests/engine-settings.
    //
    // Any field whose zero value is invalid must therefore carry the engine's own default.
    root["remote-dns-address"] = resolvedDns(m_remoteDns);
    root["direct-dns-address"] = resolvedDns(m_directDns);

    // TLS tricks — only the keys config.TLSTricks actually declares.
    QJsonObject tls;
    tls["enable-fragment"] = m_enableTlsFragment;
    tls["fragment-size"]   = m_tlsFragmentSize;
    tls["fragment-sleep"]  = m_tlsFragmentSleep;
    root["tls-tricks"] = tls;

    if (m_enableMux) {
        QJsonObject mux;
        mux["enable"]      = true;
        mux["padding"]     = true;
        mux["max-streams"] = m_muxMaxConnections;
        mux["protocol"]    = m_muxProtocol;
        root["mux"] = mux;
    }

    if (m_warpEnabled) {
        QJsonObject warp;
        warp["enable"] = true;
        warp["mode"]   = m_warpMode;   // proxy_over_warp | warp_over_proxy
        root["warp"] = warp;
    }

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
