#include "AccountManager.h"

#include "ApiClient.h"
#include "AuthState.h"
#include "FirebaseAuth.h"

#include <QDateTime>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>

namespace {
constexpr int kCheckoutPollMs = 3000;
constexpr qint64 kCheckoutTimeoutMs = 10 * 60 * 1000;
/// Upper bound on the startup "we don't know the tier yet" window. Without it a black-holed
/// network would leave busy() stuck true and the Connect button disabled forever.
constexpr int kInitialResolveTimeoutMs = 8000;
} // namespace

AccountManager *AccountManager::s_instance = nullptr;

AccountManager::AccountManager(QObject *parent)
    : QObject(parent)
{
    s_instance = this;

    m_checkoutTimer.setInterval(kCheckoutPollMs);
    connect(&m_checkoutTimer, &QTimer::timeout, this, &AccountManager::pollCheckout);

    // Come back signed in from the persisted refresh token, like the mobile clients.
    // busy() has to survive until the *subscription* answers, not just the token restore:
    // clearing it early let a premium user hit Connect while premium() still read false, and
    // they silently landed on the free pool with nothing re-evaluating afterwards.
    m_initialResolvePending = true;
    setBusy(true);
    QTimer::singleShot(kInitialResolveTimeoutMs, this, &AccountManager::finishInitialResolve);
    AuthState::instance().restore([this](bool restored) {
        if (!restored) {
            finishInitialResolve();
            return;
        }
        adoptLogin();
        refreshSubscriptionThen([this]() {
            finishInitialResolve();
            // Only now is there a bearer token to verify with. If premium already came back true
            // the stored checkout is moot and this is a no-op.
            resumeCheckoutIfPending();
        });
    });
}

void AccountManager::finishInitialResolve()
{
    if (!m_initialResolvePending)
        return;
    m_initialResolvePending = false;
    setBusy(false);
}

AccountManager::~AccountManager()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void AccountManager::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        emit changed();
    }
}

void AccountManager::setError(const QString &error)
{
    if (m_lastError != error) {
        m_lastError = error;
        emit changed();
    }
}

void AccountManager::adoptLogin()
{
    m_loggedIn = AuthState::instance().isLoggedIn();
    m_email = AuthState::instance().email();
    if (!m_loggedIn)
        m_premium = false;
    emit changed();
}

void AccountManager::sendOtp(const QString &email)
{
    const QString trimmed = email.trimmed();
    if (trimmed.isEmpty() || !trimmed.contains(QLatin1Char('@'))) {
        setError(QStringLiteral("Enter a valid email address"));
        return;
    }
    setError({});
    setBusy(true);
    ApiClient::instance().sendEmailOtp(
        trimmed,
        [this]() {
            setBusy(false);
            emit otpSent();
        },
        [this](const QString &err) {
            setBusy(false);
            setError(err);
        });
}

void AccountManager::verifyOtp(const QString &email, const QString &code)
{
    const QString trimmedCode = code.trimmed();
    if (trimmedCode.isEmpty()) {
        setError(QStringLiteral("Enter the code from the email"));
        return;
    }
    setError({});
    setBusy(true);
    const QString cleanEmail = email.trimmed();
    ApiClient::instance().verifyEmailOtp(
        cleanEmail, trimmedCode,
        [this, cleanEmail](const QString &customToken) {
            FirebaseAuth::instance().signInWithCustomToken(
                customToken,
                [this, cleanEmail](FirebaseSession session) {
                    AuthState::instance().applySession(session, cleanEmail);
                    setBusy(false);
                    adoptLogin();
                    emit loginSucceeded();
                    refreshSubscription();
                },
                [this](const QString &err) {
                    setBusy(false);
                    setError(err);
                });
        },
        [this](const QString &err) {
            setBusy(false);
            setError(err);
        });
}

void AccountManager::logout()
{
    stopCheckout();
    AuthState::instance().logout();
    m_premium = false;
    setError({});
    adoptLogin();
}

void AccountManager::deleteAccount()
{
    if (m_busy)
        return;
    setError({});
    setBusy(true);
    AuthState::instance().refreshIfNeeded([this](bool ok) {
        if (!ok) {
            setBusy(false);
            setError(QStringLiteral("Session expired — sign in again, then retry"));
            return;
        }
        ApiClient::instance().deleteAccount(
            [this]() {
                setBusy(false);
                // The account is gone server-side (Firebase user deleted, tokens revoked) —
                // logout() is exactly the local teardown that remains.
                logout();
            },
            [this](const QString &err) {
                setBusy(false);
                setError(err);
            });
    });
}

void AccountManager::refreshSubscription()
{
    refreshSubscriptionThen({});
}

void AccountManager::refreshSubscriptionThen(std::function<void()> done)
{
    // `done` must fire on every exit path, or the startup busy() never clears.
    auto finish = [done]() { if (done) done(); };

    if (!AuthState::instance().isLoggedIn()) {
        finish();
        return;
    }
    AuthState::instance().refreshIfNeeded([this, finish](bool ok) {
        if (!ok) {
            finish();
            return;
        }
        ApiClient::instance().fetchSubscriptionStatus(
            [this, finish](bool active, const QString &tier) {
                Q_UNUSED(tier)
                if (m_premium != active) {
                    m_premium = active;
                    emit changed();
                }
                finish();
            },
            [finish](const QString &) {
                // Keep the last known tier on a transient failure; the next refresh wins.
                finish();
            });
    });
}

void AccountManager::startCheckout()
{
    if (!AuthState::instance().isLoggedIn()) {
        setError(QStringLiteral("Sign in first"));
        return;
    }
    if (m_premium || m_checkoutTimer.isActive())
        return;

    setError({});
    setBusy(true);
    AuthState::instance().refreshIfNeeded([this](bool ok) {
        if (!ok) {
            setBusy(false);
            setError(QStringLiteral("Session expired — sign in again"));
            return;
        }
        ApiClient::instance().createPremiumCheckout(
            [this](const QString &url, const QString &sessionId) {
                setBusy(false);
                m_checkoutSessionId = sessionId;
                m_checkoutDeadlineMs = QDateTime::currentMSecsSinceEpoch() + kCheckoutTimeoutMs;
                // Persist BEFORE opening the browser. Stripe is live: if the app is closed or
                // crashes between paying and the poll confirming, an in-memory-only session id
                // means the payment can never be reconciled and the user is charged for nothing.
                saveCheckoutState();
                QDesktopServices::openUrl(QUrl(url));
                m_checkoutTimer.start();
                emit changed(); // checkoutPending flipped
            },
            [this](const QString &err) {
                setBusy(false);
                setError(err);
            });
    });
}

void AccountManager::cancelCheckout()
{
    if (!m_checkoutTimer.isActive())
        return;
    const QString sessionId = m_checkoutSessionId;
    // Stop waiting but KEEP the persisted handle: the verify below may not answer, and Stripe is
    // live — a payment made seconds before the user gave up must stay reconcilable on next launch.
    stopCheckout(/*forgetPersisted=*/false);
    // No error: the user chose to stop waiting, and startCheckout() is clickable again right away.
    setError({});
    if (sessionId.isEmpty())
        return;
    ApiClient::instance().verifyPremiumCheckout(
        sessionId,
        [this](bool paid) {
            // Either answer is definitive, so the handle has done its job.
            stopCheckout(/*forgetPersisted=*/true);
            if (!paid)
                return;
            m_premium = true;
            emit changed();
            refreshSubscription();
        },
        [](const QString &) {
            // Unknown outcome: leave the persisted record so resumeCheckoutIfPending() settles it.
        });
}

void AccountManager::clearError()
{
    setError({});
}

void AccountManager::pollCheckout()
{
    if (QDateTime::currentMSecsSinceEpoch() > m_checkoutDeadlineMs) {
        // Timing out says nothing about whether the payment landed; keep the handle so the next
        // launch verifies it once.
        stopCheckout(/*forgetPersisted=*/false);
        setError(QStringLiteral("Checkout timed out — try again"));
        return;
    }
    // The idToken only lives ~1 h and this loop can run for 10 min on top of an already-aged
    // session, so re-mint it before every attempt. Without this the verify calls start 401ing
    // partway through and a payment the user really made is never recorded.
    AuthState::instance().refreshIfNeeded([this](bool ok) {
        if (!ok)
            return; // auth hiccup — retryable, the next tick tries again until the deadline
        if (!m_checkoutTimer.isActive())
            return; // checkout ended while the token refresh was in flight

        ApiClient::instance().verifyPremiumCheckout(
            m_checkoutSessionId,
            [this](bool paid) {
                if (!paid)
                    return; // keep polling
                stopCheckout();
                // The server has recorded the subscription; re-fetch the authoritative status.
                m_premium = true;
                emit changed();
                refreshSubscription();
            },
            [](const QString &) {
                // Transient poll failure — the next tick retries until the deadline.
            });
    });
}

void AccountManager::stopCheckout(bool forgetPersisted)
{
    const bool wasPending = m_checkoutTimer.isActive();
    m_checkoutTimer.stop();
    m_checkoutSessionId.clear();
    m_checkoutDeadlineMs = 0;
    // The persisted record is the ONLY way a payment that completed while we were not looking can
    // still be reconciled. Drop it once the outcome is known, never merely because we stopped
    // waiting — see cancelCheckout().
    if (forgetPersisted) {
        QSettings settings;
        settings.remove(QStringLiteral("checkout/sessionId"));
        settings.remove(QStringLiteral("checkout/deadlineMs"));
    }
    if (wasPending)
        emit changed();
}

void AccountManager::saveCheckoutState()
{
    QSettings settings;
    settings.setValue(QStringLiteral("checkout/sessionId"), m_checkoutSessionId);
    settings.setValue(QStringLiteral("checkout/deadlineMs"), m_checkoutDeadlineMs);
}

/// Resume a checkout that was still in flight when the app last closed. Called once the session is
/// restored, because verifying needs a bearer token.
void AccountManager::resumeCheckoutIfPending()
{
    if (m_checkoutTimer.isActive() || m_premium)
        return;
    QSettings settings;
    const QString sessionId = settings.value(QStringLiteral("checkout/sessionId")).toString();
    const qint64 deadline = settings.value(QStringLiteral("checkout/deadlineMs")).toLongLong();
    if (sessionId.isEmpty())
        return;
    if (QDateTime::currentMSecsSinceEpoch() > deadline) {
        // Past the poll window, but the payment may still have gone through — verify once rather
        // than dropping it. Only a definitive "not paid" clears the record.
        m_checkoutSessionId = sessionId;
        ApiClient::instance().verifyPremiumCheckout(
            sessionId,
            [this](bool paid) {
                if (paid) {
                    m_premium = true;
                    emit changed();
                    refreshSubscription();
                }
                stopCheckout();
            },
            [this](const QString &) { m_checkoutSessionId.clear(); });
        return;
    }
    m_checkoutSessionId = sessionId;
    m_checkoutDeadlineMs = deadline;
    m_checkoutTimer.start();
    emit changed();
}
