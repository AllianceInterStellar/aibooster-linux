// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AIBOOSTER_PRIVATEFILES_H
#define AIBOOSTER_PRIVATEFILES_H

#include <QByteArray>
#include <QString>

/// Files this client writes carry node credentials (UUIDs, passwords, pre-shared keys) and
/// the desktop's saved proxy settings. The default umask makes them world-readable (0644),
/// so on a shared machine any other account could lift a user's subscription. Everything
/// goes through here instead: owner-only, and replaced atomically.
namespace PrivateFiles {

/// Creates `path` if needed and restricts it to its owner (0700).
bool ensureDir(const QString &path);

/// Writes `contents` to `path` as an owner-only (0600) file. Atomic: a crash or a full disk
/// mid-write leaves the previous file intact rather than a truncated one. On failure returns
/// false and, when `error` is given, a message naming the file.
bool write(const QString &path, const QByteArray &contents, QString *error = nullptr);

} // namespace PrivateFiles

#endif // AIBOOSTER_PRIVATEFILES_H
