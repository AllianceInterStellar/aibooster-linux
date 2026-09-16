#include "FirebaseAuth.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

// Firebase Web API keys are public by design — they identify the project, they do not grant
// access; access is decided by Firebase security rules. It is parameterised anyway so a fork
// points at its own project instead of this one's.
const QString FirebaseAuth::API_KEY = QStringLiteral(AIBOOSTER_FIREBASE_API_KEY);

FirebaseAuth &FirebaseAuth::instance() {
    static FirebaseAuth a;
    return a;
}

FirebaseAuth::FirebaseAuth(QObject *parent) : QObject(parent) {}

/// The uid is NOT returned as a field by signInWithCustomToken (verified against the live
/// endpoint: it answers kind/idToken/refreshToken/expiresIn/isNewUser only). It lives in the
/// idToken's JWT claims as user_id/sub, so read it from there.
static QString uidFromIdToken(const QString &idToken) {
    const QStringList parts = idToken.split('.');
    if (parts.size() < 2) return {};
    QByteArray payload = parts.at(1).toUtf8();
    // JWT uses base64url without padding.
    payload = QByteArray::fromBase64(payload, QByteArray::Base64UrlEncoding);
    const QJsonObject claims = QJsonDocument::fromJson(payload).object();
    QString uid = claims.value("user_id").toString();
    if (uid.isEmpty()) uid = claims.value("sub").toString();
    return uid;
}

static qint64 expiryFromSeconds(const QString &expiresIn) {
    const qint64 seconds = expiresIn.toLongLong();
    // Refresh a minute early so a call never goes out with a just-expired token.
    const qint64 skew = 60;
    return QDateTime::currentMSecsSinceEpoch() + (seconds > skew ? (seconds - skew) : seconds) * 1000;
}

void FirebaseAuth::signInWithCustomToken(const QString &customToken,
                                         std::function<void(FirebaseSession)> onSuccess,
                                         std::function<void(QString)> onError) {
    QNetworkRequest req(QUrl(
        "https://identitytoolkit.googleapis.com/v1/accounts:signInWithCustomToken?key=" + API_KEY));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["token"] = customToken;
    body["returnSecureToken"] = true;

    auto *reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, onSuccess, onError]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            // Firebase puts the useful reason in the body, not in errorString().
            const auto obj = QJsonDocument::fromJson(raw).object();
            const QString msg = obj.value("error").toObject().value("message").toString();
            onError(msg.isEmpty() ? reply->errorString() : msg);
            return;
        }

        const auto obj = QJsonDocument::fromJson(raw).object();
        FirebaseSession session;
        session.idToken = obj.value("idToken").toString();
        session.refreshToken = obj.value("refreshToken").toString();
        session.uid = obj.value("localId").toString();
        if (session.uid.isEmpty()) session.uid = uidFromIdToken(session.idToken);
        session.expiresAtMs = expiryFromSeconds(obj.value("expiresIn").toString());

        if (!session.isValid()) {
            onError("Firebase returned an incomplete session");
            return;
        }
        onSuccess(session);
    });
}

/// Did the server actually refuse the credential, or did the request just never get an answer?
/// securetoken.googleapis.com answers 400 for TOKEN_EXPIRED / INVALID_REFRESH_TOKEN /
/// USER_DISABLED / USER_NOT_FOUND — a verdict on the token itself. A 5xx, or no HTTP status at
/// all (DNS failure, timeout, connection refused, a proxy erroring out), is the network being
/// unavailable and must not cost the user their session.
static AuthFailure classifyFailure(QNetworkReply *reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool refused = (status == 400 || status == 401 || status == 403);
    return refused ? AuthFailure::Rejected : AuthFailure::Transport;
}

void FirebaseAuth::refresh(const QString &refreshToken,
                           std::function<void(FirebaseSession)> onSuccess,
                           std::function<void(QString, AuthFailure)> onError) {
    QNetworkRequest req(QUrl("https://securetoken.googleapis.com/v1/token?key=" + API_KEY));
    // This endpoint is form-encoded, not JSON.
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    const QByteArray body =
        "grant_type=refresh_token&refresh_token=" + QUrl::toPercentEncoding(refreshToken);

    auto *reply = m_nam.post(req, body);
    connect(reply, &QNetworkReply::finished, this, [reply, refreshToken, onSuccess, onError]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            const auto obj = QJsonDocument::fromJson(raw).object();
            const QString msg = obj.value("error").toObject().value("message").toString();
            onError(msg.isEmpty() ? reply->errorString() : msg, classifyFailure(reply));
            return;
        }

        const auto obj = QJsonDocument::fromJson(raw).object();
        FirebaseSession session;
        session.idToken = obj.value("id_token").toString();
        // The refresh endpoint may or may not rotate the refresh token.
        session.refreshToken = obj.value("refresh_token").toString();
        if (session.refreshToken.isEmpty()) session.refreshToken = refreshToken;
        session.uid = obj.value("user_id").toString();
        if (session.uid.isEmpty()) session.uid = uidFromIdToken(session.idToken);
        session.expiresAtMs = expiryFromSeconds(obj.value("expires_in").toString());

        if (!session.isValid()) {
            onError("Firebase refresh returned an incomplete session", AuthFailure::Transport);
            return;
        }
        onSuccess(session);
    });
}
