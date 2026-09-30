// SPDX-License-Identifier: GPL-3.0-or-later
//
// The desktop proxy is machine-wide state that outlives this process, so the property that
// actually matters is not "can we set it" but "is it always put back". These tests assert
// the round trip, including the crash path — the one that, left unhandled, takes the user's
// whole machine off the network.

#include "../src/platform/SystemProxy.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTest>
#include <QThread>

namespace {

QString gsettings(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("gsettings"), args);
    if (!p.waitForFinished(5000))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString proxyMode()
{
    return gsettings({QStringLiteral("get"), QStringLiteral("org.gnome.system.proxy"),
                      QStringLiteral("mode")});
}

QString httpHost()
{
    return gsettings({QStringLiteral("get"), QStringLiteral("org.gnome.system.proxy.http"),
                      QStringLiteral("host")});
}

QString statePath()
{
    QString base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/state");
    return base + QStringLiteral("/aibooster/system-proxy-restore.json");
}

} // namespace

class TestSystemProxy : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void applyThenRevertRestoresTheOriginalSettings();
    void loopbackIsExcludedFromTheProxy();
    void recoveryUndoesAProxyLeftBehindByAKilledRun();
    void recoveryIsANoOpWithNoStateFile();
    void stateFileIsOwnerOnly();
    void applyKeepsTheOriginalsAnUnfinishedRestoreLeftBehind();
    void asyncCallsRunInOrderAndReportBackOnTheCallersThread();
    void asyncCompletionIsDroppedForADestroyedContext();
};

void TestSystemProxy::initTestCase()
{
    if (SystemProxy::instance().backend() == SystemProxy::None)
        QSKIP("no gsettings/kioslaverc backend on this machine");
    // These tests only know how to read back the gsettings backend.
    if (SystemProxy::instance().backend() != SystemProxy::GSettings)
        QSKIP("backend is not gsettings; read-back assertions do not apply");

    // Start from a known state so a leftover 'manual' from a previous run cannot make a
    // broken revert look successful.
    gsettings({QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy"),
               QStringLiteral("mode"), QStringLiteral("none")});
    QFile::remove(statePath());
}

void TestSystemProxy::applyThenRevertRestoresTheOriginalSettings()
{
    const QString modeBefore = proxyMode();
    const QString hostBefore = httpHost();

    SystemProxy &proxy = SystemProxy::instance();
    QVERIFY2(proxy.apply(QStringLiteral("127.0.0.1"), 2334), qPrintable(proxy.lastError()));
    QVERIFY(proxy.isApplied());
    QCOMPARE(proxyMode(), QStringLiteral("'manual'"));
    QVERIFY(httpHost().contains(QStringLiteral("127.0.0.1")));
    QVERIFY(QFile::exists(statePath()));

    proxy.revert();
    QVERIFY(!proxy.isApplied());
    QCOMPARE(proxyMode(), modeBefore);
    QCOMPARE(httpHost(), hostBefore);
    // The state file is the crash-recovery marker; leaving it behind would make the NEXT
    // launch "restore" settings that are already correct, clobbering anything the user
    // changed in between.
    QVERIFY(!QFile::exists(statePath()));
}

void TestSystemProxy::loopbackIsExcludedFromTheProxy()
{
    SystemProxy &proxy = SystemProxy::instance();
    QVERIFY(proxy.apply(QStringLiteral("127.0.0.1"), 2334));

    const QString ignore = gsettings({QStringLiteral("get"),
                                      QStringLiteral("org.gnome.system.proxy"),
                                      QStringLiteral("ignore-hosts")});
    // The client itself talks to the engine's control API on loopback. Routing that through
    // the proxy the engine just installed is a loop, and it shows up as "connected but no
    // traffic is ever reported".
    QVERIFY2(ignore.contains(QStringLiteral("localhost")), qPrintable(ignore));
    QVERIFY2(ignore.contains(QStringLiteral("127.0.0.0/8")), qPrintable(ignore));

    proxy.revert();
}

void TestSystemProxy::recoveryUndoesAProxyLeftBehindByAKilledRun()
{
    const QString modeBefore = proxyMode();

    // Simulate exactly what a SIGKILL leaves behind: the proxy redirected, and the state
    // file still on disk because revert() never ran.
    SystemProxy &proxy = SystemProxy::instance();
    QVERIFY(proxy.apply(QStringLiteral("127.0.0.1"), 2334));
    QCOMPARE(proxyMode(), QStringLiteral("'manual'"));
    QVERIFY(QFile::exists(statePath()));

    // A fresh process would see isApplied() == false, so recovery must work purely from
    // the file — not from in-memory state.
    QVERIFY(SystemProxy::recoverFromPreviousRun());

    QCOMPARE(proxyMode(), modeBefore);
    QVERIFY(!QFile::exists(statePath()));

    // Leave the singleton consistent for the remaining tests.
    proxy.revert();
}

void TestSystemProxy::recoveryIsANoOpWithNoStateFile()
{
    QFile::remove(statePath());
    const QString modeBefore = proxyMode();
    QVERIFY(!SystemProxy::recoverFromPreviousRun());
    QCOMPARE(proxyMode(), modeBefore);
}

void TestSystemProxy::stateFileIsOwnerOnly()
{
    SystemProxy &proxy = SystemProxy::instance();
    QVERIFY(proxy.apply(QStringLiteral("127.0.0.1"), 2334));
    const QFileDevice::Permissions others =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther
        | QFileDevice::WriteOther;
    QCOMPARE(QFile::permissions(statePath()) & others, QFileDevice::Permissions());
    proxy.revert();
}

void TestSystemProxy::applyKeepsTheOriginalsAnUnfinishedRestoreLeftBehind()
{
    // A restore that failed (say, no session bus yet at start-up) leaves the desktop still
    // pointed at us AND the state file holding the user's real settings. Connecting again
    // must not capture our own leftover settings over that file: the revert would then
    // "restore" the redirect and the user's originals would be gone for good.
    const QString modeBefore = proxyMode();
    QCOMPARE(modeBefore, QStringLiteral("'none'"));

    QJsonObject originals{{QStringLiteral("backend"), QStringLiteral("gsettings")},
                          {QStringLiteral("mode"), QStringLiteral("none")}};
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(originals).toJson());
    file.close();
    gsettings({QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy"),
               QStringLiteral("mode"), QStringLiteral("manual")});

    SystemProxy &proxy = SystemProxy::instance();
    QVERIFY(proxy.apply(QStringLiteral("127.0.0.1"), 2334));
    proxy.revert();

    QCOMPARE(proxyMode(), modeBefore);
    QVERIFY(!QFile::exists(statePath()));
}

void TestSystemProxy::asyncCallsRunInOrderAndReportBackOnTheCallersThread()
{
    // A cancel right after the engine came up queues a revert behind the apply. If the two
    // could reorder, the revert would find nothing to undo and the apply would then leave
    // the desktop redirected at an engine that is gone.
    const QString modeBefore = proxyMode();
    SystemProxy &proxy = SystemProxy::instance();
    QStringList order;
    bool applyOk = false;
    QThread *applyThread = nullptr;

    proxy.applyAsync(QStringLiteral("127.0.0.1"), 2334, this,
                     [&](bool ok, const QString &) {
                         applyOk = ok;
                         applyThread = QThread::currentThread();
                         order << QStringLiteral("apply");
                     });
    proxy.revertAsync(this, [&]() { order << QStringLiteral("revert"); });

    QTRY_COMPARE_WITH_TIMEOUT(order.size(), 2, 20000);
    QCOMPARE(order, (QStringList{QStringLiteral("apply"), QStringLiteral("revert")}));
    QVERIFY(applyOk);
    QCOMPARE(applyThread, QThread::currentThread());
    QVERIFY(!proxy.isApplied());
    QCOMPARE(proxyMode(), modeBefore);
    QVERIFY(!QFile::exists(statePath()));
}

void TestSystemProxy::asyncCompletionIsDroppedForADestroyedContext()
{
    SystemProxy &proxy = SystemProxy::instance();
    bool called = false;
    auto *context = new QObject;
    proxy.applyAsync(QStringLiteral("127.0.0.1"), 2334, context,
                     [&](bool, const QString &) { called = true; });
    delete context;
    proxy.waitForPending();
    QTest::qWait(100);   // let any queued completion be delivered
    QVERIFY(!called);
    proxy.revert();
}

QTEST_MAIN(TestSystemProxy)
#include "test_systemproxy.moc"
