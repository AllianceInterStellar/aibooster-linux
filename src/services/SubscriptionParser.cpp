// SPDX-License-Identifier: GPL-3.0-or-later
#include "SubscriptionParser.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace SubscriptionParser {

namespace {

/// Scheme → display name. Keys mirror ray2sing's configTypes and endpointParsers, less the
/// exclusions listed on shareLinkSchemes().
const QList<QPair<QString, QString>> &schemeTable()
{
    static const QList<QPair<QString, QString>> table{
        {QStringLiteral("vmess"), QStringLiteral("VMess")},
        {QStringLiteral("svmess"), QStringLiteral("VMess")},
        {QStringLiteral("xvmess"), QStringLiteral("VMess")},
        {QStringLiteral("vless"), QStringLiteral("VLESS")},
        {QStringLiteral("svless"), QStringLiteral("VLESS")},
        {QStringLiteral("xvless"), QStringLiteral("VLESS")},
        {QStringLiteral("trojan"), QStringLiteral("Trojan")},
        {QStringLiteral("strojan"), QStringLiteral("Trojan")},
        {QStringLiteral("xtrojan"), QStringLiteral("Trojan")},
        {QStringLiteral("ss"), QStringLiteral("Shadowsocks")},
        {QStringLiteral("ssconf"), QStringLiteral("Shadowsocks")},
        {QStringLiteral("tuic"), QStringLiteral("TUIC")},
        {QStringLiteral("hysteria"), QStringLiteral("Hysteria")},
        {QStringLiteral("hysteria2"), QStringLiteral("Hysteria2")},
        {QStringLiteral("hy2"), QStringLiteral("Hysteria2")},
        {QStringLiteral("ssh"), QStringLiteral("SSH")},
        {QStringLiteral("socks"), QStringLiteral("SOCKS")},
        {QStringLiteral("phttp"), QStringLiteral("HTTP")},
        {QStringLiteral("phttps"), QStringLiteral("HTTPS")},
        {QStringLiteral("mieru"), QStringLiteral("Mieru")},
        {QStringLiteral("mierus"), QStringLiteral("Mieru")},
        {QStringLiteral("psiphon"), QStringLiteral("Psiphon")},
        {QStringLiteral("dnstt"), QStringLiteral("DNSTT")},
        {QStringLiteral("wg"), QStringLiteral("WireGuard")},
        {QStringLiteral("wireguard"), QStringLiteral("WireGuard")},
        {QStringLiteral("awg"), QStringLiteral("AmneziaWG")},
        {QStringLiteral("warp"), QStringLiteral("WARP")},
    };
    return table;
}

QString schemeOf(const QString &line)
{
    const int idx = line.indexOf(QStringLiteral("://"));
    return idx > 0 ? line.left(idx).toLower() : QString();
}

/// A Clash config the engine converts: a top-level `proxies:` list with at least one entry.
bool looksLikeClashProxies(const QString &text)
{
    static const QRegularExpression proxiesKey(QStringLiteral("^proxies:\\s*$"),
                                               QRegularExpression::MultilineOption);
    static const QRegularExpression entry(QStringLiteral("^\\s*-\\s*\\{?\\s*name\\s*:"),
                                          QRegularExpression::MultilineOption);
    const QRegularExpressionMatch key = proxiesKey.match(text);
    return key.hasMatch() && entry.match(text, key.capturedEnd()).hasMatch();
}

} // namespace

const QStringList &shareLinkSchemes()
{
    static const QStringList schemes = [] {
        QStringList list;
        for (const auto &entry : schemeTable())
            list.append(entry.first + QStringLiteral("://"));
        return list;
    }();
    return schemes;
}

bool isShareLink(const QString &line)
{
    for (const QString &prefix : shareLinkSchemes()) {
        if (line.startsWith(prefix, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QString unsupportedScheme(const QString &line)
{
    static const QStringList schemes{QStringLiteral("ssr"), QStringLiteral("hy"),
                                     QStringLiteral("naive")};
    const QString scheme = schemeOf(line);
    return schemes.contains(scheme) ? scheme : QString();
}

QString decodeBase64(const QString &input)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QString compact = input;
    compact.remove(whitespace);
    if (compact.isEmpty())
        return {};
    compact.replace(QLatin1Char('-'), QLatin1Char('+'));
    compact.replace(QLatin1Char('_'), QLatin1Char('/'));
    while (compact.size() % 4 != 0)
        compact.append(QLatin1Char('='));

    const auto result = QByteArray::fromBase64Encoding(
        compact.toLatin1(), QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!result)
        return {};
    const QByteArray &decoded = result.decoded;
    if (decoded.isEmpty() || decoded.contains('\0'))
        return {};
    return QString::fromUtf8(decoded);
}

QString decodeBody(const QString &content)
{
    const QString trimmed = content.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('{'))
        || trimmed.startsWith(QLatin1Char('[')))
        return trimmed;

    const QStringList lines = trimmed.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (isShareLink(line.trimmed()))
            return trimmed;
    }

    // Too short to be a subscription; decoding it would turn an ordinary word into bytes.
    if (trimmed.size() < 8)
        return trimmed;
    const QString decoded = decodeBase64(trimmed);
    if (decoded.isEmpty())
        return trimmed;
    if (decoded.contains(QStringLiteral("://")) || decoded.contains(QLatin1Char('{'))
        || decoded.contains(QLatin1Char('#')) || decoded.contains(QStringLiteral("[Interface]")))
        return decoded;
    return trimmed;
}

QStringList shareLinks(const QString &content)
{
    QStringList links;
    const QStringList lines = decodeBody(content).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (isShareLink(trimmed))
            links.append(trimmed);
    }
    return links;
}

QString protocolName(const QString &line)
{
    const QString scheme = schemeOf(line);
    if (scheme.isEmpty())
        return {};
    for (const auto &entry : schemeTable()) {
        if (entry.first == scheme)
            return entry.second;
    }
    return {};
}

bool isUsableConfig(const QByteArray &configData)
{
    const QByteArray trimmed = configData.trimmed();
    if (trimmed.isEmpty())
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        if (root.contains(QStringLiteral("outbounds")) || root.contains(QStringLiteral("endpoints"))) {
            return !root.value(QStringLiteral("outbounds")).toArray().isEmpty()
                || !root.value(QStringLiteral("endpoints")).toArray().isEmpty();
        }
        // The engine wraps an object with neither key as a single outbound. Only one with a
        // type can become one; the free-node index has none.
        return !root.value(QStringLiteral("type")).toString().isEmpty();
    }
    if (doc.isArray())
        return false;

    const QString text = QString::fromUtf8(trimmed);
    if (!shareLinks(text).isEmpty())
        return true;
    const QString decoded = decodeBody(text);
    return decoded.contains(QStringLiteral("[Interface]")) || looksLikeClashProxies(decoded);
}

} // namespace SubscriptionParser
