// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings document is handed to the engine as a FILE, and the engine unmarshals it
// into a ZERO-VALUED options struct rather than merging it over its own defaults. Any field
// we leave out therefore arrives as "" or 0 — and for some fields that is not a default,
// it is a rejection: an empty remote-dns-address makes the engine refuse the entire config
// with "invalid server address", so the client cannot connect at all.
//
// These tests pin the fields whose zero value is invalid. They were written after exactly
// that bug was found by running a real engine build.

#include "../src/models/settingsmodel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

class TestEngineSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void carriesEveryFieldWhoseZeroValueIsInvalid();
    void autoDnsBecomesAConcreteAddress();
    void explicitDnsIsPreserved();
    void portsAreWithinRange();

private:
    QJsonObject build() const;
    SettingsModel *m_settings = nullptr;
};

void TestEngineSettings::initTestCase()
{
    // Keep the test off the developer's real configuration.
    QStandardPaths::setTestModeEnabled(true);
    m_settings = new SettingsModel(this);
}

QJsonObject TestEngineSettings::build() const
{
    const QByteArray json = m_settings->buildEngineSettingsJson();
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    Q_ASSERT(error.error == QJsonParseError::NoError);
    return doc.object();
}

void TestEngineSettings::carriesEveryFieldWhoseZeroValueIsInvalid()
{
    const QJsonObject o = build();

    // Strings that must not be empty. An empty DNS address is the one that cost a whole
    // release's worth of "it connects but nothing works" — the engine rejects the config.
    const QStringList requiredStrings{
        QStringLiteral("remote-dns-address"),
        QStringLiteral("direct-dns-address"),
        QStringLiteral("connection-test-url"),
        QStringLiteral("tun-implementation"),
        QStringLiteral("log-level"),
        QStringLiteral("ipv6-mode"),
    };
    for (const QString &key : requiredStrings) {
        QVERIFY2(o.contains(key), qPrintable(QStringLiteral("missing key: %1").arg(key)));
        QVERIFY2(!o.value(key).toString().isEmpty(),
                 qPrintable(QStringLiteral("empty value for: %1").arg(key)));
    }

    // Numbers that must not be zero.
    const QStringList requiredNumbers{
        QStringLiteral("mixed-port"),
        QStringLiteral("clash-api-port"),
        QStringLiteral("mtu"),
        QStringLiteral("url-test-interval"),
    };
    for (const QString &key : requiredNumbers) {
        QVERIFY2(o.contains(key), qPrintable(QStringLiteral("missing key: %1").arg(key)));
        QVERIFY2(o.value(key).toInt() > 0,
                 qPrintable(QStringLiteral("zero value for: %1").arg(key)));
    }

    // The control API has to be on, or the client can never observe readiness or traffic.
    QCOMPARE(o.value(QStringLiteral("enable-clash-api")).toBool(), true);
    QVERIFY(!o.value(QStringLiteral("web-secret")).toString().isEmpty());
}

void TestEngineSettings::autoDnsBecomesAConcreteAddress()
{
    m_settings->setRemoteDns(QStringLiteral("Auto"));
    m_settings->setDirectDns(QString());

    const QJsonObject o = build();
    // "Auto" must be resolved by us, because the engine will not fill it in on this path.
    QCOMPARE(o.value(QStringLiteral("remote-dns-address")).toString(), QStringLiteral("1.1.1.1"));
    QCOMPARE(o.value(QStringLiteral("direct-dns-address")).toString(), QStringLiteral("1.1.1.1"));
}

void TestEngineSettings::explicitDnsIsPreserved()
{
    m_settings->setRemoteDns(QStringLiteral("8.8.8.8"));
    m_settings->setDirectDns(QStringLiteral("9.9.9.9"));

    const QJsonObject o = build();
    QCOMPARE(o.value(QStringLiteral("remote-dns-address")).toString(), QStringLiteral("8.8.8.8"));
    QCOMPARE(o.value(QStringLiteral("direct-dns-address")).toString(), QStringLiteral("9.9.9.9"));
}

void TestEngineSettings::portsAreWithinRange()
{
    const QJsonObject o = build();
    for (const QString &key : {QStringLiteral("mixed-port"), QStringLiteral("clash-api-port")}) {
        const int port = o.value(key).toInt();
        QVERIFY2(port > 0 && port <= 65535, qPrintable(QStringLiteral("%1 = %2").arg(key).arg(port)));
    }
    // The two must not collide, or whichever binds second fails and the failure surfaces
    // as an unexplained "engine never became ready".
    QVERIFY(o.value(QStringLiteral("mixed-port")).toInt()
            != o.value(QStringLiteral("clash-api-port")).toInt());
}

QTEST_MAIN(TestEngineSettings)
#include "test_enginesettings.moc"
