// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AIBOOSTER_SYSTEMPROXY_H
#define AIBOOSTER_SYSTEMPROXY_H

#include <QJsonObject>
#include <QObject>
#include <QString>

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
    QString lastError() const { return m_lastError; }

    /// Saves the current settings, then routes HTTP/HTTPS/SOCKS at host:port.
    /// Returns false and touches nothing on failure.
    bool apply(const QString &host, quint16 port);

    /// Restores whatever was saved by apply(). Safe to call when nothing was applied.
    void revert();

    /// Call once at start-up, BEFORE any connection can be made: if the previous run was
    /// killed while the proxy was redirected, this puts the desktop back.
    /// Returns true when it actually had to undo something.
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

    static bool writeState(const QJsonObject &state);
    static void clearState();

    Backend m_backend;
    bool m_applied = false;
    QString m_lastError;
};

#endif // AIBOOSTER_SYSTEMPROXY_H
