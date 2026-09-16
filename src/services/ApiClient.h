#ifndef APICLIENT_H
#define APICLIENT_H

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <functional>

/// ClawHostAPI client, trimmed from the AgentAura Qt port to what AiBooster needs:
/// email one-time-passcode auth plus the premium subscription endpoints.
///
/// Every response uses the {success,data,message} envelope and answers HTTP 200 even for
/// logical failures, so callers branch on the envelope's `success`, not the status code.
class ApiClient : public QObject {
    Q_OBJECT
public:
    static ApiClient &instance() { static ApiClient c; return c; }

    void setAuthToken(const QString &token) { m_authToken = token; }

    // ── Auth (email one-time passcode) ──
    /// POST /auth/send-otp — emails a 6-digit code.
    void sendEmailOtp(const QString &email,
                      std::function<void()> onSuccess, std::function<void(QString)> onError);
    /// POST /auth/verify-otp — returns a Firebase *custom* token to exchange via FirebaseAuth.
    void verifyEmailOtp(const QString &email, const QString &code,
                        std::function<void(QString)> onSuccess, std::function<void(QString)> onError);

    // ── Premium subscription ──
    /// GET /subscriptions/status?app=aibooster — data.active + data.tier ('pro'|'free').
    /// DELETE /users/me — permanently deletes the backend account. The server refuses with 409
    /// while a store subscription is active; that message reaches onError verbatim.
    void deleteAccount(std::function<void()> onSuccess, std::function<void(QString)> onError);

    void fetchSubscriptionStatus(std::function<void(bool active, QString tier)> onSuccess,
                                 std::function<void(QString)> onError);
    /// POST /stripe/premium/checkout — data.url (open in the browser) + data.sessionId.
    void createPremiumCheckout(std::function<void(QString url, QString sessionId)> onSuccess,
                               std::function<void(QString)> onError);
    /// POST /stripe/premium/verify — data.paid; polled while the browser checkout is open.
    void verifyPremiumCheckout(const QString &sessionId,
                               std::function<void(bool paid)> onSuccess,
                               std::function<void(QString)> onError);

private:
    explicit ApiClient(QObject *parent = nullptr);
    QNetworkReply *request(const QString &method, const QString &path, const QByteArray &body = {});

    QNetworkAccessManager m_nam;
    QString m_authToken;
    static const QString BASE_URL;
};

#endif
