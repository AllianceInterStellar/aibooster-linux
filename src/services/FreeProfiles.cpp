#include "FreeProfiles.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

// ── Deployment-specific endpoints ────────────────────────────────────────────────
// These identify ONE operator's free-node service; they are not part of the client's
// behaviour. Supply your own at configure time:
//
//   cmake -B build -DAIBOOSTER_FREE_URL=… -DAIBOOSTER_FREE_BACKUP_URL=…
//
// Left at their defaults the client builds and runs, and reports a clear error when a
// connection is attempted, rather than silently pointing at somebody else's servers.
const QString FreeProfiles::kPrimaryUrl = QStringLiteral(AIBOOSTER_FREE_URL);
const QString FreeProfiles::kBackupUrl = QStringLiteral(AIBOOSTER_FREE_BACKUP_URL);

namespace {

const char *const kUserAgent = "AiBooster/2.0";
constexpr int kTimeoutMs = 15000;

} // namespace

FreeProfiles::FreeProfiles(QObject *parent)
    : QObject(parent)
    , m_urls({kPrimaryUrl, kBackupUrl})
{
}

void FreeProfiles::setEndpoints(const QStringList &urls)
{
    if (!urls.isEmpty())
        m_urls = urls;
}

QStringList FreeProfiles::configuredEndpoints(const QStringList &urls)
{
    QStringList usable;
    for (const QString &url : urls) {
        const QString trimmed = url.trimmed();
        if (trimmed.isEmpty())
            continue;
        // The shipped default is https://example.invalid/…; .invalid is reserved (RFC 2606)
        // and can never answer, so requesting it would only turn "never configured" into an
        // anonymous DNS failure.
        if (QUrl(trimmed).host().endsWith(QLatin1String(".invalid"), Qt::CaseInsensitive))
            continue;
        usable.append(trimmed);
    }
    return usable;
}

QString FreeProfiles::unconfiguredMessage()
{
    return QStringLiteral(
        "This build has no free-node endpoint. It was compiled without -DAIBOOSTER_FREE_URL, "
        "so it cannot fetch the free nodes. Add a profile of your own under Profiles, or see "
        "README.md → Deployment configuration.");
}

void FreeProfiles::tryGet(const QStringList &urls, int index,
                          std::function<void(QByteArray)> onBody,
                          std::function<void(QString)> onError)
{
    if (index >= urls.size()) {
        if (onError) onError(QStringLiteral("All free config endpoints failed"));
        return;
    }

    QNetworkRequest request{QUrl{urls.at(index)}};
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));
    request.setTransferTimeout(kTimeoutMs);

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, urls, index, onBody, onError]() {
        reply->deleteLater();
        // Any failure (network, HTTP status, empty body) falls through to the next endpoint,
        // exactly like the mobile clients.
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError
            || (status != 0 && (status < 200 || status > 299)) || body.isEmpty()) {
            tryGet(urls, index + 1, onBody, onError);
            return;
        }
        if (onBody) onBody(body);
    });
}

QString FreeProfiles::parseFirstFreeSublink(const QByteArray &indexBody)
{
    const QJsonDocument doc = QJsonDocument::fromJson(indexBody);
    if (!doc.isObject()) return {};
    const QJsonArray profiles = doc.object().value("data").toObject().value("profiles").toArray();
    if (profiles.isEmpty()) return {};
    return profiles.at(0).toObject().value("sublink").toString();
}

void FreeProfiles::fetch(std::function<void(QString)> onSuccess,
                         std::function<void(QString)> onError)
{
    const QStringList urls = configuredEndpoints(m_urls);
    if (urls.isEmpty()) {
        if (onError) onError(unconfiguredMessage());
        return;
    }

    tryGet(urls, 0,
           [this, onSuccess, onError](const QByteArray &indexBody) {
        const QString sublink = parseFirstFreeSublink(indexBody);
        if (sublink.isEmpty()) {
            // Not the index shape — assume the body already is a usable subscription.
            if (onSuccess) onSuccess(QString::fromUtf8(indexBody));
            return;
        }
        tryGet({sublink}, 0,
               [onSuccess](const QByteArray &body) {
                   if (onSuccess) onSuccess(QString::fromUtf8(body));
               },
               onError);
    }, onError);
}
