#include "connectionmodel.h"
#include <QDir>
#include <QStandardPaths>
#include "profilelistmodel.h"
#include "proxylistmodel.h"
#include "settingsmodel.h"
#include "vpncore.h"
#include "../services/AccountManager.h"
#include "../services/ClashApi.h"
#include "../services/PremiumConfig.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>

#include <chrono>

namespace {

constexpr int kStatsIntervalMs = 1000;
/// The clash API only exists once the core has finished starting; give it a moment before the
/// first poll and before replaying the node selection.
constexpr int kCoreSettleMs = 1500;
/// Cap on how long Auto Connect waits for the subscription lookup before dialling anyway.
constexpr int kAutoConnectWatchdogMs = 6000;

QString formatBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    static const char *const units[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes) / 1024.0;
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', value >= 100.0 ? 0 : 1).arg(units[unit]);
}

/// Public-IP endpoints, tried in order — same list and order as the Swift client.
const QStringList &ipEndpoints()
{
    static const QStringList urls{QStringLiteral("https://ipwho.is/"),
                                  QStringLiteral("https://api.ip.sb/geoip/"),
                                  QStringLiteral("https://ipinfo.io/json")};
    return urls;
}

} // namespace

ConnectionModel *ConnectionModel::s_instance = nullptr;

ConnectionModel::ConnectionModel(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    m_vpnCore = new VpnCore(this);
    m_premiumConfig = new PremiumConfig(this);
    m_clashApi = new ClashApi(this);

    connect(m_vpnCore, &VpnCore::connected, this, &ConnectionModel::onVpnConnected);
    connect(m_vpnCore, &VpnCore::disconnected, this, &ConnectionModel::onVpnDisconnected);
    connect(m_vpnCore, &VpnCore::errorOccurred, this, &ConnectionModel::onVpnError);
    connect(m_vpnCore, &VpnCore::statusMessage, this, &ConnectionModel::onVpnStatusMessage);

    m_statsTimer.setInterval(kStatsIntervalMs);
    connect(&m_statsTimer, &QTimer::timeout, this, &ConnectionModel::pollStats);

    // /subscriptions/status answers 1-3 s after launch. If it upgrades the account while a
    // free-fallback session is already up, say so instead of leaving a paying customer on the
    // free pool for the rest of the session.
    if (AccountManager *account = AccountManager::instance()) {
        m_lastKnownPremium = account->premium();
        connect(account, &AccountManager::changed, this, &ConnectionModel::onAccountChanged);
    }

    // main.cpp builds SettingsModel and ProfileListModel after this one, so the auto-connect
    // decision has to wait for the event loop — every singleton exists by then.
    QMetaObject::invokeMethod(this, [this]() { scheduleAutoConnect(); }, Qt::QueuedConnection);
}

ConnectionModel::Status ConnectionModel::status() const { return m_status; }

void ConnectionModel::setStatus(Status status)
{
    if (m_status != status) {
        m_status = status;
        emit statusChanged();
    }
}

QString ConnectionModel::uploadSpeed() const { return m_uploadSpeed; }
QString ConnectionModel::downloadSpeed() const { return m_downloadSpeed; }
QString ConnectionModel::uploadTotal() const { return m_uploadTotal; }
QString ConnectionModel::downloadTotal() const { return m_downloadTotal; }
QString ConnectionModel::ipAddress() const { return m_ipAddress; }
QString ConnectionModel::activeProxyName() const { return m_activeProxyName; }
QString ConnectionModel::activeProxyType() const { return m_activeProxyType; }
QString ConnectionModel::activeProxyCountry() const { return m_activeProxyCountry; }
QString ConnectionModel::statusLog() const { return m_statusLog; }
QString ConnectionModel::sessionNotice() const { return m_sessionNotice; }
bool ConnectionModel::sessionNoticeIsError() const { return m_sessionNoticeIsError; }

void ConnectionModel::setSessionNotice(const QString &message, bool isError)
{
    if (m_sessionNotice == message && m_sessionNoticeIsError == isError)
        return;
    m_sessionNotice = message;
    m_sessionNoticeIsError = isError;
    emit sessionNoticeChanged();
}

void ConnectionModel::dismissSessionNotice()
{
    setSessionNotice({}, false);
}

void ConnectionModel::toggleConnection()
{
    switch (m_status) {
    case Disconnected:
        setStatus(Connecting);
        startConnect();
        break;
    case Connected:
        setStatus(Disconnecting);
        m_vpnCore->disconnectVpn();
        break;
    default:
        break;
    }
}

void ConnectionModel::retryConnection()
{
    setSessionNotice({}, false);
    switch (m_status) {
    case Connected:
        // Only reached from an explicit banner click, so dropping the tunnel is consented to.
        m_reconnectAfterDisconnect = true;
        setStatus(Disconnecting);
        m_vpnCore->disconnectVpn();
        break;
    case Disconnected:
        setStatus(Connecting);
        startConnect();
        break;
    default:
        break;
    }
}

void ConnectionModel::onAccountChanged()
{
    AccountManager *account = AccountManager::instance();
    if (!account)
        return;

    const bool premium = account->premium();
    const bool becamePremium = premium && !m_lastKnownPremium;
    const bool lostPremium = !premium && m_lastKnownPremium;
    m_lastKnownPremium = premium;

    if (becamePremium && m_status == Connected && m_freeFallbackSession) {
        emit premiumAvailableForReconnect();
        setSessionNotice(QStringLiteral("Your Premium subscription is active, but this session "
                                        "started on the free pool. Reconnect to move onto Premium "
                                        "servers."),
                         false);
    }

    // The mirror case: the entitlement ended (lapse, sign-out, account deletion) while a session
    // dialled on the premium config is still up. Nothing else notices, so without this the tunnel
    // keeps using premium servers until the user happens to disconnect by hand.
    if (lostPremium && m_status == Connected && m_activeProxyName == PremiumConfig::kProfileName)
        setSessionNotice(QStringLiteral("Premium access ended, but this session is still running on "
                                        "Premium servers. Reconnect to move onto the free pool."),
                         false);

    // Premium ending (lapse, sign-out, account deletion) has to take the decrypted premium config
    // with it. VpnCore writes it to AppDataLocation/engine/config.json and only clears that
    // directory on the NEXT connect, so leaving it behind kept a paid config usable afterwards.
    if (lostPremium)
        discardPremiumConfigOnDisk();

    // Only relevant while the launch-time auto-connect is still waiting for the tier.
    if (m_autoConnectPending && !account->busy())
        maybeAutoConnect();
}

/// Erase the decrypted premium config written by the last connect. Safe while connected: the core
/// already holds its own copy, so this only prevents a FUTURE run from reusing it.
void ConnectionModel::discardPremiumConfigOnDisk()
{
    QDir(VpnCore::engineDir()).removeRecursively();
}

void ConnectionModel::scheduleAutoConnect()
{
    if (!SettingsModel::instance() || !SettingsModel::instance()->autoConnect())
        return;

    m_autoConnectPending = true;

    AccountManager *account = AccountManager::instance();
    if (!account || !account->busy()) {
        maybeAutoConnect();
        return;
    }
    // onAccountChanged() fires as soon as the tier settles; this is only the backstop for a
    // subscription lookup that never answers.
    QTimer::singleShot(kAutoConnectWatchdogMs, this, [this]() { maybeAutoConnect(); });
}

void ConnectionModel::maybeAutoConnect()
{
    if (!m_autoConnectPending)
        return;
    m_autoConnectPending = false;

    // The user may have connected by hand while the subscription lookup was still running.
    if (m_status != Disconnected)
        return;

    onVpnStatusMessage(QStringLiteral("Auto Connect: connecting..."));
    setStatus(Connecting);
    startConnect();
}

void ConnectionModel::startConnect()
{
    // 1. A profile the user added and selected always wins (like the mobile clients, where
    //    "AiBooster Premium" is just another entry in the profile list).
    if (ProfileListModel *profiles = ProfileListModel::instance()) {
        const QString content = profiles->activeProfileContent();
        if (!content.trimmed().isEmpty()) {
            m_pendingProxyName = profiles->activeProfileName();
            m_pendingFreeFallback = false;
            m_vpnCore->connectVpnWithConfig(content);
            return;
        }
    }

    // 2. Premium accounts get the encrypted premium config (fetch → decrypt → sanitize).
    AccountManager *account = AccountManager::instance();
    if (account && account->premium()) {
        onVpnStatusMessage(QStringLiteral("Fetching %1 config...").arg(PremiumConfig::kProfileName));
        m_premiumConfig->fetch(
            [this](const QString &config) {
                if (m_status != Connecting)
                    return; // the attempt was abandoned while the fetch ran
                m_pendingProxyName = PremiumConfig::kProfileName;
                m_pendingFreeFallback = false;
                m_vpnCore->connectVpnWithConfig(config);
            },
            [this](const QString &error) {
                if (m_status != Connecting)
                    return;
                // Keep the Connect button working: drop to the free tier for this session.
                // Surface it — a paying user silently routed over free servers is worse than
                // a visible failure.
                emit premiumFellBackToFree(error);
                setSessionNotice(QStringLiteral("Premium servers are unavailable (%1) — this "
                                                "session is running on the free pool.").arg(error),
                                 false);
                onVpnStatusMessage(QStringLiteral("Premium config failed (%1), using free tier")
                                       .arg(error));
                connectFree();
            });
        return;
    }

    // 3. Logged out / free tier.
    connectFree();
}

void ConnectionModel::connectFree()
{
    m_pendingProxyName = QStringLiteral("AiBooster Free");
    m_pendingFreeFallback = true;
    // NOT connectVpn(kFreeConfigUrl): that URL answers a profile INDEX, and handing the index
    // to the core yields a config with zero outbounds — the core reports nothing and the app
    // would claim "Connected" with no tunnel. fetchFree() resolves the index to a real
    // subscription body first.
    m_premiumConfig->fetchFree(
        [this](const QString &config) {
            if (m_status != Connecting)
                return;
            m_vpnCore->connectVpnWithConfig(config);
        },
        [this](const QString &error) {
            if (m_status != Connecting)
                return;
            onVpnError(QStringLiteral("Could not fetch the free config: %1").arg(error));
        });
}

void ConnectionModel::onVpnConnected()
{
    m_activeProxyName = m_pendingProxyName.isEmpty() ? QStringLiteral("AiBooster VPN")
                                                     : m_pendingProxyName;
    m_freeFallbackSession = m_pendingFreeFallback;
    m_activeProxyType = "Auto";
    m_activeProxyCountry = "Auto";
    m_ipAddress = QStringLiteral("Checking…");
    resetStats();
    // A successful connect answers whatever the last failure was complaining about; the
    // informational notices describe THIS session, so they stay.
    if (m_sessionNoticeIsError)
        setSessionNotice({}, false);
    setStatus(Connected);
    emit statsChanged();

    // The clash API and the tunnel are not up the instant start() returns.
    QTimer::singleShot(kCoreSettleMs, this, [this]() {
        if (m_status != Connected)
            return;
        // A node picked while disconnected only reaches the core here.
        if (ProxyListModel *proxies = ProxyListModel::instance())
            proxies->applySelectionToCore();
        startStatsPolling();
        lookupIpAddress();
    });
}

void ConnectionModel::onVpnDisconnected()
{
    stopStatsPolling();
    resetStats();
    m_ipAddress = "--";
    m_activeProxyName.clear();
    m_activeProxyType.clear();
    m_activeProxyCountry.clear();
    m_freeFallbackSession = false;
    setStatus(Disconnected);
    emit statsChanged();

    if (m_reconnectAfterDisconnect) {
        m_reconnectAfterDisconnect = false;
        setStatus(Connecting);
        startConnect();
    }
}

void ConnectionModel::onVpnError(const QString &error)
{
    m_statusLog += "ERROR: " + error + "\n";
    emit statusLogChanged();
    m_reconnectAfterDisconnect = false;
    stopStatsPolling();
    setStatus(Disconnected);
    emit connectionFailed(error);
    setSessionNotice(QStringLiteral("Could not connect: %1").arg(error), true);
}

void ConnectionModel::resetStats()
{
    m_uploadSpeed = "0 B/s";
    m_downloadSpeed = "0 B/s";
    m_uploadTotal = "0 B";
    m_downloadTotal = "0 B";
    m_lastUploadTotal = 0;
    m_lastDownloadTotal = 0;
    m_lastPollMs = 0;
    m_havePreviousSample = false;
    m_statsFailureLogged = false;
}

void ConnectionModel::startStatsPolling()
{
    if (!m_statsTimer.isActive())
        m_statsTimer.start();
    pollStats();
}

void ConnectionModel::stopStatsPolling()
{
    m_statsTimer.stop();
}

void ConnectionModel::pollStats()
{
    if (m_status != Connected) {
        stopStatsPolling();
        return;
    }

    m_clashApi->fetchTraffic(
        [this](ClashApi::Traffic traffic) {
            if (m_status != Connected)
                return;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_havePreviousSample && now > m_lastPollMs) {
                const double seconds = static_cast<double>(now - m_lastPollMs) / 1000.0;
                const qint64 up = qMax<qint64>(0, traffic.uploadTotal - m_lastUploadTotal);
                const qint64 down = qMax<qint64>(0, traffic.downloadTotal - m_lastDownloadTotal);
                m_uploadSpeed = formatBytes(static_cast<qint64>(up / seconds)) + QStringLiteral("/s");
                m_downloadSpeed = formatBytes(static_cast<qint64>(down / seconds)) + QStringLiteral("/s");
            }
            m_lastUploadTotal = traffic.uploadTotal;
            m_lastDownloadTotal = traffic.downloadTotal;
            m_lastPollMs = now;
            m_havePreviousSample = true;
            m_uploadTotal = formatBytes(traffic.uploadTotal);
            m_downloadTotal = formatBytes(traffic.downloadTotal);
            m_statsFailureLogged = false;
            emit statsChanged();
        },
        [this](const QString &error) {
            // Keep the last figures rather than flashing zeros; one missed poll is normal
            // while the core is still bringing the API up. Log the start of a failure streak
            // only — this runs once a second and would otherwise flood the status log.
            if (!m_statsFailureLogged) {
                m_statsFailureLogged = true;
                onVpnStatusMessage(QStringLiteral("Traffic stats unavailable: %1").arg(error));
            }
        });
}

void ConnectionModel::lookupIpAddress(int endpointIndex)
{
    if (m_status != Connected)
        return;
    if (endpointIndex >= ipEndpoints().size()) {
        m_ipAddress = QStringLiteral("Unavailable");
        emit statsChanged();
        return;
    }

    if (!m_ipLookup)
        m_ipLookup = new QNetworkAccessManager(this);
    // Go through the core's own mixed inbound: the desktop app is not inside the tunnel unless
    // TUN or the system proxy happens to be on, and asking directly would report the machine's
    // real IP as if it were the exit node's. Re-read the port every time — it is user-editable.
    const int mixedPort = SettingsModel::instance() ? SettingsModel::instance()->mixedPort() : 2334;
    m_ipLookup->setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"),
                                       static_cast<quint16>(mixedPort)));

    QNetworkRequest request{QUrl(ipEndpoints().at(endpointIndex))};
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(std::chrono::milliseconds(8000));

    QNetworkReply *reply = m_ipLookup->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, endpointIndex]() {
        reply->deleteLater();
        const QString ip = reply->error() == QNetworkReply::NoError
            ? QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("ip")).toString()
            : QString();
        if (ip.isEmpty()) {
            lookupIpAddress(endpointIndex + 1);
            return;
        }
        m_ipAddress = ip;
        emit statsChanged();
    });
}

void ConnectionModel::onVpnStatusMessage(const QString &message)
{
    m_statusLog += message + "\n";
    emit statusLogChanged();
}
