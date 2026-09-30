#include "connectionmodel.h"
#include "profilelistmodel.h"
#include "proxylistmodel.h"
#include "settingsmodel.h"
#include "vpncore.h"
#include "../services/ClashApi.h"
#include "../services/FreeProfiles.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInformation>
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

const QList<int> &ConnectionModel::reconnectDelaysSeconds()
{
    // Quick at first (most drops are the engine restarting or a brief network blip), then
    // backing off so a server that is really gone is not hammered.
    static const QList<int> delays{2, 5, 10, 30, 60};
    return delays;
}

ConnectionModel::ConnectionModel(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    m_vpnCore = new VpnCore(this);
    m_freeProfiles = new FreeProfiles(this);
    m_clashApi = new ClashApi(this);

    connect(m_vpnCore, &VpnCore::connected, this, &ConnectionModel::onVpnConnected);
    connect(m_vpnCore, &VpnCore::disconnected, this, &ConnectionModel::onVpnDisconnected);
    connect(m_vpnCore, &VpnCore::errorOccurred, this, &ConnectionModel::onVpnError);
    connect(m_vpnCore, &VpnCore::statusMessage, this, &ConnectionModel::onVpnStatusMessage);
    connect(m_vpnCore, &VpnCore::engineLogLine, this, &ConnectionModel::logLine);

    m_statsTimer.setInterval(kStatsIntervalMs);
    connect(&m_statsTimer, &QTimer::timeout, this, &ConnectionModel::pollStats);

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &ConnectionModel::onReconnectTimer);

    // Knowing when the network is down lets a reconnect wait for it instead of spending its
    // attempts on a machine that is offline (a laptop lid closed, Wi-Fi switching). Without
    // a backend (no NetworkManager) every check reads "not known to be down", which is the
    // behaviour we had before.
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability);
#else
    QNetworkInformation::load(QNetworkInformation::Feature::Reachability);
#endif
    if (QNetworkInformation *info = QNetworkInformation::instance()) {
        connect(info, &QNetworkInformation::reachabilityChanged, this,
                &ConnectionModel::onReachabilityChanged);
    }

    // main.cpp builds SettingsModel and ProfileListModel after this one, so the auto-connect
    // decision has to wait for the event loop — every singleton exists by then.
    QMetaObject::invokeMethod(this, [this]() { autoConnectAtLaunch(); }, Qt::QueuedConnection);
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
QString ConnectionModel::statusLog() const { return m_statusLog.join(QLatin1Char('\n')); }
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
    // The ✕ on a "reconnecting in…" banner means "stop trying", not just "hide this".
    stopReconnecting();
    setSessionNotice({}, false);
}

void ConnectionModel::toggleConnection()
{
    // Whatever the user asked for replaces any reconnect we had planned.
    stopReconnecting();
    switch (m_status) {
    case Disconnected:
        setStatus(Connecting);
        startConnect();
        break;
    case Connected:
        setStatus(Disconnecting);
        m_vpnCore->disconnectVpn();
        break;
    case Connecting:
        cancelConnect();
        break;
    default:
        break;
    }
}

void ConnectionModel::cancelConnect()
{
    // A slow config can take the engine up to two minutes to come up. Being unable to back
    // out of that — the button used to ignore clicks while connecting — is how users end up
    // killing the whole app, which is the one exit that cannot restore the desktop proxy.
    ++m_attempt;   // orphans a free-node fetch still in flight
    m_reconnectAfterDisconnect = false;
    onVpnStatusMessage(QStringLiteral("Connection cancelled"));
    setStatus(Disconnecting);
    // While the engine is starting this stops it, and onVpnDisconnected() finishes the job
    // once it has gone. While the free nodes are still being fetched there is no engine,
    // and nothing else will move us on.
    if (!m_vpnCore->disconnectVpn())
        setStatus(Disconnected);
}

void ConnectionModel::retryConnection()
{
    stopReconnecting();
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

void ConnectionModel::autoConnectAtLaunch()
{
    if (!SettingsModel::instance() || !SettingsModel::instance()->autoConnect())
        return;
    if (m_status != Disconnected)
        return;

    onVpnStatusMessage(QStringLiteral("Auto Connect: connecting..."));
    setStatus(Connecting);
    startConnect();
}

void ConnectionModel::startConnect()
{
    // 1. A profile the user added and selected always wins.
    if (ProfileListModel *profiles = ProfileListModel::instance()) {
        const QString content = profiles->activeProfileContent();
        if (!content.trimmed().isEmpty()) {
            m_pendingProxyName = profiles->activeProfileName();
            m_vpnCore->connectVpnWithConfig(content);
            return;
        }
    }

    // 2. Otherwise the free nodes.
    connectFree();
}

void ConnectionModel::connectFree()
{
    m_pendingProxyName = QStringLiteral("AiBooster Free");
    const quint64 attempt = m_attempt;
    // Not the endpoint URL itself: it answers a profile INDEX, and handing the index to the
    // core yields a config with zero outbounds — the core reports nothing and the app would
    // claim "Connected" with no tunnel. FreeProfiles::fetch() resolves the index to a real
    // subscription body first.
    m_freeProfiles->fetch(
        [this, attempt](const QString &config) {
            // Abandoned while the fetch ran. The status alone cannot tell: a cancel followed
            // by a fresh connect is back at Connecting, and must not start a second engine.
            if (m_status != Connecting || attempt != m_attempt)
                return;
            m_vpnCore->connectVpnWithConfig(config);
        },
        [this, attempt](const QString &error) {
            if (m_status != Connecting || attempt != m_attempt)
                return;
            onVpnError(tr("Could not fetch the free nodes: %1").arg(error));
        });
}

void ConnectionModel::onVpnConnected()
{
    m_activeProxyName = m_pendingProxyName.isEmpty() ? QStringLiteral("AiBooster VPN")
                                                     : m_pendingProxyName;
    m_activeProxyType = "Auto";
    m_activeProxyCountry = "Auto";
    m_ipAddress = QStringLiteral("Checking…");
    resetStats();
    // A successful connect answers whatever the last failure was complaining about.
    stopReconnecting();
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
    setStatus(Disconnected);
    emit statsChanged();

    if (m_reconnectAfterDisconnect) {
        m_reconnectAfterDisconnect = false;
        setStatus(Connecting);
        startConnect();
    }
}

void ConnectionModel::appendStatusLog(const QString &line)
{
    // Bounded: this used to grow for as long as the app ran, one line per engine message.
    constexpr int kMaxStatusLines = 200;
    m_statusLog.append(line);
    if (m_statusLog.size() > kMaxStatusLines)
        m_statusLog.erase(m_statusLog.begin(), m_statusLog.end() - kMaxStatusLines);
    emit statusLogChanged();
    // The Logs page shows these next to the engine's own lines, so "Connecting…" and the
    // reason a connect failed are where the user looks for them.
    emit logLine(line);
}

void ConnectionModel::onVpnError(const QString &error)
{
    // A tunnel that was up and went down, or a reconnect attempt that failed, is worth
    // retrying. A first connect that fails is not: that is a bad config or no network, and
    // retrying it in a loop would only bury the reason.
    const bool dropped = m_status == Connected || m_reconnectAttempt > 0;

    // "ERROR " so the Logs page files it under Error.
    appendStatusLog(QStringLiteral("ERROR ") + error);
    m_reconnectAfterDisconnect = false;
    stopStatsPolling();
    setStatus(Disconnected);

    // Decided before connectionFailed goes out, so listeners (the tray's notification) can
    // tell "reconnecting" from "gave up" by reading reconnecting().
    const bool retrying = dropped && scheduleReconnect(error);
    emit connectionFailed(error);
    if (!retrying)
        setSessionNotice(tr("Could not connect: %1").arg(error), true);
}

bool ConnectionModel::scheduleReconnect(const QString &error)
{
    if (!SettingsModel::instance() || !SettingsModel::instance()->autoReconnect()) {
        stopReconnecting();
        return false;
    }

    const QList<int> &delays = reconnectDelaysSeconds();
    if (m_reconnectAttempt >= delays.size()) {
        stopReconnecting();
        setSessionNotice(tr("Connection lost, and %1 attempts to reconnect failed: %2")
                             .arg(delays.size())
                             .arg(error),
                         true);
        return true;
    }

    m_lastDropError = error;
    setReconnectAttempt(m_reconnectAttempt + 1);
    const int seconds = delays.at(m_reconnectAttempt - 1);
    m_reconnectTimer.start(seconds * 1000);
    onVpnStatusMessage(QStringLiteral("Reconnecting in %1 s (attempt %2 of %3)")
                           .arg(seconds)
                           .arg(m_reconnectAttempt)
                           .arg(delays.size()));
    setSessionNotice(tr("Connection lost: %1. Reconnecting in %2 s (attempt %3 of %4)…")
                         .arg(error)
                         .arg(seconds)
                         .arg(m_reconnectAttempt)
                         .arg(delays.size()),
                     true);
    return true;
}

void ConnectionModel::onReconnectTimer()
{
    if (m_reconnectAttempt == 0 || m_status != Disconnected)
        return;
    if (networkIsDown()) {
        // Does not use up an attempt: onReachabilityChanged() resumes when it is back.
        m_waitingForNetwork = true;
        onVpnStatusMessage(QStringLiteral("Waiting for the network to reconnect"));
        setSessionNotice(tr("Connection lost: %1. Waiting for the network…")
                             .arg(m_lastDropError),
                         true);
        return;
    }
    m_waitingForNetwork = false;
    onVpnStatusMessage(QStringLiteral("Reconnecting (attempt %1 of %2)")
                           .arg(m_reconnectAttempt)
                           .arg(reconnectDelaysSeconds().size()));
    setStatus(Connecting);
    startConnect();
}

void ConnectionModel::onReachabilityChanged()
{
    if (networkIsDown())
        return;
    // Back online while a reconnect waits: go now rather than at the end of the backoff.
    if (m_reconnectAttempt > 0 && m_status == Disconnected
        && (m_waitingForNetwork || m_reconnectTimer.isActive())) {
        m_reconnectTimer.stop();
        onReconnectTimer();
    }
}

bool ConnectionModel::networkIsDown()
{
    const QNetworkInformation *info = QNetworkInformation::instance();
    return info && info->reachability() == QNetworkInformation::Reachability::Disconnected;
}

void ConnectionModel::stopReconnecting()
{
    m_reconnectTimer.stop();
    m_waitingForNetwork = false;
    m_lastDropError.clear();
    setReconnectAttempt(0);
}

void ConnectionModel::setReconnectAttempt(int attempt)
{
    const bool was = reconnecting();
    m_reconnectAttempt = attempt;
    if (was != reconnecting())
        emit reconnectingChanged();
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
    const int mixedPort = SettingsModel::instance() ? SettingsModel::instance()->mixedPort()
                                                    : SettingsModel::kDefaultMixedPort;
    m_ipLookup->setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"),
                                       static_cast<quint16>(mixedPort)));

    QNetworkRequest request{QUrl(ipEndpoints().at(endpointIndex))};
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(8000);

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
    appendStatusLog(message);
}
