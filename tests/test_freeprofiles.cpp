// SPDX-License-Identifier: GPL-3.0-or-later
//
// The free nodes are the one thing an unconfigured install can connect through, so their
// fetch has to behave exactly like the mobile clients': primary then backup, the index
// resolved to its first sublink, and a build that was never given an endpoint saying so
// instead of failing on DNS. The network cases run against a loopback HTTP server, never
// against the real endpoints.

#include "../src/services/FreeProfiles.h"

#include <QHash>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

namespace {

const char *const kPlaceholder = "https://example.invalid/?type=free";
const QByteArray kSubscriptionBody = "vmess://eyJhZGQiOiIxMjcuMC4wLjEifQ==\n";

/// Minimal HTTP/1.1 server: one request per connection, answered by path.
class FakeFeed : public QObject
{
public:
    struct Response {
        int status = 200;
        QByteArray body;
    };

    FakeFeed()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection())
                serve(socket);
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QString url(const QString &path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path);
    }
    void route(const QString &path, int status, const QByteArray &body)
    {
        m_routes.insert(path, Response{status, body});
    }

    QStringList requestedPaths;
    QStringList userAgents;

private:
    void serve(QTcpSocket *socket)
    {
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray &buffer = m_buffers[socket];
            buffer += socket->readAll();
            if (!buffer.contains("\r\n\r\n"))
                return;
            const QList<QByteArray> lines = buffer.split('\n');
            const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
            const QString path = requestLine.size() > 1 ? QString::fromLatin1(requestLine.at(1))
                                                        : QString();
            for (const QByteArray &line : lines) {
                if (line.toLower().startsWith("user-agent:"))
                    userAgents.append(QString::fromLatin1(line.mid(11).trimmed()));
            }
            requestedPaths.append(path);
            m_buffers.remove(socket);

            const Response response = m_routes.value(path, Response{404, "not found"});
            QByteArray reply = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n"
                             + "Content-Type: text/plain\r\n"
                             + "Content-Length: " + QByteArray::number(response.body.size())
                             + "\r\nConnection: close\r\n\r\n" + response.body;
            socket->write(reply);
            socket->disconnectFromHost();
        });
    }

    QTcpServer m_server;
    QHash<QString, Response> m_routes;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

/// Runs one fetch to completion and reports which callback fired.
struct FetchResult {
    bool finished = false;
    bool succeeded = false;
    QString value;
};

FetchResult runFetch(FreeProfiles &service)
{
    FetchResult result;
    service.fetch(
        [&result](const QString &config) {
            result.finished = true;
            result.succeeded = true;
            result.value = config;
        },
        [&result](const QString &error) {
            result.finished = true;
            result.value = error;
        });
    if (!QTest::qWaitFor([&result]() { return result.finished; }, 10000))
        result.value = QStringLiteral("no answer within 10 s");
    return result;
}

} // namespace

class TestFreeProfiles : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void firstSublinkOfTheIndex_data();
    void firstSublinkOfTheIndex();
    void placeholderAndEmptyEndpointsAreDropped();
    void anUnconfiguredBuildSaysWhyWithoutARequest();
    void primaryFailureFallsOverToBackupAndResolvesTheIndex();
    void aBodyThatIsNotAnIndexIsUsedAsIs();
    void everyEndpointFailingIsReported();
};

void TestFreeProfiles::initTestCase()
{
    // A developer machine's system proxy must not stand between the test and loopback.
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
}

void TestFreeProfiles::firstSublinkOfTheIndex_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("sublink");

    QTest::newRow("one profile")
        << QByteArray(R"({"data":{"profiles":[{"sublink":"https://a.example/s"}]}})")
        << QStringLiteral("https://a.example/s");
    QTest::newRow("first of several")
        << QByteArray(R"({"data":{"profiles":[{"sublink":"https://a.example/1"},)"
                      R"({"sublink":"https://a.example/2"}]}})")
        << QStringLiteral("https://a.example/1");
    QTest::newRow("no profiles") << QByteArray(R"({"data":{"profiles":[]}})") << QString();
    QTest::newRow("no data") << QByteArray(R"({"profiles":[{"sublink":"x"}]})") << QString();
    QTest::newRow("first has no sublink")
        << QByteArray(R"({"data":{"profiles":[{"name":"a"},{"sublink":"https://a.example/2"}]}})")
        << QString();
    QTest::newRow("engine config") << QByteArray(R"({"outbounds":[{"type":"direct"}]})") << QString();
    QTest::newRow("json array") << QByteArray(R"([{"sublink":"x"}])") << QString();
    QTest::newRow("share links") << kSubscriptionBody << QString();
    QTest::newRow("empty") << QByteArray() << QString();
}

void TestFreeProfiles::firstSublinkOfTheIndex()
{
    QFETCH(QByteArray, body);
    QFETCH(QString, sublink);
    QCOMPARE(FreeProfiles::parseFirstFreeSublink(body), sublink);
}

void TestFreeProfiles::placeholderAndEmptyEndpointsAreDropped()
{
    const QString placeholder = QString::fromLatin1(kPlaceholder);
    QVERIFY(FreeProfiles::configuredEndpoints({placeholder, placeholder}).isEmpty());
    QVERIFY(FreeProfiles::configuredEndpoints({QString(), QStringLiteral("  ")}).isEmpty());
    QVERIFY(FreeProfiles::configuredEndpoints({QStringLiteral("https://EXAMPLE.INVALID/x")}).isEmpty());

    // Only the reserved TLD counts — a real host that merely contains the word is kept.
    const QStringList real{QStringLiteral("https://invalid.example.com/?type=free"),
                           QStringLiteral("https://feed.example.net/?type=free")};
    QCOMPARE(FreeProfiles::configuredEndpoints(real), real);

    // One configured endpoint is enough; the placeholder beside it is skipped, not requested.
    QCOMPARE(FreeProfiles::configuredEndpoints({placeholder, real.at(1)}),
             QStringList{real.at(1)});
}

void TestFreeProfiles::anUnconfiguredBuildSaysWhyWithoutARequest()
{
    FreeProfiles service;
    service.setEndpoints({QString::fromLatin1(kPlaceholder), QString::fromLatin1(kPlaceholder)});

    bool succeeded = false;
    QString error;
    service.fetch([&succeeded](const QString &) { succeeded = true; },
                  [&error](const QString &e) { error = e; });

    // Reported at once: no request was made, so there is nothing to wait for.
    QVERIFY(!succeeded);
    QCOMPARE(error, FreeProfiles::unconfiguredMessage());
    QVERIFY(error.contains(QLatin1String("-DAIBOOSTER_FREE_URL")));
    QVERIFY(error.contains(QLatin1String("profile of your own")));
}

void TestFreeProfiles::primaryFailureFallsOverToBackupAndResolvesTheIndex()
{
    FakeFeed feed;
    QVERIFY(feed.listen());
    feed.route(QStringLiteral("/primary"), 503, "down");
    feed.route(QStringLiteral("/backup"), 200,
               R"({"data":{"profiles":[{"sublink":")" + feed.url(QStringLiteral("/sub")).toLatin1()
                   + R"("},{"sublink":"http://127.0.0.1:1/never"}]}})");
    feed.route(QStringLiteral("/sub"), 200, kSubscriptionBody);

    FreeProfiles service;
    service.setEndpoints({feed.url(QStringLiteral("/primary")), feed.url(QStringLiteral("/backup"))});
    const FetchResult result = runFetch(service);

    QVERIFY2(result.finished, "the fetch never completed");
    QVERIFY2(result.succeeded, qPrintable(result.value));
    QCOMPARE(result.value, QString::fromUtf8(kSubscriptionBody));
    QCOMPARE(feed.requestedPaths, (QStringList{QStringLiteral("/primary"), QStringLiteral("/backup"),
                                              QStringLiteral("/sub")}));
    for (const QString &agent : std::as_const(feed.userAgents))
        QCOMPARE(agent, QStringLiteral("AiBooster/2.0"));
}

void TestFreeProfiles::aBodyThatIsNotAnIndexIsUsedAsIs()
{
    FakeFeed feed;
    QVERIFY(feed.listen());
    feed.route(QStringLiteral("/plain"), 200, kSubscriptionBody);

    FreeProfiles service;
    service.setEndpoints({feed.url(QStringLiteral("/plain"))});
    const FetchResult result = runFetch(service);

    QVERIFY2(result.finished, "the fetch never completed");
    QVERIFY2(result.succeeded, qPrintable(result.value));
    QCOMPARE(result.value, QString::fromUtf8(kSubscriptionBody));
    QCOMPARE(feed.requestedPaths, QStringList{QStringLiteral("/plain")});
}

void TestFreeProfiles::everyEndpointFailingIsReported()
{
    FakeFeed feed;
    QVERIFY(feed.listen());
    feed.route(QStringLiteral("/a"), 500, "boom");
    feed.route(QStringLiteral("/b"), 200, QByteArray()); // an empty body is a failure too

    FreeProfiles service;
    service.setEndpoints({feed.url(QStringLiteral("/a")), feed.url(QStringLiteral("/b"))});
    const FetchResult result = runFetch(service);

    QVERIFY2(result.finished, "the fetch never completed");
    QVERIFY(!result.succeeded);
    QCOMPARE(result.value, QStringLiteral("All free config endpoints failed"));
    QCOMPARE(feed.requestedPaths, (QStringList{QStringLiteral("/a"), QStringLiteral("/b")}));
}

QTEST_MAIN(TestFreeProfiles)
#include "test_freeprofiles.moc"
