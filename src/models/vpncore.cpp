// SPDX-License-Identifier: GPL-3.0-or-later
#include "vpncore.h"

#include "../core/CoreProcess.h"
#include "../platform/SystemProxy.h"
#include "settingsmodel.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

namespace {

/// Written next to the engine's own state so a support request can just tar the directory.
const char *const kConfigFileName = "config.json";
const char *const kSettingsFileName = "engine-settings.json";

bool writeFile(const QString &path, const QByteArray &contents, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(contents) != contents.size()) {
        *error = QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.close();
    return true;
}

} // namespace

VpnCore::VpnCore(QObject *parent)
    : QObject(parent)
    , m_core(new CoreProcess(this))
{
    connect(m_core, &CoreProcess::ready, this, &VpnCore::handleEngineReady);
    connect(m_core, &CoreProcess::failed, this, &VpnCore::handleEngineFailure);
    connect(m_core, &CoreProcess::logLine, this, &VpnCore::statusMessage);
    connect(m_core, &CoreProcess::stoppedCleanly, this, [this]() {
        if (m_coreStatus == Running || m_coreStatus == Stopping)
            emit statusMessage(QStringLiteral("Engine stopped"));
    });
}

VpnCore::~VpnCore()
{
    // The system proxy is machine-wide state; leaving it pointed at an engine that is about
    // to die would break every other application on the desktop.
    teardown();
}

QString VpnCore::engineDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/engine");
}

QString VpnCore::runningConfigPath()
{
    return engineDir() + QLatin1Char('/') + QLatin1String(kConfigFileName);
}

void VpnCore::fail(const QString &message)
{
    m_lastError = message;
    setCoreStatus(Error);
    emit errorOccurred(message);
}

void VpnCore::connectVpn(const QString &subscriptionUrl)
{
    if (m_coreStatus == Running || m_coreStatus == Starting || m_coreStatus == Downloading) {
        // Returning quietly would leave the caller stuck on "Connecting" for ever.
        m_lastError = QStringLiteral("A connection attempt is already in progress");
        emit errorOccurred(m_lastError);
        return;
    }

    setCoreStatus(Downloading);
    emit statusMessage(QStringLiteral("Downloading subscription..."));
    downloadSubscription(subscriptionUrl);
}

void VpnCore::connectVpnWithConfig(const QString &configContent)
{
    if (m_coreStatus == Running || m_coreStatus == Starting || m_coreStatus == Downloading) {
        m_lastError = QStringLiteral("A connection attempt is already in progress");
        emit errorOccurred(m_lastError);
        return;
    }

    if (configContent.trimmed().isEmpty()) {
        fail(QStringLiteral("Empty config content"));
        return;
    }

    launchEngine(configContent);
}

void VpnCore::disconnectVpn()
{
    if (m_coreStatus != Running && m_coreStatus != Starting)
        return;

    setCoreStatus(Stopping);
    emit statusMessage(QStringLiteral("Stopping VPN..."));

    teardown();

    setCoreStatus(Idle);
    emit disconnected();
    emit statusMessage(QStringLiteral("VPN stopped"));
}

void VpnCore::teardown()
{
    // Order matters: put the desktop back BEFORE the engine goes away, so there is never a
    // window in which the system proxy points at a port with nothing behind it.
    SystemProxy::instance().revert();
    m_core->stop();
}

void VpnCore::downloadSubscription(const QString &url)
{
    QString cleanUrl = url;
    const int hashIndex = cleanUrl.indexOf(QLatin1Char('#'));
    if (hashIndex >= 0)
        cleanUrl = cleanUrl.left(hashIndex);

    QNetworkRequest request{QUrl{cleanUrl}};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AiBooster/2.0"));
    request.setTransferTimeout(30000);

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("Download failed: %1").arg(reply->errorString()));
            return;
        }

        const QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            fail(QStringLiteral("Empty subscription content"));
            return;
        }

        emit statusMessage(QStringLiteral("Subscription downloaded (%1 bytes)").arg(data.size()));
        launchEngine(QString::fromUtf8(data));
    });
}

/// True when the payload is something the engine can actually build a tunnel from:
/// an engine JSON object carrying a non-empty "outbounds" array, or a subscription body
/// (share links, or base64 that decodes to them). Deliberately rejects the `?type=free`
/// profile INDEX, whose {"data":{"profiles":[…]}} shape the engine silently discards.
bool VpnCore::isUsableConfig(const QByteArray &configData)
{
    const QByteArray trimmed = configData.trimmed();
    if (trimmed.isEmpty())
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
    if (doc.isObject())
        return !doc.object().value(QStringLiteral("outbounds")).toArray().isEmpty();

    auto hasShareLink = [](const QByteArray &text) {
        static const char *const kSchemes[] = {"vmess://", "vless://", "ss://", "ssr://",
                                               "trojan://", "hysteria://", "hysteria2://",
                                               "tuic://", "wg://", "warp://", "ssh://"};
        for (const char *scheme : kSchemes)
            if (text.contains(scheme))
                return true;
        return false;
    };

    if (hasShareLink(trimmed))
        return true;
    // Subscriptions are frequently one long base64 blob.
    const QByteArray decoded = QByteArray::fromBase64(trimmed);
    return !decoded.isEmpty() && hasShareLink(decoded);
}

void VpnCore::launchEngine(const QString &configContent)
{
    setCoreStatus(Starting);
    emit statusMessage(QStringLiteral("Preparing VPN engine..."));

    const QByteArray config = configContent.toUtf8();

    // Reject anything that cannot possibly become a tunnel BEFORE handing it over. The
    // engine's failure paths are not always distinguishable from a clean start, and for a
    // VPN client "connected with no tunnel" is the worst possible outcome: the user believes
    // they are protected while their traffic is in the clear.
    if (!isUsableConfig(config)) {
        fail(QStringLiteral("The server returned something that is not a usable VPN config"));
        return;
    }

    const QString workingDir = engineDir();
    // Start from a clean directory: a stale config from a previous profile that failed to be
    // overwritten would silently connect the user to the wrong servers.
    QDir(workingDir).removeRecursively();
    if (!QDir().mkpath(workingDir)) {
        fail(QStringLiteral("Could not create the engine directory at %1").arg(workingDir));
        return;
    }

    SettingsModel *settings = SettingsModel::instance();
    QJsonObject engineSettings =
        QJsonDocument::fromJson(settings ? settings->buildEngineSettingsJson() : QByteArray("{}"))
            .object();

    // We drive the desktop's proxy ourselves rather than letting the engine do it. The
    // engine has no way to restore the previous settings if it is killed, and on this
    // platform that leaves every application pointed at a dead port. SystemProxy records the
    // old values on disk first and replays them on the next launch. See SystemProxy.h.
    m_wantSystemProxy = settings ? settings->systemProxy() : true;
    engineSettings.insert(QStringLiteral("set-system-proxy"), false);

    m_mixedPort = static_cast<quint16>(
        engineSettings.value(QStringLiteral("mixed-port")).toInt(SettingsModel::kDefaultMixedPort));
    // Readiness is the proxy inbound, not the Clash API — see CoreProcess::start.

    const QString configPath = runningConfigPath();
    const QString settingsPath = workingDir + QLatin1Char('/') + QLatin1String(kSettingsFileName);

    QString error;
    if (!writeFile(configPath, config, &error)
        || !writeFile(settingsPath,
                      QJsonDocument(engineSettings).toJson(QJsonDocument::Compact), &error)) {
        fail(error);
        return;
    }

    emit statusMessage(QStringLiteral("Config written to %1").arg(configPath));

    // The proxy is only applied once the engine is actually listening.
    m_core->start(configPath, settingsPath, m_mixedPort);
}

void VpnCore::handleEngineReady()
{
    if (m_wantSystemProxy) {
        SystemProxy &proxy = SystemProxy::instance();
        if (proxy.apply(QStringLiteral("127.0.0.1"), m_mixedPort)) {
            emit statusMessage(QStringLiteral("System proxy set to 127.0.0.1:%1 via %2")
                                   .arg(m_mixedPort)
                                   .arg(proxy.backendName()));
        } else {
            // Not fatal: the tunnel is up and usable by anything pointed at the local port.
            // Reporting it as an error would hide a working connection behind a red banner.
            emit statusMessage(proxy.lastError());
        }
    }

    setCoreStatus(Running);
    emit connected();
    emit statusMessage(QStringLiteral("VPN connected"));
}

void VpnCore::handleEngineFailure(const QString &error)
{
    // Whatever went wrong, the desktop must not be left pointing at a port that is gone.
    SystemProxy::instance().revert();

    const bool wasConnected = m_coreStatus == Running;
    fail(error);
    if (wasConnected)
        emit disconnected();
}

void VpnCore::setCoreStatus(CoreStatus status)
{
    m_coreStatus = status;
}
