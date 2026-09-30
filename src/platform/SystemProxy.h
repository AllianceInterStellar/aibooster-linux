// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AIBOOSTER_SYSTEMPROXY_H
#define AIBOOSTER_SYSTEMPROXY_H

#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <functional>

/// Points the desktop's system-wide proxy settings at the engine's local inbound, and —
/// far more importantly — puts them back.
///
/// ⚠️ This writes MACHINE-WIDE state that outlives the process. If we set the proxy and
/// then die without reverting, every application on the desktop keeps trying to reach a
/// port that nothing is listening on any more, which presents to the user as "the whole
/// machine lost internet" with no visible culprit. That is not hypothetical — it is the
/// exact failure this class exists to prevent.
///
/// So the previous settings are written to disk *before* ours are applied, and
/// recoverFromPreviousRun() replays that file on the next launch. A SIGKILL is therefore
/// recoverable: the worst case is that the proxy stays wrong until the app is started
/// again, instead of staying wrong for ever.
///
/// Every change runs gsettings or kwriteconfig once per key — nine processes for an apply —
/// and each may take seconds on a busy session bus. The *Async calls run them on one
/// private worker thread, in the order they were made, so the UI never waits on them; the
/// synchronous calls finish any queued work first and are for start-up, shutdown and tests.
class SystemProxy : public QObject
{
    Q_OBJECT

public:
    enum Backend {
        None,      ///< No supported settings daemon — we leave the system alone.
        GSettings, ///< GNOME, Cinnamon, MATE, Budgie, Unity…
        Kde,       ///< Plasma (kioslaverc)
    };
    Q_ENUM(Backend)

    static SystemProxy &instance();

    /// Detected once, at first use.
    Backend backend() const { return m_backend; }
    QString backendName() const;

    bool isApplied() const { return m_applied; }
    QString lastError() const;

    /// Saves the current settings, then routes HTTP/HTTPS/SOCKS at host:port.
    /// Returns false and touches nothing on failure. Blocks; see applyAsync().
    bool apply(const QString &host, quint16 port);

    /// apply() off the UI thread. `done` runs on `context`'s thread with the result and,
    /// on failure, the reason — unless `context` has been destroyed by then.
    void applyAsync(const QString &host, quint16 port, QObject *context,
                    std::function<void(bool ok, const QString &error)> done);

    /// revert() off the UI thread, after anything already queued. `done` as for applyAsync.
    void revertAsync(QObject *context, std::function<void()> done);

    /// Blocks until every queued apply/revert has finished.
    void waitForPending();

    /// Restores whatever was saved by apply(). Safe to call when nothing was applied.
    /// The saved state is deleted only once the restore has gone through; otherwise it is
    /// kept so the next launch can retry.
    void revert();

    /// Call once at start-up, BEFORE any connection can be made: if the previous run was
    /// killed while the proxy was redirected, this puts the desktop back.
    /// Returns true when it actually undid something. A restore that fails keeps the saved
    /// state for the next attempt and returns false.
    static bool recoverFromPreviousRun();

signals:
    void appliedChanged(bool applied);

private:
    explicit SystemProxy(QObject *parent = nullptr);

    static Backend detectBackend();
    static QString statePath();

    static QJsonObject captureGSettings();
    static bool restoreGSettings(const QJsonObject &saved);
    static bool applyGSettings(const QString &host, quint16 port);

    static QJsonObject captureKde();
    static bool restoreKde(const QJsonObject &saved);
    static bool applyKde(const QString &host, quint16 port);

    bool doApply(const QString &host, quint16 port);
    void doRevert();
    void setLastError(const QString &error);

    static bool writeState(const QJsonObject &state);
    static QJsonObject readState();
    static void clearState();

    Backend m_backend;
    std::atomic<bool> m_applied{false};
    mutable QMutex m_errorMutex;
    QString m_lastError;
    /// One thread, so queued operations run strictly in order: an apply followed by a
    /// revert must never be reordered.
    QThreadPool m_worker;
};

#endif // AIBOOSTER_SYSTEMPROXY_H
