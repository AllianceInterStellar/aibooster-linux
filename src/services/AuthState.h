#ifndef AUTHSTATE_H
#define AUTHSTATE_H

#include "ApiClient.h"
#include "FirebaseAuth.h"

#include <QDateTime>
#include <QSettings>
#include <QString>
#include <functional>

/// Signed-in state for the desktop client, ported from the AgentAura Qt client.
///
/// Sessions come from the backend's email one-time passcode: POST /auth/send-otp then
/// /auth/verify-otp yields a Firebase *custom* token, which FirebaseAuth exchanges for the
/// idToken every ClawHostAPI call is bearer-authenticated with. The refresh token is kept in
/// QSettings so the app comes back signed in, exactly like the mobile clients.
class AuthState {
public:
    static AuthState &instance() { static AuthState s; return s; }

    bool isLoggedIn() const { return m_loggedIn; }
    QString uid() const { return m_uid; }
    QString email() const { return m_email; }

    /// Adopt a freshly exchanged Firebase session and start authenticating API calls with it.
    void applySession(const FirebaseSession &session, const QString &email) {
        m_uid = session.uid;
        if (!email.isEmpty()) m_email = email;
        m_refreshToken = session.refreshToken;
        m_expiresAtMs = session.expiresAtMs;
        m_loggedIn = true;

        ApiClient::instance().setAuthToken(session.idToken);
        persist();
    }

    void logout() {
        m_uid.clear();
        m_email.clear();
        m_loggedIn = false;
        m_refreshToken.clear();
        m_expiresAtMs = 0;
        ApiClient::instance().setAuthToken({});
        QSettings st;
        st.remove("auth");
    }

    /// Restore a stored session at startup. Calls back with whether a usable session was
    /// re-established (the refresh token is long-lived, the idToken is not).
    void restore(std::function<void(bool)> done) {
        QSettings st;
        const QString refresh = st.value("auth/refreshToken").toString();
        const QString email = st.value("auth/email").toString();
        if (refresh.isEmpty()) {
            if (done) done(false);
            return;
        }
        FirebaseAuth::instance().refresh(
            refresh,
            [this, email, done](FirebaseSession session) {
                applySession(session, email);
                if (done) done(true);
            },
            [this, done](const QString &, AuthFailure failure) {
                // Only a real refusal (revoked/expired/disabled) may clear the session. A 5xx,
                // a timeout or an offline launch must keep the refresh token: treating those as
                // rejection signed the user out permanently the first time the network hiccuped.
                if (failure == AuthFailure::Rejected) logout();
                if (done) done(false);
            });
    }

    /// True when the cached idToken is at/near expiry and should be refreshed before use.
    bool needsRefresh() const {
        return m_loggedIn && m_expiresAtMs > 0 &&
               QDateTime::currentMSecsSinceEpoch() >= m_expiresAtMs;
    }

    /// Refresh the idToken in place (no-op when there is nothing stored).
    void refreshIfNeeded(std::function<void(bool)> done = {}) {
        if (!needsRefresh() || m_refreshToken.isEmpty()) {
            if (done) done(m_loggedIn);
            return;
        }
        const QString email = m_email;
        FirebaseAuth::instance().refresh(
            m_refreshToken,
            [this, email, done](FirebaseSession session) {
                applySession(session, email);
                if (done) done(true);
            },
            [this, done](const QString &, AuthFailure failure) {
                // Same rule on the mid-session refresh path.
                if (failure == AuthFailure::Rejected) logout();
                if (done) done(false);
            });
    }

private:
    AuthState() = default;

    void persist() const {
        QSettings st;
        st.setValue("auth/refreshToken", m_refreshToken);
        st.setValue("auth/email", m_email);
        st.setValue("auth/uid", m_uid);
    }

    bool m_loggedIn = false;
    QString m_uid;
    QString m_email;
    QString m_refreshToken;
    qint64 m_expiresAtMs = 0;
};

#endif
