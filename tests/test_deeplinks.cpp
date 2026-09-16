// SPDX-License-Identifier: GPL-3.0-or-later
//
// Deep links used to be matched against a hardcoded list of client application names. They
// are now matched by shape — any scheme carrying a `url=` parameter. These tests exist to
// prove that swap changed nothing for users: every scheme the old list enumerated is
// asserted here by name, so a link that worked before still works.

#include "../src/models/profilelistmodel.h"

#include <QTest>

class TestDeepLinks : public QObject
{
    Q_OBJECT

private slots:
    void everySchemeTheOldListEnumeratedStillWorks_data();
    void everySchemeTheOldListEnumeratedStillWorks();
    void schemesTheOldListNeverKnewAlsoWork();
    void importPathSegmentIsStripped();
    void nameParameterIsExtracted();
    void directProtocolLinksAreNotDeepLinks_data();
    void directProtocolLinksAreNotDeepLinks();
    void plainSubscriptionUrlsAreNotDeepLinks();
    void aDeepLinkWithoutAUrlIsReportedAsMalformed();
    void rawTextIsNotADeepLink();
};

void TestDeepLinks::everySchemeTheOldListEnumeratedStillWorks_data()
{
    QTest::addColumn<QString>("scheme");
    // Exactly the list that used to be hardcoded in deepLinkPrefixes().
    for (const char *s : {"hiddify", "v2ray", "v2rayn", "v2rayng", "clash", "clashmeta", "sing-box"})
        QTest::newRow(s) << QString::fromLatin1(s);
}

void TestDeepLinks::everySchemeTheOldListEnumeratedStillWorks()
{
    QFETCH(QString, scheme);
    const QString link = scheme + QStringLiteral("://install-config?url=https://example.com/sub");
    bool malformed = true;
    const QString url = ProfileListModel::subscriptionUrlFromDeepLink(link, nullptr, &malformed);
    QCOMPARE(url, QStringLiteral("https://example.com/sub"));
    QVERIFY(!malformed);
}

void TestDeepLinks::schemesTheOldListNeverKnewAlsoWork()
{
    // The point of matching by shape: a client that did not exist when the list was written.
    const QString url = ProfileListModel::subscriptionUrlFromDeepLink(
        QStringLiteral("aibooster://install?url=https://example.com/s"));
    QCOMPARE(url, QStringLiteral("https://example.com/s"));
}

void TestDeepLinks::importPathSegmentIsStripped()
{
    for (const QString &link : {QStringLiteral("clash://import/?url=https://example.com/a"),
                                QStringLiteral("clash://import?url=https://example.com/a")}) {
        QCOMPARE(ProfileListModel::subscriptionUrlFromDeepLink(link),
                 QStringLiteral("https://example.com/a"));
    }
}

void TestDeepLinks::nameParameterIsExtracted()
{
    QString name;
    const QString url = ProfileListModel::subscriptionUrlFromDeepLink(
        QStringLiteral("hiddify://import?url=https://example.com/s&name=Tokyo"), &name);
    QCOMPARE(url, QStringLiteral("https://example.com/s"));
    QCOMPARE(name, QStringLiteral("Tokyo"));
}

void TestDeepLinks::directProtocolLinksAreNotDeepLinks_data()
{
    QTest::addColumn<QString>("link");
    for (const char *s : {"vmess://abc", "vless://abc", "ss://abc", "ssr://abc", "trojan://abc",
                          "hysteria://abc", "hysteria2://abc", "hy://abc", "hy2://abc",
                          "tuic://abc", "wg://abc", "ssh://abc"})
        QTest::newRow(s) << QString::fromLatin1(s);
}

void TestDeepLinks::directProtocolLinksAreNotDeepLinks()
{
    QFETCH(QString, link);
    // A config must never be swallowed by the deep-link branch, even if it somehow carried
    // a url= parameter — it is a server definition, not a pointer to a subscription.
    bool malformed = true;
    QVERIFY(ProfileListModel::subscriptionUrlFromDeepLink(link + QStringLiteral("?url=x"),
                                                          nullptr, &malformed)
                .isEmpty());
    QVERIFY(!malformed);
}

void TestDeepLinks::plainSubscriptionUrlsAreNotDeepLinks()
{
    for (const QString &link : {QStringLiteral("https://example.com/sub?url=inner"),
                                QStringLiteral("http://example.com/sub")}) {
        bool malformed = true;
        QVERIFY(ProfileListModel::subscriptionUrlFromDeepLink(link, nullptr, &malformed).isEmpty());
        QVERIFY(!malformed);
    }
}

void TestDeepLinks::aDeepLinkWithoutAUrlIsReportedAsMalformed()
{
    bool malformed = false;
    const QString url = ProfileListModel::subscriptionUrlFromDeepLink(
        QStringLiteral("clash://import?name=OnlyAName"), nullptr, &malformed);
    QVERIFY(url.isEmpty());
    QVERIFY2(malformed, "a link that looks like a deep link but fetches nothing must be reported, "
                        "not stored as a profile that can never connect");
}

void TestDeepLinks::rawTextIsNotADeepLink()
{
    for (const QString &input : {QStringLiteral("just some text"), QStringLiteral("{\"outbounds\":[]}"),
                                 QString()}) {
        bool malformed = true;
        QVERIFY(ProfileListModel::subscriptionUrlFromDeepLink(input, nullptr, &malformed).isEmpty());
        QVERIFY(!malformed);
    }
}

QTEST_MAIN(TestDeepLinks)
#include "test_deeplinks.moc"
