// SPDX-License-Identifier: GPL-3.0-or-later
#include "CoreProcess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTcpSocket>

namespace {

/// How long we wait for the engine to bind its Clash API before declaring failure.
/// Cold start on a slow disk is comfortably under two seconds; 20 s is generous without
/// leaving the UI stuck on "Connecting" for ever if the engine silently dies.
constexpr int kReadyPollMs = 250;
constexpr int kReadyMaxAttempts = 80;

/// The engine writes its own diagnostics to stderr. We keep only the tail: a rejected
/// config can produce thousands of lines, and the useful part is always the last few.
constexpr int kStderrTailBytes = 4096;

QString existingExecutable(const QString &path)
{
    if (path.isEmpty())
        return QString();
    const QFileInfo info(path);
    if (info.exists() && info.isFile() && info.isExecutable())
        return info.absoluteFilePath();
    return QString();
}

} // namespace

CoreProcess::CoreProcess(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);

    connect(&m_process, &QProcess::readyReadStandardOutput, this, &CoreProcess::drainOutput);
    connect(&m_process, &QProcess::finished, this, &CoreProcess::handleFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_lastError = QStringLiteral("The VPN engine could not be started: %1")
                              .arg(m_process.errorString());
            setState(Failed);
            emit failed(m_lastError);
        }
    });

    m_readyTimer.setInterval(kReadyPollMs);
    connect(&m_readyTimer, &QTimer::timeout, this, &CoreProcess::pollReadiness);
}

CoreProcess::~CoreProcess()
{
    // Never leave an orphaned engine behind: it holds the proxy ports, so the next launch
    // would fail to bind and the user would be left with a half-working machine.
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(3000))
            m_process.kill();
        m_process.waitForFinished(1000);
    }
}

QStringList CoreProcess::searchedLocations()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return {
        QStringLiteral("$AIBOOSTER_CORE"),
        appDir + QStringLiteral("/aibooster-core"),
        QDir(appDir + QStringLiteral("/../libexec/aibooster")).absolutePath()
            + QStringLiteral("/aibooster-core"),
        QStringLiteral("/usr/libexec/aibooster/aibooster-core"),
        QStringLiteral("/usr/lib/aibooster/aibooster-core"),
        QStringLiteral("/usr/local/lib/aibooster/aibooster-core"),
        QStringLiteral("aibooster-core (on $PATH)"),
    };
}

QString CoreProcess::locateBinary()
{
    const QString appDir = QCoreApplication::applicationDirPath();

    const QStringList candidates = {
        qEnvironmentVariable("AIBOOSTER_CORE"),
        appDir + QStringLiteral("/aibooster-core"),
        QDir(appDir + QStringLiteral("/../libexec/aibooster")).absolutePath()
            + QStringLiteral("/aibooster-core"),
        QStringLiteral("/usr/libexec/aibooster/aibooster-core"),
        QStringLiteral("/usr/lib/aibooster/aibooster-core"),
        QStringLiteral("/usr/local/lib/aibooster/aibooster-core"),
    };

    for (const QString &candidate : candidates) {
        const QString resolved = existingExecutable(candidate);
        if (!resolved.isEmpty())
            return resolved;
    }

    return QStandardPaths::findExecutable(QStringLiteral("aibooster-core"));
}

void CoreProcess::start(const QString &configPath, const QString &settingsPath,
                        quint16 proxyPort)
{
    if (m_state == Starting || m_state == Running) {
        m_lastError = QStringLiteral("The VPN engine is already running");
        emit failed(m_lastError);
        return;
    }

    const QString binary = locateBinary();
    if (binary.isEmpty()) {
        m_lastError = QStringLiteral(
                          "The VPN engine binary (aibooster-core) was not found.\n\n"
                          "This client is the user interface only; the engine ships separately.\n"
                          "Looked in:\n  %1")
                          .arg(searchedLocations().join(QStringLiteral("\n  ")));
        setState(Failed);
        emit failed(m_lastError);
        return;
    }

    m_proxyPort = proxyPort;
    m_readyAttempts = 0;
    m_stderrTail.clear();
    m_lastError.clear();

    const QStringList args = {
        QStringLiteral("run"),
        QStringLiteral("-c"), configPath,
        QStringLiteral("-d"), settingsPath,
    };

    emit logLine(QStringLiteral("Starting engine: %1 %2").arg(binary, args.join(QLatin1Char(' '))));

    setState(Starting);
    // Run from the config's own directory: the engine resolves its relative working paths
    // (cache, rule sets) against the cwd, and inheriting ours would scatter them wherever
    // the launcher happened to be.
    m_process.setWorkingDirectory(QFileInfo(configPath).absolutePath());
    m_process.start(binary, args);
    m_readyTimer.start();
}

void CoreProcess::stop()
{
    m_readyTimer.stop();

    if (m_process.state() == QProcess::NotRunning) {
        setState(Stopped);
        return;
    }

    setState(Stopping);
    m_process.terminate();
    if (!m_process.waitForFinished(5000)) {
        emit logLine(QStringLiteral("Engine ignored SIGTERM; sending SIGKILL"));
        m_process.kill();
        m_process.waitForFinished(2000);
    }
    setState(Stopped);
}

void CoreProcess::pollReadiness()
{
    if (m_state != Starting) {
        m_readyTimer.stop();
        return;
    }

    // A dead child can never become ready. handleFinished() reports the real reason.
    if (m_process.state() == QProcess::NotRunning) {
        m_readyTimer.stop();
        return;
    }

    // Try both loopback families: the engine brings up one inbound per family and, when
    // the machine prefers IPv6, the v4 listener can lag or be absent entirely. Probing only
    // 127.0.0.1 would then report "never became ready" for a working tunnel.
    for (const QHostAddress &loopback : {QHostAddress(QHostAddress::LocalHost),
                                         QHostAddress(QHostAddress::LocalHostIPv6)}) {
        QTcpSocket probe;
        probe.connectToHost(loopback, m_proxyPort);
        if (probe.waitForConnected(150)) {
            probe.abort();
            m_readyTimer.stop();
            setState(Running);
            emit ready();
            return;
        }
    }

    if (++m_readyAttempts >= kReadyMaxAttempts) {
        m_readyTimer.stop();
        m_lastError = QStringLiteral(
                          "The VPN engine started but never opened its proxy port (%1), so "
                          "no traffic could go through it. Last output:\n%2")
                          .arg(m_proxyPort)
                          .arg(m_stderrTail.trimmed());
        stop();
        setState(Failed);
        emit failed(m_lastError);
    }
}

void CoreProcess::drainOutput()
{
    const QString chunk = QString::fromUtf8(m_process.readAllStandardOutput());
    if (chunk.isEmpty())
        return;

    m_stderrTail += chunk;
    if (m_stderrTail.size() > kStderrTailBytes)
        m_stderrTail = m_stderrTail.right(kStderrTailBytes);

    const QStringList lines = chunk.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines)
        emit logLine(line.trimmed());
}

void CoreProcess::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    m_readyTimer.stop();
    drainOutput();

    // An orderly stop() already moved us to Stopping; anything else is the engine dying
    // underneath us, which the caller must hear about so it can undo the system proxy.
    if (m_state == Stopping || m_state == Stopped) {
        setState(Stopped);
        emit stoppedCleanly();
        return;
    }

    m_lastError = status == QProcess::CrashExit
        ? QStringLiteral("The VPN engine crashed. Last output:\n%1").arg(m_stderrTail.trimmed())
        : QStringLiteral("The VPN engine exited unexpectedly (code %1). Last output:\n%2")
              .arg(exitCode)
              .arg(m_stderrTail.trimmed());

    setState(Failed);
    emit failed(m_lastError);
}

void CoreProcess::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}
