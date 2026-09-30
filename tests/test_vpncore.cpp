// SPDX-License-Identifier: GPL-3.0-or-later
//
// VpnCore turns a config into a running engine. These tests run it against a stub engine:
// that unusable configs never reach it, what is written to disk and with which
// permissions, and that an attempt still starting can be abandoned.

#include "../src/models/vpncore.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace {

constexpr QFileDevice::Permissions kGroupOrOther =
    QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
    | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;

/// An engine that starts and never opens its proxy port: "still connecting" for as long as
/// the test needs.
QString writeSlowEngine(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("slow-core"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write("#!/bin/sh\nexec sleep 60\n");
    file.close();
    QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ExeOwner);
    return path;
}

} // namespace

class TestVpnCore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void refusesAConfigTheEngineCannotUse();
    void anAttemptStillStartingCanBeAbandoned();
    void configFilesAreOwnerOnlyAndTheCacheSurvives();

private:
    QTemporaryDir m_dir;
};

void TestVpnCore::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QDir(VpnCore::engineDir()).removeRecursively();
    const QString engine = writeSlowEngine(m_dir);
    QVERIFY(!engine.isEmpty());
    qputenv("AIBOOSTER_CORE", engine.toUtf8());
}

void TestVpnCore::cleanupTestCase()
{
    qunsetenv("AIBOOSTER_CORE");
    QDir(VpnCore::engineDir()).removeRecursively();
}

void TestVpnCore::refusesAConfigTheEngineCannotUse()
{
    // What counts as usable is SubscriptionParser's call (see its tests); this checks the
    // connect path asks it, and never starts an engine for the free-node index.
    VpnCore core;
    QSignalSpy errors(&core, &VpnCore::errorOccurred);
    core.connectVpnWithConfig(
        QStringLiteral("{\"data\":{\"profiles\":[{\"sublink\":\"https://x\"}]}}"));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(core.coreStatus(), VpnCore::Error);
}

void TestVpnCore::anAttemptStillStartingCanBeAbandoned()
{
    VpnCore core;
    QSignalSpy disconnected(&core, &VpnCore::disconnected);
    QSignalSpy errors(&core, &VpnCore::errorOccurred);

    core.connectVpnWithConfig(QStringLiteral("vless://user@example.com:443#node"));
    QCOMPARE(core.coreStatus(), VpnCore::Starting);

    // Asynchronous: the UI thread is not held while the engine exits.
    QVERIFY(core.disconnectVpn());
    QCOMPARE(core.coreStatus(), VpnCore::Stopping);
    // Until it has gone, a new attempt is refused rather than racing the old engine.
    core.connectVpnWithConfig(QStringLiteral("vless://user@example.com:443#node"));
    QCOMPARE(errors.count(), 1);

    QVERIFY(disconnected.wait(10000));
    QCOMPARE(core.coreStatus(), VpnCore::Idle);
    QCOMPARE(disconnected.count(), 1);

    // And then a new attempt is accepted.
    core.connectVpnWithConfig(QStringLiteral("vless://user@example.com:443#node"));
    QCOMPARE(core.coreStatus(), VpnCore::Starting);
    QVERIFY(core.disconnectVpn());
    QVERIFY(disconnected.wait(10000));

    // Nothing to stop: says so, and no signal follows.
    QVERIFY(!core.disconnectVpn());
}

void TestVpnCore::configFilesAreOwnerOnlyAndTheCacheSurvives()
{
    // Something the engine cached on an earlier run, like a downloaded rule set.
    QVERIFY(QDir().mkpath(VpnCore::engineDir() + QStringLiteral("/data")));
    const QString cached = VpnCore::engineDir() + QStringLiteral("/data/geosite-cn.srs");
    QFile cache(cached);
    QVERIFY(cache.open(QIODevice::WriteOnly));
    cache.write("rules");
    cache.close();

    VpnCore core;
    core.connectVpnWithConfig(QStringLiteral("vless://secret-uuid@example.com:443#node"));
    QCOMPARE(core.coreStatus(), VpnCore::Starting);

    const QString config = VpnCore::runningConfigPath();
    QVERIFY(QFile::exists(config));
    QCOMPARE(QFile::permissions(config) & kGroupOrOther, QFileDevice::Permissions());
    QCOMPARE(QFile::permissions(VpnCore::engineDir()) & kGroupOrOther, QFileDevice::Permissions());
    QVERIFY2(QFile::exists(cached), "starting the engine wiped its cache");

    QSignalSpy disconnected(&core, &VpnCore::disconnected);
    QVERIFY(core.disconnectVpn());
    QVERIFY(disconnected.wait(10000));
}

QTEST_MAIN(TestVpnCore)
#include "test_vpncore.moc"
