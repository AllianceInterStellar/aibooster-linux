#ifndef FIREBASEAUTH_H
#define FIREBASEAUTH_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <functional>

/// One signed-in Firebase session (what the backend's custom token is exchanged for).
struct FirebaseSession {
    QString idToken;       ///< Bearer token for ClawHostAPI calls.
    QString refreshToken;  ///< Used to mint a new idToken when it expires (~1h).
    QString uid;
    qint64 expiresAtMs = 0;

    bool isValid() const { return !idToken.isEmpty() && !uid.isEmpty(); }
};

/// Whether the credential was actually refused, or the request simply never got an answer.
enum class AuthFailure { Transport, Rejected };

/// Firebase Auth over its public REST API — no Firebase SDK, ported from the AgentAura Qt
/// client. The backend hands out a *custom* token (POST /auth/verify-otp); Firebase
/// exchanges that for the idToken every other API call is authenticated with.
class FirebaseAuth : public QObject {
    Q_OBJECT
public:
    static FirebaseAuth &instance();

    /// Exchange a backend custom token for a real session.
    void signInWithCustomToken(const QString &customToken,
                               std::function<void(FirebaseSession)> onSuccess,
                               std::function<void(QString)> onError);

    /// Mint a fresh idToken from a stored refresh token (called on launch and on 401).
    void refresh(const QString &refreshToken,
                 std::function<void(FirebaseSession)> onSuccess,
                 std::function<void(QString, AuthFailure)> onError);

private:
    explicit FirebaseAuth(QObject *parent = nullptr);

    QNetworkAccessManager m_nam;

    /// Public Firebase Web API key (same project as the iOS/Android/WinUI clients).
    static const QString API_KEY;
};

#endif
