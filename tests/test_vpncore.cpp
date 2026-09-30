// SPDX-License-Identifier: GPL-3.0-or-later
//
// VpnCore turns a config into a running engine. These tests run it against a stub engine:
// what is accepted as a config, what is written to disk and with which permissions, and
// that an attempt still starting can be abandoned.

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
    void acceptsEveryShareLinkSchemeTheImporterAccepts_data();
    void acceptsEveryShareLinkSchemeTheImporterAccepts();
    void rejectsTheFreeProfileIndex();
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

void TestVpnCore::acceptsEveryShareLinkSchemeTheImporterAccepts_data()
{
    QTest::addColumn<QByteArray>("link");
    // hy:// and hy2:// were accepted on import and then refused at connect time.
    for (const char *scheme : {"vmess", "vless", "ss", "ssr", "trojan", "hysteria", "hysteria2",
                               "hy", "hy2", "tuic", "wg", "ssh"}) {
        QTest::newRow(scheme) << QByteArray(scheme) + "://user@example.com:443#node";
    }
}

void TestVpnCore::acceptsEveryShareLinkSchemeTheImporterAccepts()
{
    QFETCH(QByteArray, link);
    QVERIFY(VpnCore::isUsableConfig(link));
    // Subscriptions usually arrive as one base64 blob.
    QVERIFY(VpnCore::isUsableConfig(link.toBase64()));
}

void TestVpnCore::rejectsTheFreeProfileIndex()
{
    QVERIFY(!VpnCore::isUsableConfig("{\"data\":{\"profiles\":[{\"sublink\":\"https://x\"}]}}"));
    QVERIFY(!VpnCore::isUsableConfig("   "));
    QVERIFY(VpnCore::isUsableConfig("{\"outbounds\":[{\"type\":\"direct\"}]}"));
}

void TestVpnCore::anAttemptStillStartingCanBeAbandoned()
{
    VpnCore core;
    QSignalSpy disconnected(&core, &VpnCore::disconnected);
    QSignalSpy errors(&core, &VpnCore::errorOccurred);

    core.connectVpnWithConfig(QStringLiteral("vless://user@example.com:443#node"));
    QCOMPARE(core.coreStatus(), VpnCore::Starting);

    core.disconnectVpn();
    QCOMPARE(core.coreStatus(), VpnCore::Idle);
    QCOMPARE(disconnected.count(), 1);
    QCOMPARE(errors.count(), 0);

    // And a new attempt is accepted straight away.
    core.connectVpnWithConfig(QStringLiteral("vless://user@example.com:443#node"));
    QCOMPARE(core.coreStatus(), VpnCore::Starting);
    core.disconnectVpn();
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

    core.disconnectVpn();
}

QTEST_MAIN(TestVpnCore)
#include "test_vpncore.moc"
