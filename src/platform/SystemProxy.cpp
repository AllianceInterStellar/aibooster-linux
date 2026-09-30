// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemProxy.h"

#include "../core/PrivateFiles.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

namespace {

constexpr int kCommandTimeoutMs = 4000;

/// Runs a helper synchronously. Returns false on any non-zero exit, and never blocks
/// longer than kCommandTimeoutMs — a hung gsettings must not freeze the UI thread.
bool run(const QString &program, const QStringList &args, QString *stdOut = nullptr)
{
    QProcess process;
    process.start(program, args);
    if (!process.waitForFinished(kCommandTimeoutMs)) {
        process.kill();
        process.waitForFinished(500);
        return false;
    }
    if (stdOut)
        *stdOut = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool haveExecutable(const QString &name)
{
    return !QStandardPaths::findExecutable(name).isEmpty();
}

/// gsettings quotes strings ('none', 'localhost,…'); the values we store are unquoted.
QString unquote(QString value)
{
    if (value.size() >= 2 && value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))
        return value.mid(1, value.size() - 2);
    return value;
}

QString gsGet(const QString &schema, const QString &key)
{
    QString out;
    if (!run(QStringLiteral("gsettings"), {QStringLiteral("get"), schema, key}, &out))
        return QString();
    return unquote(out);
}

bool gsSet(const QString &schema, const QString &key, const QString &value)
{
    return run(QStringLiteral("gsettings"), {QStringLiteral("set"), schema, key, value});
}

const char *const kProxySchema = "org.gnome.system.proxy";
const char *const kHttpSchema = "org.gnome.system.proxy.http";
const char *const kHttpsSchema = "org.gnome.system.proxy.https";
const char *const kSocksSchema = "org.gnome.system.proxy.socks";

const char *const kKdeGroup = "Proxy Settings";

QString kioslavercPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kioslaverc");
}

QString kdeWriteTool()
{
    for (const QString &tool : {QStringLiteral("kwriteconfig6"), QStringLiteral("kwriteconfig5")})
        if (haveExecutable(tool))
            return tool;
    return QString();
}

QString kdeReadTool()
{
    for (const QString &tool : {QStringLiteral("kreadconfig6"), QStringLiteral("kreadconfig5")})
        if (haveExecutable(tool))
            return tool;
    return QString();
}

} // namespace

SystemProxy::SystemProxy(QObject *parent)
    : QObject(parent)
    , m_backend(detectBackend())
{
}

SystemProxy &SystemProxy::instance()
{
    static SystemProxy proxy;
    return proxy;
}

SystemProxy::Backend SystemProxy::detectBackend()
{
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();

    // Ask the desktop first, then fall back to whichever tool exists: a GNOME session
    // without gsettings (unusual, but it happens in trimmed containers) must not claim a
    // backend it cannot drive.
    if (desktop.contains(QLatin1String("kde")) || desktop.contains(QLatin1String("plasma"))) {
        if (!kdeWriteTool().isEmpty())
            return Kde;
    }
    if (haveExecutable(QStringLiteral("gsettings")))
        return GSettings;
    if (!kdeWriteTool().isEmpty())
        return Kde;
    return None;
}

QString SystemProxy::backendName() const
{
    switch (m_backend) {
    case GSettings: return QStringLiteral("GNOME (gsettings)");
    case Kde:       return QStringLiteral("KDE Plasma (kioslaverc)");
    case None:      break;
    }
    return QStringLiteral("unsupported desktop");
}

QString SystemProxy::statePath()
{
    // Resolved by hand rather than via QStandardPaths::GenericStateLocation, which only
    // exists from Qt 6.7 — this client supports the Qt 6.4 that current LTS distributions
    // ship. The fallback is the one the XDG base directory spec mandates.
    QString base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            + QStringLiteral("/.local/state");
    }
    const QString dir = base + QStringLiteral("/aibooster");
    PrivateFiles::ensureDir(dir);
    return dir + QStringLiteral("/system-proxy-restore.json");
}

bool SystemProxy::writeState(const QJsonObject &state)
{
    // The file is only useful if it survives a power cut mid-write, and it is tiny, so pay
    // for an atomic, synced replace rather than risk restoring from a truncated document.
    return PrivateFiles::write(statePath(), QJsonDocument(state).toJson(QJsonDocument::Indented));
}

QJsonObject SystemProxy::readState()
{
    QFile file(statePath());
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(file.readAll()).object();
}

void SystemProxy::clearState()
{
    QFile::remove(statePath());
}

QJsonObject SystemProxy::captureGSettings()
{
    QJsonObject saved{
        {QStringLiteral("backend"), QStringLiteral("gsettings")},
        {QStringLiteral("mode"), gsGet(QLatin1String(kProxySchema), QStringLiteral("mode"))},
        {QStringLiteral("ignore-hosts"),
         gsGet(QLatin1String(kProxySchema), QStringLiteral("ignore-hosts"))},
    };
    const QList<QPair<QString, const char *>> channels{
        {QStringLiteral("http"), kHttpSchema},
        {QStringLiteral("https"), kHttpsSchema},
        {QStringLiteral("socks"), kSocksSchema},
    };
    for (const auto &[name, schema] : channels) {
        saved.insert(name + QStringLiteral("-host"),
                     gsGet(QLatin1String(schema), QStringLiteral("host")));
        saved.insert(name + QStringLiteral("-port"),
                     gsGet(QLatin1String(schema), QStringLiteral("port")));
    }
    return saved;
}

bool SystemProxy::applyGSettings(const QString &host, quint16 port)
{
    const QString portText = QString::number(port);
    bool ok = true;
    ok &= gsSet(QLatin1String(kHttpSchema), QStringLiteral("host"), host);
    ok &= gsSet(QLatin1String(kHttpSchema), QStringLiteral("port"), portText);
    ok &= gsSet(QLatin1String(kHttpsSchema), QStringLiteral("host"), host);
    ok &= gsSet(QLatin1String(kHttpsSchema), QStringLiteral("port"), portText);
    ok &= gsSet(QLatin1String(kSocksSchema), QStringLiteral("host"), host);
    ok &= gsSet(QLatin1String(kSocksSchema), QStringLiteral("port"), portText);
    // Loopback and link-local must never be proxied — the UI itself talks to the engine's
    // Clash API on 127.0.0.1, and routing that through the proxy the engine just installed
    // is a loop that presents as "connected but the app shows no traffic".
    ok &= gsSet(QLatin1String(kProxySchema), QStringLiteral("ignore-hosts"),
                QStringLiteral("['localhost', '127.0.0.0/8', '::1', '192.168.0.0/16', "
                               "'10.0.0.0/8', '172.16.0.0/12']"));
    // Mode last: until it flips to 'manual' none of the above is live, so a partial
    // failure above leaves the desktop untouched rather than half-configured.
    ok &= gsSet(QLatin1String(kProxySchema), QStringLiteral("mode"), QStringLiteral("manual"));
    return ok;
}

bool SystemProxy::restoreGSettings(const QJsonObject &saved)
{
    bool ok = true;
    const QList<QPair<QString, const char *>> channels{
        {QStringLiteral("http"), kHttpSchema},
        {QStringLiteral("https"), kHttpsSchema},
        {QStringLiteral("socks"), kSocksSchema},
    };
    for (const auto &[name, schema] : channels) {
        const QString host = saved.value(name + QStringLiteral("-host")).toString();
        const QString port = saved.value(name + QStringLiteral("-port")).toString();
        if (!host.isNull())
            ok &= gsSet(QLatin1String(schema), QStringLiteral("host"), host);
        if (!port.isEmpty())
            ok &= gsSet(QLatin1String(schema), QStringLiteral("port"), port);
    }
    const QString ignore = saved.value(QStringLiteral("ignore-hosts")).toString();
    if (!ignore.isEmpty())
        ok &= gsSet(QLatin1String(kProxySchema), QStringLiteral("ignore-hosts"), ignore);

    // Fall back to 'none' rather than leaving 'manual' pointed at a dead port: an empty or
    // unreadable saved mode must still end with a working desktop.
    const QString mode = saved.value(QStringLiteral("mode")).toString();
    ok &= gsSet(QLatin1String(kProxySchema), QStringLiteral("mode"),
                mode.isEmpty() ? QStringLiteral("none") : mode);
    return ok;
}

QJsonObject SystemProxy::captureKde()
{
    QJsonObject saved{{QStringLiteral("backend"), QStringLiteral("kde")}};
    const QString reader = kdeReadTool();
    if (reader.isEmpty())
        return saved;

    const QStringList keys{QStringLiteral("ProxyType"), QStringLiteral("httpProxy"),
                           QStringLiteral("httpsProxy"), QStringLiteral("socksProxy"),
                           QStringLiteral("NoProxyFor")};
    for (const QString &key : keys) {
        QString out;
        run(reader,
            {QStringLiteral("--file"), kioslavercPath(), QStringLiteral("--group"),
             QLatin1String(kKdeGroup), QStringLiteral("--key"), key},
            &out);
        saved.insert(key, out);
    }
    return saved;
}

bool SystemProxy::applyKde(const QString &host, quint16 port)
{
    const QString writer = kdeWriteTool();
    if (writer.isEmpty())
        return false;

    auto write = [&](const QString &key, const QString &value) {
        return run(writer,
                   {QStringLiteral("--file"), kioslavercPath(), QStringLiteral("--group"),
                    QLatin1String(kKdeGroup), QStringLiteral("--key"), key, value});
    };

    const QString endpoint = QStringLiteral("http://%1 %2").arg(host).arg(port);
    bool ok = true;
    ok &= write(QStringLiteral("httpProxy"), endpoint);
    ok &= write(QStringLiteral("httpsProxy"), endpoint);
    ok &= write(QStringLiteral("socksProxy"), QStringLiteral("socks://%1 %2").arg(host).arg(port));
    ok &= write(QStringLiteral("NoProxyFor"), QStringLiteral("localhost,127.0.0.1,::1"));
    ok &= write(QStringLiteral("ProxyType"), QStringLiteral("1"));

    // Plasma caches proxy settings per-process; without this notification already-running
    // apps keep the old values until they restart.
    run(QStringLiteral("dbus-send"),
        {QStringLiteral("--session"), QStringLiteral("--type=signal"), QStringLiteral("/KIO/Scheduler"),
         QStringLiteral("org.kde.KIO.Scheduler.reparseSlaveConfiguration"), QStringLiteral("string:")});
    return ok;
}

bool SystemProxy::restoreKde(const QJsonObject &saved)
{
    const QString writer = kdeWriteTool();
    if (writer.isEmpty())
        return false;

    auto write = [&](const QString &key, const QString &value) {
        return run(writer,
                   {QStringLiteral("--file"), kioslavercPath(), QStringLiteral("--group"),
                    QLatin1String(kKdeGroup), QStringLiteral("--key"), key, value});
    };

    bool ok = true;
    for (const QString &key : {QStringLiteral("httpProxy"), QStringLiteral("httpsProxy"),
                               QStringLiteral("socksProxy"), QStringLiteral("NoProxyFor")}) {
        ok &= write(key, saved.value(key).toString());
    }
    const QString type = saved.value(QStringLiteral("ProxyType")).toString();
    ok &= write(QStringLiteral("ProxyType"), type.isEmpty() ? QStringLiteral("0") : type);

    run(QStringLiteral("dbus-send"),
        {QStringLiteral("--session"), QStringLiteral("--type=signal"), QStringLiteral("/KIO/Scheduler"),
         QStringLiteral("org.kde.KIO.Scheduler.reparseSlaveConfiguration"), QStringLiteral("string:")});
    return ok;
}

bool SystemProxy::apply(const QString &host, quint16 port)
{
    if (m_backend == None) {
        m_lastError = QStringLiteral(
            "This desktop has no proxy settings this client knows how to drive, so the "
            "system proxy was left alone. Point your browser at %1:%2 manually.")
                          .arg(host)
                          .arg(port);
        return false;
    }

    if (m_applied)
        return true;

    // A state file still on disk means an earlier restore did not go through, so the desktop
    // may still be pointed at us. Capturing now would record OUR settings as "the user's"
    // and overwrite the only copy of the real ones; keep the file and restore from it later.
    const QString backendKey = m_backend == GSettings ? QStringLiteral("gsettings")
                                                      : QStringLiteral("kde");
    const QJsonObject pending = readState();
    const bool reusePending =
        pending.value(QStringLiteral("backend")).toString() == backendKey;

    // Save first. If we crash between here and the apply below, recoverFromPreviousRun()
    // restores settings that were never changed, which is harmless; the opposite ordering
    // would leave a redirected desktop with nothing to restore from.
    const QJsonObject saved = reusePending ? pending
                              : m_backend == GSettings ? captureGSettings()
                                                       : captureKde();
    if (!reusePending && !writeState(saved)) {
        m_lastError = QStringLiteral("Could not save the current proxy settings to %1; "
                                     "refusing to change them.")
                          .arg(statePath());
        return false;
    }

    const bool ok = m_backend == GSettings ? applyGSettings(host, port) : applyKde(host, port);
    if (!ok) {
        m_lastError = QStringLiteral("Failed to set the system proxy via %1.").arg(backendName());
        // Undo whatever partially landed. The state file goes only once that worked: it is the
        // one record of the settings to go back to.
        const bool restored =
            m_backend == GSettings ? restoreGSettings(saved) : restoreKde(saved);
        if (restored)
            clearState();
        return false;
    }

    m_applied = true;
    m_lastError.clear();
    emit appliedChanged(true);
    return true;
}

void SystemProxy::revert()
{
    // Nothing of ours is live. Any state file left on disk belongs to a restore that has not
    // gone through yet, and deleting it would throw away the only way back.
    if (!m_applied)
        return;

    const QJsonObject saved = readState();
    bool restored = false;
    if (m_backend == GSettings)
        restored = restoreGSettings(saved);
    else if (m_backend == Kde)
        restored = restoreKde(saved);

    if (restored) {
        clearState();
    } else {
        // Keep the file: the next launch's recoverFromPreviousRun() retries from it.
        m_lastError = QStringLiteral("Could not restore the system proxy via %1; it will be "
                                     "retried the next time the app starts.")
                          .arg(backendName());
        qWarning("%s", qPrintable(m_lastError));
    }
    m_applied = false;
    emit appliedChanged(false);
}

bool SystemProxy::recoverFromPreviousRun()
{
    QFile file(statePath());
    if (!file.exists())
        return false;
    if (!file.open(QIODevice::ReadOnly)) {
        clearState();
        return false;
    }

    const QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    const QString backend = saved.value(QStringLiteral("backend")).toString();
    bool restored = false;
    if (backend == QLatin1String("gsettings")) {
        restored = restoreGSettings(saved);
    } else if (backend == QLatin1String("kde")) {
        restored = restoreKde(saved);
    } else {
        // Unreadable or from an unknown backend: nothing we could ever replay.
        clearState();
        return false;
    }

    if (!restored) {
        // Keep it and try again next launch; apply() reuses it meanwhile.
        qWarning("Could not restore the system proxy left behind by a previous run; "
                 "keeping %s to retry", qPrintable(statePath()));
        return false;
    }
    clearState();
    return true;
}
