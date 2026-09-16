#include "ClashApi.h"

#include "../models/settingsmodel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <chrono>

namespace {
/// The clash API is in-process on the loopback; anything slower than this is a dead core.
constexpr int kTimeoutMs = 3000;
} // namespace

ClashApi::ClashApi(QObject *parent)
    : QObject(parent)
{
    // 127.0.0.1 must never be dialled through the system proxy the core itself installs —
    // that would loop the request back into the tunnel and time out.
    m_network.setProxy(QNetworkProxy::NoProxy);
}

QNetworkRequest ClashApi::buildRequest(const QString &path) const
{
    QNetworkRequest request{QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                                     .arg(SettingsModel::clashApiPort())
                                     .arg(path))};
    request.setRawHeader("Authorization",
                         "Bearer " + SettingsModel::clashApiSecret().toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kTimeoutMs);
    return request;
}

void ClashApi::fetchTraffic(std::function<void(Traffic)> onSuccess,
                            std::function<void(QString)> onError)
{
    QNetworkReply *reply = m_network.get(buildRequest(QStringLiteral("/connections")));
    connect(reply, &QNetworkReply::finished, this, [reply, onSuccess, onError]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (onError) onError(reply->errorString());
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        if (!root.contains(QStringLiteral("uploadTotal"))) {
            if (onError) onError(QStringLiteral("clash API returned no traffic counters"));
            return;
        }
        Traffic traffic;
        traffic.uploadTotal = static_cast<qint64>(root.value(QStringLiteral("uploadTotal")).toDouble());
        traffic.downloadTotal = static_cast<qint64>(root.value(QStringLiteral("downloadTotal")).toDouble());
        if (onSuccess) onSuccess(traffic);
    });
}

void ClashApi::fetchGroupMembers(const QString &group,
                                 std::function<void(QStringList)> onSuccess,
                                 std::function<void(QString)> onError)
{
    const QString path = QStringLiteral("/proxies/")
        + QString::fromUtf8(QUrl::toPercentEncoding(group));

    QNetworkReply *reply = m_network.get(buildRequest(path));
    connect(reply, &QNetworkReply::finished, this, [reply, group, onSuccess, onError]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (onError) onError(reply->errorString());
            return;
        }
        const QJsonArray all =
            QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("all")).toArray();
        if (all.isEmpty()) {
            if (onError)
                onError(QStringLiteral("the core reports no selectable nodes in \"%1\"").arg(group));
            return;
        }
        QStringList members;
        members.reserve(all.size());
        for (const QJsonValue &value : all)
            members.append(value.toString());
        if (onSuccess) onSuccess(members);
    });
}

void ClashApi::selectOutbound(const QString &group, const QString &tag,
                              std::function<void()> onSuccess,
                              std::function<void(QString)> onError)
{
    const QString path = QStringLiteral("/proxies/")
        + QString::fromUtf8(QUrl::toPercentEncoding(group));
    const QByteArray body =
        QJsonDocument(QJsonObject{{QStringLiteral("name"), tag}}).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_network.put(buildRequest(path), body);
    connect(reply, &QNetworkReply::finished, this, [reply, tag, onSuccess, onError]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status <= 299) {
            if (onSuccess) onSuccess();
            return;
        }
        if (status > 0) {
            // 404 here means the core does not know that tag — usually a profile edited after
            // connecting. Say so instead of pretending the switch happened.
            if (onError)
                onError(QStringLiteral("the core rejected \"%1\" (HTTP %2)").arg(tag).arg(status));
            return;
        }
        if (onError) onError(reply->errorString());
    });
}
