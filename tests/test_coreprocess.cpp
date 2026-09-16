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

constexpr quint16 kTestClashPort = 39751;

} // namespace

class TestCoreProcess : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void locateBinaryHonoursEnvironmentOverride();
    void missingBinaryFailsWithTheSearchedPaths();
    void readyOnlyAfterTheControlPortIsOpen();
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

    // An override pointing at something that is not there must not be returned: silently
    // using a stale path is how "it connected to the wrong engine" happens.
    qputenv("AIBOOSTER_CORE", QByteArray("/nonexistent/aibooster-core"));
    QVERIFY(CoreProcess::locateBinary() != QStringLiteral("/nonexistent/aibooster-core"));
    qunsetenv("AIBOOSTER_CORE");
}

void TestCoreProcess::missingBinaryFailsWithTheSearchedPaths()
{
    qputenv("AIBOOSTER_CORE", QByteArray("/nonexistent/aibooster-core"));
    // Also keep $PATH from accidentally supplying one on a developer machine.
    const QByteArray savedPath = qgetenv("PATH");
    qputenv("PATH", QByteArray("/nonexistent"));

    CoreProcess core;
    QSignalSpy failed(&core, &CoreProcess::failed);
    core.start(m_configPath, m_settingsPath, kTestClashPort);

    QCOMPARE(failed.count(), 1);
    QCOMPARE(core.state(), CoreProcess::Failed);
    const QString message = failed.first().first().toString();
    // The message has to be actionable, not just "failed".
    QVERIFY(message.contains(QStringLiteral("aibooster-core")));
    QVERIFY(message.contains(QStringLiteral("$AIBOOSTER_CORE")));
    QVERIFY(message.contains(QStringLiteral("/usr/libexec/aibooster/aibooster-core")));

    qputenv("PATH", savedPath);
    qunsetenv("AIBOOSTER_CORE");
}

void TestCoreProcess::readyOnlyAfterTheControlPortIsOpen()
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
            .arg(kTestClashPort));
    QVERIFY(!stub.isEmpty());
    qputenv("AIBOOSTER_CORE", stub.toUtf8());

    CoreProcess core;
    QSignalSpy ready(&core, &CoreProcess::ready);
    QSignalSpy failed(&core, &CoreProcess::failed);

    core.start(m_configPath, m_settingsPath, kTestClashPort);

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

void TestCoreProcess::stopTerminatesTheEngine()
{
    const QString stub = writeScript(
        QDir(m_dir.path()), QStringLiteral("held-core"),
        QStringLiteral("#!/bin/sh\n"
                       "exec python3 -c \"import socket,time;"
                       "s=socket.socket();s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);"
                       "s.bind(('127.0.0.1',%1));s.listen(5);time.sleep(120)\"\n")
            .arg(kTestClashPort + 1));
    qputenv("AIBOOSTER_CORE", stub.toUtf8());

    CoreProcess core;
    QSignalSpy ready(&core, &CoreProcess::ready);
    core.start(m_configPath, m_settingsPath, kTestClashPort + 1);
    QVERIFY(ready.wait(15000));

    core.stop();
    QCOMPARE(core.state(), CoreProcess::Stopped);

    // The port must be free afterwards; an orphaned engine would hold it and the next
    // connection attempt would fail to bind.
    QTcpServer rebind;
    QVERIFY2(rebind.listen(QHostAddress::LocalHost, kTestClashPort + 1),
             "the stub engine outlived stop() and is still holding the control port");
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
    core.start(m_configPath, m_settingsPath, kTestClashPort + 2);

    QVERIFY(failed.wait(10000));
    QCOMPARE(ready.count(), 0);
    QCOMPARE(core.state(), CoreProcess::Failed);
    QVERIFY2(failed.first().first().toString().contains(QStringLiteral("invalid outbound")),
             "the engine's own diagnostic must reach the user");
    qunsetenv("AIBOOSTER_CORE");
}

QTEST_MAIN(TestCoreProcess)
#include "test_coreprocess.moc"
