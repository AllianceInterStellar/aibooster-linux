// SPDX-License-Identifier: GPL-3.0-or-later
//
// SubscriptionParser decides what reaches the engine. The scheme list is checked against
// the engine's own parser table: a scheme accepted here but unknown to the engine is
// dropped silently there, and the client would report a tunnel that does not exist.

#include "../src/services/SubscriptionParser.h"

#include <QTest>

using namespace SubscriptionParser;

class TestSubscriptionParser : public QObject
{
    Q_OBJECT

private slots:
    void acceptsTheEnginesSchemes_data();
    void acceptsTheEnginesSchemes();
    void refusesSchemesTheEngineCannotParse_data();
    void refusesSchemesTheEngineCannotParse();
    void namesTheUnsupportedServerFormats();
    void decodesBase64Subscriptions();
    void leavesPlainBodiesAlone();
    void usableEngineJson();
    void refusesTheFreeProfileIndex();
    void acceptsWireGuardAndClash();
    void protocolNames();
};

void TestSubscriptionParser::acceptsTheEnginesSchemes_data()
{
    QTest::addColumn<QString>("link");
    for (const char *scheme : {"vmess", "vless", "trojan", "ss", "tuic", "hysteria", "hysteria2",
                               "hy2", "ssh", "wg", "wireguard", "awg", "warp", "socks", "mieru"}) {
        QTest::newRow(scheme) << QStringLiteral("%1://user@example.com:443#node").arg(scheme);
    }
}

void TestSubscriptionParser::acceptsTheEnginesSchemes()
{
    QFETCH(QString, link);
    QVERIFY(isShareLink(link));
    QVERIFY(isUsableConfig(link.toUtf8()));
    // Subscriptions usually arrive as one base64 blob.
    QVERIFY(isUsableConfig(link.toUtf8().toBase64()));
}

void TestSubscriptionParser::refusesSchemesTheEngineCannotParse_data()
{
    QTest::addColumn<QString>("link");
    // hy:// and ssr:// have no parser in the engine. https:// is a subscription URL here, and
    // direct:// is no tunnel at all.
    for (const char *scheme : {"hy", "ssr", "https", "http", "direct", "naive"})
        QTest::newRow(scheme) << QStringLiteral("%1://user@example.com:443#node").arg(scheme);
}

void TestSubscriptionParser::refusesSchemesTheEngineCannotParse()
{
    QFETCH(QString, link);
    QVERIFY(!isShareLink(link));
    QVERIFY(!isUsableConfig(link.toUtf8()));
}

void TestSubscriptionParser::namesTheUnsupportedServerFormats()
{
    QCOMPARE(unsupportedScheme(QStringLiteral("ssr://abc")), QStringLiteral("ssr"));
    QCOMPARE(unsupportedScheme(QStringLiteral("HY://abc")), QStringLiteral("hy"));
    QCOMPARE(unsupportedScheme(QStringLiteral("vless://abc")), QString());
    QCOMPARE(unsupportedScheme(QStringLiteral("https://abc")), QString());
}

void TestSubscriptionParser::decodesBase64Subscriptions()
{
    const QString body = QStringLiteral("vless://a@h1:443#one\ntrojan://b@h2:443#two\n");
    // URL-safe alphabet, no padding: both show up in the wild.
    QByteArray encoded = body.toUtf8().toBase64(QByteArray::Base64UrlEncoding
                                                | QByteArray::OmitTrailingEquals);
    QCOMPARE(decodeBody(QString::fromLatin1(encoded)).trimmed(), body.trimmed());
    QCOMPARE(shareLinks(QString::fromLatin1(encoded)).size(), 2);
    QVERIFY(decodeBase64(QStringLiteral("not base64 at all!")).isEmpty());
}

void TestSubscriptionParser::leavesPlainBodiesAlone()
{
    QCOMPARE(decodeBody(QStringLiteral("  vless://a@h:1#x  ")), QStringLiteral("vless://a@h:1#x"));
    QCOMPARE(decodeBody(QStringLiteral("{\"outbounds\":[]}")), QStringLiteral("{\"outbounds\":[]}"));
    // Short words must not be "decoded" into bytes.
    QCOMPARE(decodeBody(QStringLiteral("abcd")), QStringLiteral("abcd"));
}

void TestSubscriptionParser::usableEngineJson()
{
    QVERIFY(isUsableConfig("{\"outbounds\":[{\"type\":\"vless\",\"tag\":\"a\"}]}"));
    QVERIFY(isUsableConfig("{\"endpoints\":[{\"type\":\"wireguard\",\"tag\":\"w\"}]}"));
    QVERIFY(!isUsableConfig("{\"outbounds\":[]}"));
    // The engine wraps a lone outbound object itself.
    QVERIFY(isUsableConfig("{\"type\":\"vless\",\"server\":\"h\",\"server_port\":443}"));
    // The engine rejects top-level arrays outright.
    QVERIFY(!isUsableConfig("[{\"type\":\"vless\"}]"));
}

void TestSubscriptionParser::refusesTheFreeProfileIndex()
{
    QVERIFY(!isUsableConfig("{\"data\":{\"profiles\":[{\"sublink\":\"https://x\"}]}}"));
    QVERIFY(!isUsableConfig("   "));
    QVERIFY(!isUsableConfig("<html>502 Bad Gateway</html>"));
}

void TestSubscriptionParser::acceptsWireGuardAndClash()
{
    QVERIFY(isUsableConfig("[Interface]\nPrivateKey = x\n[Peer]\nEndpoint = h:51820\n"));
    QVERIFY(isUsableConfig("port: 7890\nproxies:\n  - name: a\n    type: ss\n    server: h\n"));
    QVERIFY(isUsableConfig("proxies:\n  - {name: a, type: vmess, server: h}\n"));
    QVERIFY(!isUsableConfig("proxies:\n"));
}

void TestSubscriptionParser::protocolNames()
{
    QCOMPARE(protocolName(QStringLiteral("hy2://x@h:1")), QStringLiteral("Hysteria2"));
    QCOMPARE(protocolName(QStringLiteral("VLESS://x@h:1")), QStringLiteral("VLESS"));
    QCOMPARE(protocolName(QStringLiteral("ssr://x")), QString());
    QCOMPARE(protocolName(QStringLiteral("no scheme")), QString());
}

QTEST_MAIN(TestSubscriptionParser)
#include "test_subscriptionparser.moc"
