// SPDX-License-Identifier: GPL-3.0-or-later
#include "vpncore.h"

#include "../core/CoreProcess.h"
#include "../core/PrivateFiles.h"
#include "../services/ClashApi.h"
#include "../services/SubscriptionParser.h"
#include "../platform/SystemProxy.h"
#include "settingsmodel.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

/// Written next to the engine's own state so a support request can just tar the directory.
const char *const kConfigFileName = "config.json";
const char *const kSettingsFileName = "engine-settings.json";

} // namespace

VpnCore::VpnCore(QObject *parent)
    : QObject(parent)
    , m_core(new CoreProcess(this))
{
    connect(m_core, &CoreProcess::ready, this, &VpnCore::handleEngineReady);
    connect(m_core, &CoreProcess::failed, this, &VpnCore::handleEngineFailure);
    connect(m_core, &CoreProcess::logLine, this, &VpnCore::engineLogLine);
    connect(m_core, &CoreProcess::controlApiPortDetected, this, [this](quint16 port) {
        // Follow the engine rather than our own request; see CoreProcess::controlApiPortDetected.
        ClashApi::setPort(port);
        emit statusMessage(QStringLiteral("Engine control API is on port %1").arg(port));
    });
    connect(m_core, &CoreProcess::stopped, this, &VpnCore::finishStopping);
}

VpnCore::~VpnCore()
{
    // The system proxy is machine-wide state; leaving it pointed at an engine that is about
    // to die would break every other application on the desktop. Synchronously: the process
    // is going away, and a queued revert would never run. The engine itself is stopped by
    // CoreProcess's destructor, after this.
    SystemProxy::instance().revert();
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

void VpnCore::connectVpnWithConfig(const QString &configContent)
{
    if (m_coreStatus == Running || m_coreStatus == Starting || m_coreStatus == Stopping) {
        // Returning quietly would leave the caller stuck on "Connecting" for ever.
        m_lastError = m_coreStatus == Stopping
            ? QStringLiteral("The previous connection is still shutting down")
            : QStringLiteral("A connection attempt is already in progress");
        emit errorOccurred(m_lastError);
        return;
    }

    if (configContent.trimmed().isEmpty()) {
        fail(QStringLiteral("Empty config content"));
        return;
    }

    launchEngine(configContent);
}

bool VpnCore::disconnectVpn()
{
    if (m_coreStatus == Stopping)
        return true;
    if (m_coreStatus != Running && m_coreStatus != Starting)
        return false;

    setCoreStatus(Stopping);
    emit statusMessage(QStringLiteral("Stopping VPN..."));

    // Order matters: put the desktop back BEFORE the engine goes away, so there is never a
    // window in which the system proxy points at a port with nothing behind it. Both steps
    // are asynchronous; disconnected() follows once the engine has actually exited.
    SystemProxy::instance().revertAsync(this, [this]() { m_core->stop(); });
    return true;
}

void VpnCore::finishStopping()
{
    if (m_coreStatus != Stopping)
        return;
    setCoreStatus(Idle);
    emit disconnected();
    emit statusMessage(QStringLiteral("VPN stopped"));
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
    if (!SubscriptionParser::isUsableConfig(config)) {
        fail(QStringLiteral("The server returned something that is not a usable VPN config"));
        return;
    }

    const QString workingDir = engineDir();
    // Owner-only: the config carries the user's node credentials.
    if (!PrivateFiles::ensureDir(workingDir)) {
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

    // Remove the previous run's files before writing, rather than the whole directory. A
    // stale config that failed to be overwritten must never be launched (it would silently
    // connect the user to the wrong servers), but the rest of the directory is the engine's
    // own cache — rule sets, geo databases — and wiping it made every connect download them
    // again.
    QFile::remove(configPath);
    QFile::remove(settingsPath);

    QString error;
    if (!PrivateFiles::write(configPath, config, &error)
        || !PrivateFiles::write(settingsPath,
                                QJsonDocument(engineSettings).toJson(QJsonDocument::Compact),
                                &error)) {
        fail(error);
        return;
    }

    emit statusMessage(QStringLiteral("Config written to %1").arg(configPath));

    // The proxy is only applied once the engine is actually listening.
    m_core->start(configPath, settingsPath, m_mixedPort);
}

void VpnCore::handleEngineReady()
{
    // Cancelled while the engine was coming up; the queued stop will finish the job.
    if (m_coreStatus != Starting)
        return;

    if (!m_wantSystemProxy) {
        markConnected();
        return;
    }

    SystemProxy &proxy = SystemProxy::instance();
    proxy.applyAsync(QStringLiteral("127.0.0.1"), m_mixedPort, this,
                     [this, &proxy](bool ok, const QString &error) {
        // Cancelled while the settings were being written. The cancel queued a revert
        // behind this apply, so the desktop is put back regardless.
        if (m_coreStatus != Starting)
            return;
        if (ok) {
            emit statusMessage(QStringLiteral("System proxy set to 127.0.0.1:%1 via %2")
                                   .arg(m_mixedPort)
                                   .arg(proxy.backendName()));
        } else {
            // Not fatal: the tunnel is up and usable by anything pointed at the local port.
            // Reporting it as an error would hide a working connection behind a red banner.
            emit statusMessage(error);
        }
        markConnected();
    });
}

void VpnCore::markConnected()
{
    setCoreStatus(Running);
    emit connected();
    emit statusMessage(QStringLiteral("VPN connected"));
}

void VpnCore::handleEngineFailure(const QString &error)
{
    // The engine died while we were stopping it anyway: that is the stop completing.
    if (m_coreStatus == Stopping) {
        finishStopping();
        return;
    }

    // Whatever went wrong, the desktop must not be left pointing at a port that is gone.
    SystemProxy::instance().revertAsync(this, nullptr);

    const bool wasConnected = m_coreStatus == Running;
    fail(error);
    if (wasConnected)
        emit disconnected();
}

void VpnCore::setCoreStatus(CoreStatus status)
{
    m_coreStatus = status;
}
