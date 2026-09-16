#include "ApiClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

const QString ApiClient::BASE_URL = QStringLiteral(AIBOOSTER_API_BASE_URL);

ApiClient::ApiClient(QObject *parent) : QObject(parent) {}

QNetworkReply *ApiClient::request(const QString &method, const QString &path, const QByteArray &body) {
    QNetworkRequest req(QUrl(BASE_URL + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_authToken.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_authToken).toUtf8());

    if (method == "GET") return m_nam.get(req);
    if (method == "POST") return m_nam.post(req, body.isEmpty() ? QByteArray("{}") : body);
    if (method == "PUT") return m_nam.put(req, body.isEmpty() ? QByteArray("{}") : body);
    if (method == "DELETE") return m_nam.deleteResource(req);
    return m_nam.get(req);
}

/// Pull the backend's error message out of the {success,data,message} envelope.
static QString envelopeError(const QByteArray &raw, const QString &fallback) {
    const auto obj = QJsonDocument::fromJson(raw).object();
    const QString msg = obj.value("message").toString();
    return msg.isEmpty() ? fallback : msg;
}

/// The API answers HTTP 200 for logical failures — success lives in the envelope. Returns
/// the `data` object on success; calls onError and returns false otherwise.
static bool unwrapEnvelope(QNetworkReply *reply, const QByteArray &raw, QJsonObject *data,
                           const std::function<void(QString)> &onError) {
    if (reply->error() != QNetworkReply::NoError) {
        onError(envelopeError(raw, reply->errorString()));
        return false;
    }
    const auto obj = QJsonDocument::fromJson(raw).object();
    if (!obj.value("success").toBool()) {
        onError(envelopeError(raw, "Request failed"));
        return false;
    }
    if (data) *data = obj.value("data").toObject();
    return true;
}

void ApiClient::sendEmailOtp(const QString &email,
                             std::function<void()> onSuccess, std::function<void(QString)> onError) {
    QJsonObject body;
    body["email"] = email.trimmed().toLower();
    auto *reply = request("POST", "/auth/send-otp", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, onSuccess, onError]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        if (!unwrapEnvelope(reply, raw, nullptr, onError))
            return;
        onSuccess();
    });
}

void ApiClient::verifyEmailOtp(const QString &email, const QString &code,
                               std::function<void(QString)> onSuccess,
                               std::function<void(QString)> onError) {
    QJsonObject body;
    body["email"] = email.trimmed().toLower();
    body["code"] = code.trimmed();
    auto *reply = request("POST", "/auth/verify-otp", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, onSuccess, onError]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        QJsonObject data;
        if (!unwrapEnvelope(reply, raw, &data, onError))
            return;
        const QString customToken = data.value("customToken").toString();
        if (customToken.isEmpty()) {
            onError(envelopeError(raw, "No token returned"));
            return;
        }
        onSuccess(customToken);
    });
}

void ApiClient::deleteAccount(std::function<void()> onSuccess,
                              std::function<void(QString)> onError) {
    auto *reply = request("DELETE", "/users/me");
    connect(reply, &QNetworkReply::finished, this, [reply, onSuccess, onError]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        if (!unwrapEnvelope(reply, raw, nullptr, onError))
            return;
        onSuccess();
    });
}

void ApiClient::fetchSubscriptionStatus(std::function<void(bool, QString)> onSuccess,
                                        std::function<void(QString)> onError) {
    auto *reply = request("GET", "/subscriptions/status?app=aibooster");
    connect(reply, &QNetworkReply::finished, this, [reply, onSuccess, onError]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        QJsonObject data;
        if (!unwrapEnvelope(reply, raw, &data, onError))
            return;
        onSuccess(data.value("active").toBool(), data.value("tier").toString());
    });
}

void ApiClient::createPremiumCheckout(std::function<void(QString, QString)> onSuccess,
                                      std::function<void(QString)> onError) {
    QJsonObject body;
    body["app"] = "aibooster";
    auto *reply = request("POST", "/stripe/premium/checkout",
                          QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, onSuccess, onError]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        QJsonObject data;
        if (!unwrapEnvelope(reply, raw, &data, onError))
            return;
        const QString url = data.value("url").toString();
        const QString sessionId = data.value("sessionId").toString();
        if (url.isEmpty() || sessionId.isEmpty()) {
            onError(envelopeError(raw, "Checkout returned no URL"));
            return;
        }
        onSuccess(url, sessionId);
    });
}

void ApiClient::verifyPremiumCheckout(const QString &sessionId,
                                      std::function<void(bool)> onSuccess,
                                      std::function<void(QString)> onError) {
    QJsonObject body;
    body["sessionId"] = sessionId;
    auto *reply = request("POST", "/stripe/premium/verify",
                          QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, onSuccess, onError]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        QJsonObject data;
        if (!unwrapEnvelope(reply, raw, &data, onError))
            return;
        onSuccess(data.value("paid").toBool());
    });
}
