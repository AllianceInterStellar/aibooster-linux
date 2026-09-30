// SPDX-License-Identifier: GPL-3.0-or-later
//
// Auto-reconnect, end to end against a stub engine the test can "crash" on demand. The
// properties that matter: a tunnel that drops comes back by itself, a connect that never
// worked is not retried in a loop, and nothing reconnects behind the user's back after
// they disconnected or turned the setting off.

#include "../src/models/connectionmodel.h"
#include "../src/models/profilelistmodel.h"
#include "../src/models/settingsmodel.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace {

constexpr quint16 kPort = 39811;

QString writeScript(const QTemporaryDir &dir, const QString &name, const QString &body)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write(body.toUtf8());
    file.close();
    QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ExeOwner);
    return path;
}

} // namespace

class TestConnectionModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void aDroppedTunnelReconnectsByItself();
    void aFirstConnectThatFailsIsNotRetried();
    void noReconnectAfterTheUserDisconnects();
    void noReconnectWhenTheSettingIsOff();
    void dismissingTheBannerStopsReconnecting();

private:
    void connectAndWait();
    void crashEngine();

    QTemporaryDir m_dir;
    QString m_dieFile;
    QString m_healthyEngine;
    QString m_brokenEngine;
    SettingsModel *m_settings = nullptr;
    ProfileListModel *m_profiles = nullptr;
    ConnectionModel *m_conn = nullptr;
};

void TestConnectionModel::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    m_dieFile = m_dir.filePath(QStringLiteral("die"));

    // Binds the proxy port like a real engine, and exits with an error — a crash, as far as
    // the client can tell — once the die file appears.
    m_healthyEngine = writeScript(
        m_dir, QStringLiteral("engine"),
        QStringLiteral("#!/bin/sh\n"
                       "exec python3 -c \"import os,socket,sys,time;"
                       "s=socket.socket();s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);"
                       "s.bind(('127.0.0.1',%1));s.listen(5);\n"
                       "while not os.path.exists('%2'): time.sleep(0.05)\n"
                       "print('FATAL engine crashed',flush=True);sys.exit(2)\"\n")
            .arg(kPort)
            .arg(m_dieFile));
    m_brokenEngine = writeScript(m_dir, QStringLiteral("broken"),
                                 QStringLiteral("#!/bin/sh\necho 'FATAL bad config'\nexit 1\n"));
    QVERIFY(!m_healthyEngine.isEmpty() && !m_brokenEngine.isEmpty());

    m_settings = new SettingsModel(this);
    m_settings->setSystemProxy(false);   // never touch the desktop from a test
    m_settings->setMixedPort(kPort);

    m_profiles = new ProfileListModel(this);
    m_profiles->addProfile(QStringLiteral("vless://user@example.com:443#stub"));
    QVERIFY(!m_profiles->activeProfileContent().isEmpty());
}

void TestConnectionModel::init()
{
    QFile::remove(m_dieFile);
    qputenv("AIBOOSTER_CORE", m_healthyEngine.toUtf8());
    m_settings->setAutoReconnect(true);
    m_conn = new ConnectionModel(this);
}

void TestConnectionModel::cleanup()
{
    if (m_conn->status() == ConnectionModel::Connected)
        m_conn->toggleConnection();
    QTRY_COMPARE_WITH_TIMEOUT(m_conn->status(), ConnectionModel::Disconnected, 10000);
    delete m_conn;
    m_conn = nullptr;
}

void TestConnectionModel::connectAndWait()
{
    m_conn->toggleConnection();
    QTRY_COMPARE_WITH_TIMEOUT(m_conn->status(), ConnectionModel::Connected, 15000);
}

void TestConnectionModel::crashEngine()
{
    QFile die(m_dieFile);
    QVERIFY(die.open(QIODevice::WriteOnly));
    die.close();
    QTRY_COMPARE_WITH_TIMEOUT(m_conn->status(), ConnectionModel::Disconnected, 10000);
}

void TestConnectionModel::aDroppedTunnelReconnectsByItself()
{
    connectAndWait();
    crashEngine();
    QVERIFY(m_conn->reconnecting());
    QVERIFY2(m_conn->sessionNotice().contains(QStringLiteral("Reconnecting")),
             qPrintable(m_conn->sessionNotice()));

    // The engine is healthy again by the time the first retry (2 s) runs.
    QFile::remove(m_dieFile);
    QTRY_COMPARE_WITH_TIMEOUT(m_conn->status(), ConnectionModel::Connected,
                              ConnectionModel::reconnectDelaysSeconds().first() * 1000 + 15000);
    QVERIFY(!m_conn->reconnecting());
    QVERIFY(m_conn->sessionNotice().isEmpty());
}

void TestConnectionModel::aFirstConnectThatFailsIsNotRetried()
{
    qputenv("AIBOOSTER_CORE", m_brokenEngine.toUtf8());
    m_conn->toggleConnection();
    QTRY_VERIFY_WITH_TIMEOUT(m_conn->sessionNotice().contains(QStringLiteral("Could not connect")),
                             10000);
    QCOMPARE(m_conn->status(), ConnectionModel::Disconnected);
    QVERIFY(!m_conn->reconnecting());
}

void TestConnectionModel::noReconnectAfterTheUserDisconnects()
{
    connectAndWait();
    m_conn->toggleConnection();
    QTRY_COMPARE_WITH_TIMEOUT(m_conn->status(), ConnectionModel::Disconnected, 10000);
    QTest::qWait(ConnectionModel::reconnectDelaysSeconds().first() * 1000 + 1000);
    QCOMPARE(m_conn->status(), ConnectionModel::Disconnected);
    QVERIFY(!m_conn->reconnecting());
}

void TestConnectionModel::noReconnectWhenTheSettingIsOff()
{
    m_settings->setAutoReconnect(false);
    connectAndWait();
    crashEngine();
    QVERIFY(!m_conn->reconnecting());
    QVERIFY(m_conn->sessionNotice().contains(QStringLiteral("Could not connect")));
}

void TestConnectionModel::dismissingTheBannerStopsReconnecting()
{
    connectAndWait();
    crashEngine();
    QVERIFY(m_conn->reconnecting());
    m_conn->dismissSessionNotice();
    QVERIFY(!m_conn->reconnecting());
    QFile::remove(m_dieFile);
    QTest::qWait(ConnectionModel::reconnectDelaysSeconds().first() * 1000 + 1000);
    QCOMPARE(m_conn->status(), ConnectionModel::Disconnected);
}

QTEST_MAIN(TestConnectionModel)
#include "test_connectionmodel.moc"
