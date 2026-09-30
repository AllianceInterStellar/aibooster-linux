// SPDX-License-Identifier: GPL-3.0-or-later
//
// Profiles, engine configs and the saved desktop proxy settings all go through
// PrivateFiles. What matters is that another account on the machine cannot read them, and
// that a failed write never leaves a truncated file behind.

#include "../src/core/PrivateFiles.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {
constexpr QFileDevice::Permissions kOwnerRw = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr QFileDevice::Permissions kGroupOrOther =
    QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
    | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
} // namespace

class TestPrivateFiles : public QObject
{
    Q_OBJECT

private slots:
    void writtenFilesAreOwnerOnly();
    void rewritingTightensAFileLeftWorldReadable();
    void directoriesAreOwnerOnly();
    void failureNamesTheFile();

private:
    QTemporaryDir m_dir;
};

void TestPrivateFiles::writtenFilesAreOwnerOnly()
{
    const QString path = m_dir.filePath(QStringLiteral("profiles.json"));
    QVERIFY(PrivateFiles::write(path, "secret"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("secret"));
    QCOMPARE(QFile::permissions(path) & kGroupOrOther, QFileDevice::Permissions());
    QVERIFY((QFile::permissions(path) & kOwnerRw) == kOwnerRw);
}

void TestPrivateFiles::rewritingTightensAFileLeftWorldReadable()
{
    // Files written by earlier versions are 0644. The next save must not inherit that.
    const QString path = m_dir.filePath(QStringLiteral("old.json"));
    QFile old(path);
    QVERIFY(old.open(QIODevice::WriteOnly));
    old.write("old");
    old.close();
    QVERIFY(QFile::setPermissions(path, kOwnerRw | QFileDevice::ReadGroup | QFileDevice::ReadOther));

    QVERIFY(PrivateFiles::write(path, "new"));
    QCOMPARE(QFile::permissions(path) & kGroupOrOther, QFileDevice::Permissions());
}

void TestPrivateFiles::directoriesAreOwnerOnly()
{
    const QString path = m_dir.filePath(QStringLiteral("a/b/engine"));
    QVERIFY(PrivateFiles::ensureDir(path));
    QVERIFY(QDir(path).exists());
    QCOMPARE(QFile::permissions(path) & kGroupOrOther, QFileDevice::Permissions());
}

void TestPrivateFiles::failureNamesTheFile()
{
    const QString path = m_dir.filePath(QStringLiteral("missing-dir/file.json"));
    QString error;
    QVERIFY(!PrivateFiles::write(path, "x", &error));
    QVERIFY2(error.contains(path), qPrintable(error));
}

QTEST_MAIN(TestPrivateFiles)
#include "test_privatefiles.moc"
