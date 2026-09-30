// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AIBOOSTER_SUBSCRIPTIONPARSER_H
#define AIBOOSTER_SUBSCRIPTIONPARSER_H

#include <QByteArray>
#include <QString>
#include <QStringList>

/// The one place that decides what a subscription body is and whether the engine can use it.
///
/// This used to be three copies — the profile importer, the server list and the connect
/// path — with three different scheme lists. They drifted: hy:// was accepted on import and
/// refused at connect time, and ssr:// and hy:// were accepted everywhere although the
/// engine cannot parse either and silently drops them. The scheme list below is the
/// engine's own (ray2sing's configTypes and endpointParsers), so it can be checked against
/// one source.
namespace SubscriptionParser {

/// Share-link prefixes the shipped engine turns into outbounds, e.g. "vless://".
///
/// Deliberately absent: http:// and https:// (the engine reads them as HTTP proxies, but in
/// this client they are subscription URLs), direct:// (no tunnel at all), naive:// (the
/// release engine is built without it; see release.yml), and hy:// / ssr:// (the engine has
/// no parser for them).
const QStringList &shareLinkSchemes();

/// True when `line` (already trimmed) starts with one of shareLinkSchemes().
bool isShareLink(const QString &line);

/// For a server link in a format the engine cannot use (ssr://, hy://, naive://), its
/// scheme, so the importer can say so instead of storing a profile that never connects.
/// Empty for anything else.
QString unsupportedScheme(const QString &line);

/// Base64 in either alphabet, padding optional. Returns an empty string for anything that
/// is not valid base64, or that decodes to binary.
QString decodeBase64(const QString &input);

/// Subscriptions are usually served as one long base64 line; JSON, YAML and plain link lists
/// are returned as they are. Always returns the trimmed text when it cannot improve on it.
QString decodeBody(const QString &content);

/// Every share link in a body, decoded first if needed.
QStringList shareLinks(const QString &content);

/// Display name for a share link's protocol ("VLESS", "Hysteria2", …), or empty.
QString protocolName(const QString &line);

/// True when the engine can build at least one outbound from this payload: engine JSON with
/// outbounds or endpoints, a single outbound object, share links (plain or base64), a
/// WireGuard [Interface] block, or a Clash YAML proxy list.
///
/// Deliberately false for the free-node profile INDEX ({"data":{"profiles":[…]}}), which
/// the engine would read as an outbound with no type. For a VPN client "connected with no
/// tunnel" is the worst outcome — the user believes they are protected while their traffic
/// is in the clear — so anything else is refused before the engine sees it.
bool isUsableConfig(const QByteArray &configData);

} // namespace SubscriptionParser

#endif // AIBOOSTER_SUBSCRIPTIONPARSER_H
