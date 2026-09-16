// SPDX-License-Identifier: GPL-3.0-or-later
//
// Exercises the engine supervision against a STUB engine, so the whole connect path can be
// tested without the real (GPL-3.0, separately distributed) engine binary present.

#include "../src/core/CoreProcess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

namespace {

/// Writes an executable shell script and returns its path.
QString writeScript(const QDir &dir, const QString &name, const QString &body)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    file.write(body.toUtf8());
    file.close();
    QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ExeOwner
                              | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    return path;
}

constexpr quint16 kTestProxyPort = 39751;

} // namespace

class TestCoreProcess : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void locateBinaryHonoursEnvironmentOverride();
    void missingBinaryFailsWithTheSearchedPaths();
    void readinessTimeoutCoversARealisticSlowStart();
    void readyOnlyAfterTheProxyPortIsOpen();
    void readyWhenTheEngineBindsOnlyIPv6Loopback();
    void stopTerminatesTheEngine();
    void engineExitingEarlyIsReportedAsFailure();

private:
    QTemporaryDir m_dir;
    QString m_configPath;
    QString m_settingsPath;
};

void TestCoreProcess::init()
{
    QVERIFY(m_dir.isValid());
    const QDir dir(m_dir.path());
    m_configPath = dir.filePath(QStringLiteral("config.json"));
    m_settingsPath = dir.filePath(QStringLiteral("settings.json"));
    QFile c(m_configPath);
    QVERIFY(c.open(QIODevice::WriteOnly));
    c.write("{\"outbounds\":[]}");
    c.close();
    QFile s(m_settingsPath);
    QVERIFY(s.open(QIODevice::WriteOnly));
    s.write("{}");
    s.close();
}

void TestCoreProcess::locateBinaryHonoursEnvironmentOverride()
{
    const QString stub = writeScript(QDir(m_dir.path()), QStringLiteral("stub-core"),
                                     QStringLiteral("#!/bin/sh\nexit 0\n"));
    QVERIFY(!stub.isEmpty());

    qputenv("AIBOOSTER_CORE", stub.toUtf8());
    QCOMPARE(CoreProcess::locateBinary(), stub);

    // An override pointing at something that is not there yields nothing at all. It must
    // NOT fall through to a system path: answering a request for one engine with a
    // different one is how "it connected to the wrong engine" happens, and on a machine
    // with the package installed that fallback would silently succeed.
    qputenv("AIBOOSTER_CORE", QByteArray("/nonexistent/aibooster-core"));
    QVERIFY(CoreProcess::locateBinary().isEmpty());
    qunsetenv("AIBOOSTER_CORE");
}

void TestCoreProcess::missingBinaryFailsWithTheSearchedPaths()
{
    // Restored by the destructor, so a failing assertion — which aborts the rest of this
    // function — cannot leak a broken PATH into every test that follows. That is exactly
    // what happened once: one failure here left PATH=/nonexistent behind and the next three
    // tests failed because their stub engines could no longer find a shell.
    struct PathGuard {
        QByteArray saved = qgetenv("PATH");
        ~PathGuard() { qputenv("PATH", saved); }
    } pathGuard;

    qputenv("AIBOOSTER_CORE", QByteArray("/nonexistent/aibooster-core"));
    qputenv("PATH", QByteArray("/nonexistent"));

    CoreProcess core;
    QSignalSpy failed(&core, &CoreProcess::failed);
    core.start(m_configPath, m_settingsPath, kTestProxyPort);

    QCOMPARE(failed.count(), 1);
    QCOMPARE(core.state(), CoreProcess::Failed);
    const QString message = failed.first().first().toString();
    // The message has to be actionable, not just "failed".
    QVERIFY(message.contains(QStringLiteral("aibooster-core")));
    QVERIFY(message.contains(QStringLiteral("$AIBOOSTER_CORE")));
    QVERIFY(message.contains(QStringLiteral("/usr/libexec/aibooster/aibooster-core")));

    qunsetenv("AIBOOSTER_CORE");
}

void TestCoreProcess::readinessTimeoutCoversARealisticSlowStart()
{
    // Not a style check — a measurement. A real subscription (fifteen outbounds, WireGuard
    // chains, the engine's own reachability tests) took 28 seconds to open its proxy port on
    // an ordinary machine. The limit was 20 s, so the client gave up first and told the user
    // the connection had failed while the engine was still coming up correctly.
    //
    // Anything at or below that measurement ships that bug again.
    QVERIFY2(CoreProcess::kReadinessTimeoutMs > 28000,
             "the readiness timeout is shorter than a real subscription has been measured to "
             "need; a working config would be reported as a failure");
    // And generous enough to have real headroom over it.
    QVERIFY(CoreProcess::kReadinessTimeoutMs >= 60000);
}

void TestCoreProcess::readyOnlyAfterTheProxyPortIsOpen()
{
    // The stub stays alive but does not bind the port for the first two seconds. A
    // supervisor that equates "process started" with "engine ready" passes this test at
    // once and is wrong: the real engine binds its listeners only after parsing the config.
    const QString stub = writeScript(
        QDir(m_dir.path()), QStringLiteral("late-core"),
        QStringLiteral("#!/bin/sh\n"
                       "sleep 2\n"
                       "exec python3 -c \"import socket,time;"
                       "s=socket.socket();s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);"
                       "s.bind(('127.0.0.1',%1));s.listen(5);\n"
                       "print('listening',flush=True)\n"
                       "time.sleep(60)\"\n")
            .arg(kTestProxyPort));
    QVERIFY(!stub.isEmpty());
    qputenv("AIBOOSTER_CORE", stub.toUtf8());

    CoreProcess core;
    QSignalSpy ready(&core, &CoreProcess::ready);
    QSignalSpy failed(&core, &CoreProcess::failed);

    core.start(m_configPath, m_settingsPath, kTestProxyPort);

    // Not ready while the port is still closed.
    QTest::qWait(700);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(core.state(), CoreProcess::Starting);

    QVERIFY(ready.wait(15000));
    QCOMPARE(core.state(), CoreProcess::Running);
    QCOMPARE(failed.count(), 0);

    core.stop();
    qunsetenv("AIBOOSTER_CORE");
}

void TestCoreProcess::readyWhenTheEngineBindsOnlyIPv6Loopback()
{
    // The real engine brings up one inbound per address family, and the v4 listener can be
    // absent or lag behind. A probe that only tries 127.0.0.1 reports "never became ready"
    // for a tunnel that is up and carrying traffic — so bind ::1 alone and require success.
    const quint16 port = kTestProxyPort + 5;
    const QString stub = writeScript(
        QDir(m_dir.path()), QStringLiteral("v6-core"),
        QStringLiteral("#!/bin/sh\n"
                       "exec python3 -c \"import socket,time;"
                       "s=socket.socket(socket.AF_INET6);"
                       "s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);"
                       "s.bind(('::1',%1));s.listen(5);time.sleep(120)\"\n")
            .arg(port));
    QVERIFY(!stub.isEmpty());
    qputenv("AIBOOSTER_CORE", stub.toUtf8());

    CoreProcess core;
    QSignalSpy ready(&core, &CoreProcess::ready);
    core.start(m_configPath, m_settingsPath, port);
    QVERIFY2(ready.wait(15000), "an IPv6-only proxy inbound was not detected as ready");

    core.stop();
    qunsetenv("AIBOOSTER_CORE");
}

void TestCoreProcess::stopTerminatesTheEngine()
{
    const QString stub = writeScript(
        QDir(m_dir.path()), QStringLiteral("held-core"),
        QStringLiteral("#!/bin/sh\n"
                       "exec python3 -c \"import socket,time;"
                       "s=socket.socket();s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);"
                       "s.bind(('127.0.0.1',%1));s.listen(5);time.sleep(120)\"\n")
            .arg(kTestProxyPort + 1));
    qputenv("AIBOOSTER_CORE", stub.toUtf8());

    CoreProcess core;
    QSignalSpy ready(&core, &CoreProcess::ready);
    core.start(m_configPath, m_settingsPath, kTestProxyPort + 1);
    QVERIFY(ready.wait(15000));

    core.stop();
    QCOMPARE(core.state(), CoreProcess::Stopped);

    // The port must be free afterwards; an orphaned engine would hold it and the next
    // connection attempt would fail to bind.
    QTcpServer rebind;
    QVERIFY2(rebind.listen(QHostAddress::LocalHost, kTestProxyPort + 1),
             "the stub engine outlived stop() and is still holding the proxy port");
    qunsetenv("AIBOOSTER_CORE");
}

void TestCoreProcess::engineExitingEarlyIsReportedAsFailure()
{
    // A config the engine rejects makes it print a diagnostic and exit. That must surface
    // as an error carrying the output — not as a silent "connected" with no tunnel.
    const QString stub = writeScript(QDir(m_dir.path()), QStringLiteral("rejecting-core"),
                                     QStringLiteral("#!/bin/sh\n"
                                                    "echo 'FATAL: decode config: invalid outbound'\n"
                                                    "exit 1\n"));
    qputenv("AIBOOSTER_CORE", stub.toUtf8());

    CoreProcess core;
    QSignalSpy failed(&core, &CoreProcess::failed);
    QSignalSpy ready(&core, &CoreProcess::ready);
    core.start(m_configPath, m_settingsPath, kTestProxyPort + 2);

    QVERIFY(failed.wait(10000));
    QCOMPARE(ready.count(), 0);
    QCOMPARE(core.state(), CoreProcess::Failed);
    QVERIFY2(failed.first().first().toString().contains(QStringLiteral("invalid outbound")),
             "the engine's own diagnostic must reach the user");
    qunsetenv("AIBOOSTER_CORE");
}

QTEST_MAIN(TestCoreProcess)
#include "test_coreprocess.moc"
