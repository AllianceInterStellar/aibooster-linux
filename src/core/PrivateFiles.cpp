// SPDX-License-Identifier: GPL-3.0-or-later
#include "PrivateFiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace PrivateFiles {

bool ensureDir(const QString &path)
{
    if (!QDir().mkpath(path))
        return false;
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner);
}

bool write(const QString &path, const QByteArray &contents, QString *error)
{
    auto fail = [&](const QString &reason) {
        if (error)
            *error = QStringLiteral("Could not write %1: %2").arg(path, reason);
        return false;
    };

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(file.errorString());
    // Restrict the temporary file before anything secret is in it; QSaveFile renames it over
    // the target on commit, so the final file keeps these permissions.
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return fail(file.errorString());
    if (file.write(contents) != contents.size())
        return fail(file.errorString());
    if (!file.commit())
        return fail(file.errorString());
    return true;
}

} // namespace PrivateFiles
