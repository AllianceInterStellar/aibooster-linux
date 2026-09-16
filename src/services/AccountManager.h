#ifndef ACCOUNTMANAGER_H
#define ACCOUNTMANAGER_H

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QTimer>
#include <functional>

/// The one account-facing QObject QML sees. Wraps the ported AgentAura auth stack
/// (ApiClient → FirebaseAuth → AuthState) plus AiBooster's premium subscription:
///
///   sendOtp(email) → verifyOtp(email, code)   two-step email login
///   startCheckout()                           Stripe checkout in the browser + verify polling
///   refreshSubscription()                     GET /subscriptions/status?app=aibooster
class AccountManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY changed)
    Q_PROPERTY(QString email READ email NOTIFY changed)
    Q_PROPERTY(bool premium READ premium NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(bool checkoutPending READ checkoutPending NOTIFY changed)

public:
    explicit AccountManager(QObject *parent = nullptr);
    ~AccountManager() override;

    /// Last-constructed instance — ConnectionModel gates the premium config on it.
    static AccountManager *instance() { return s_instance; }

    bool loggedIn() const { return m_loggedIn; }
    QString email() const { return m_email; }
    bool premium() const { return m_premium; }
    /// True while an account round-trip is in flight — including the startup restore +
    /// subscription lookup, during which premium() is not yet trustworthy.
    bool busy() const { return m_busy; }
    QString lastError() const { return m_lastError; }
    bool checkoutPending() const { return m_checkoutTimer.isActive(); }

    // ── QML API ──
    Q_INVOKABLE void sendOtp(const QString &email);
    Q_INVOKABLE void verifyOtp(const QString &email, const QString &code);
    Q_INVOKABLE void logout();
    /// Permanently deletes the backend account (DELETE /users/me). On success the server has
    /// already removed the Firebase user and revoked every refresh token, so the teardown is the
    /// same local-only path logout() takes. Failures land in lastError — a 409 means an active
    /// store subscription must be cancelled first, and nothing changes locally.
    Q_INVOKABLE void deleteAccount();
    Q_INVOKABLE void refreshSubscription();
    /// Opens the Stripe checkout page in the system browser and polls
    /// POST /stripe/premium/verify every 3 s until paid or 10 minutes pass.
    Q_INVOKABLE void startCheckout();
    /// Abandons the checkout poll. Without it a user who closed the Stripe tab without paying is
    /// stuck on a disabled "Waiting for payment…" button for the full 10-minute window, with
    /// signing out as the only escape.
    Q_INVOKABLE void cancelCheckout();
    /// Drops a stale lastError before a new flow opens. The string is shared by the login and
    /// delete-account dialogs, so a leftover from an unrelated operation reads as if the action
    /// the user just started had already failed.
    Q_INVOKABLE void clearError();

signals:
    void changed();
    /// The OTP email went out — the login UI advances to the code step.
    void otpSent();
    /// verifyOtp() completed and the session is live.
    void loginSucceeded();

private:
    void setBusy(bool busy);
    void setError(const QString &error);
    void adoptLogin();
    void pollCheckout();
    /// `forgetPersisted` drops the on-disk handle; pass false while the payment outcome
    /// is still unknown, or a completed payment becomes unreconcilable.
    void stopCheckout(bool forgetPersisted = true);
    void saveCheckoutState();
    /// Re-arm (or settle) a checkout that outlived the previous app run.
    void resumeCheckoutIfPending();
    /// refreshSubscription() with a completion hook — the startup path needs to know when the
    /// tier has actually settled so it can drop busy().
    void refreshSubscriptionThen(std::function<void()> done);
    /// Clears the startup busy() exactly once (whichever of the round-trip or its watchdog
    /// gets there first).
    void finishInitialResolve();

    bool m_loggedIn = false;
    QString m_email;
    bool m_premium = false;
    bool m_busy = false;
    bool m_initialResolvePending = false;
    QString m_lastError;

    QString m_checkoutSessionId;
    QTimer m_checkoutTimer;
    qint64 m_checkoutDeadlineMs = 0;

    static AccountManager *s_instance;
};

#endif
