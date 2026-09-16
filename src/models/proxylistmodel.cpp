#include "proxylistmodel.h"

#include "vpncore.h"
#include "connectionmodel.h"
#include "profilelistmodel.h"
#include "../services/ClashApi.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace {

constexpr int kProbeTimeoutMs = 3000;
/// Cap the in-flight sockets: a big subscription can hold hundreds of nodes and opening one
/// socket per node at once exhausts the process file-descriptor limit.
constexpr int kMaxConcurrentProbes = 32;

QStringList protocolPrefixes()
{
    return {QStringLiteral("vmess://"),     QStringLiteral("vless://"),  QStringLiteral("ss://"),
            QStringLiteral("ssr://"),       QStringLiteral("trojan://"), QStringLiteral("hysteria://"),
            QStringLiteral("hysteria2://"), QStringLiteral("hy://"),     QStringLiteral("hy2://"),
            QStringLiteral("tuic://"),      QStringLiteral("wg://"),     QStringLiteral("ssh://")};
}

bool isProtocolLink(const QString &line)
{
    const QStringList prefixes = protocolPrefixes();
    for (const QString &prefix : prefixes) {
        if (line.startsWith(prefix, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QString tryBase64Decode(const QString &input)
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

/// Subscription bodies arrive either as JSON, as a plain link list, or as one long base64 line.
QString decodeSubscriptionBody(const QString &content)
{
    const QString trimmed = content.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('{')))
        return trimmed;

    const QStringList lines = trimmed.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (isProtocolLink(line.trimmed()))
            return trimmed;
    }

    const QString decoded = tryBase64Decode(trimmed);
    if (decoded.isEmpty())
        return trimmed;
    if (decoded.contains(QStringLiteral("://")) || decoded.trimmed().startsWith(QLatin1Char('{')))
        return decoded;
    return trimmed;
}

QString displayProxyType(const QString &type)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("vmess"), QStringLiteral("VMess")},
        {QStringLiteral("vless"), QStringLiteral("VLESS")},
        {QStringLiteral("trojan"), QStringLiteral("Trojan")},
        {QStringLiteral("shadowsocks"), QStringLiteral("Shadowsocks")},
        {QStringLiteral("ss"), QStringLiteral("Shadowsocks")},
        {QStringLiteral("shadowsocksr"), QStringLiteral("ShadowsocksR")},
        {QStringLiteral("ssr"), QStringLiteral("ShadowsocksR")},
        {QStringLiteral("shadowtls"), QStringLiteral("ShadowTLS")},
        {QStringLiteral("hysteria"), QStringLiteral("Hysteria")},
        {QStringLiteral("hysteria2"), QStringLiteral("Hysteria2")},
        {QStringLiteral("wireguard"), QStringLiteral("WireGuard")},
        {QStringLiteral("warp"), QStringLiteral("WARP")},
        {QStringLiteral("tuic"), QStringLiteral("TUIC")},
        {QStringLiteral("anytls"), QStringLiteral("AnyTLS")},
        {QStringLiteral("ssh"), QStringLiteral("SSH")},
        {QStringLiteral("socks"), QStringLiteral("SOCKS")},
        {QStringLiteral("http"), QStringLiteral("HTTP")}};
    const QString mapped = names.value(type);
    return mapped.isEmpty() ? type.toUpper() : mapped;
}

/// Parity with iOS/Android: a node is premium when its tag carries the lock emoji or "premium".
bool isPremiumTag(const QString &name)
{
    return name.contains(QString::fromUtf8("\xF0\x9F\x94\x92"))
        || name.contains(QStringLiteral("premium"), Qt::CaseInsensitive);
}

QString countryFromKeywords(const QString &haystackLower)
{
    static const QVector<QPair<QString, QString>> keywords = {
        {QStringLiteral("美国"), QStringLiteral("US")},   {QStringLiteral("日本"), QStringLiteral("JP")},
        {QStringLiteral("韩国"), QStringLiteral("KR")},   {QStringLiteral("新加坡"), QStringLiteral("SG")},
        {QStringLiteral("香港"), QStringLiteral("HK")},   {QStringLiteral("台湾"), QStringLiteral("TW")},
        {QStringLiteral("台灣"), QStringLiteral("TW")},   {QStringLiteral("英国"), QStringLiteral("GB")},
        {QStringLiteral("德国"), QStringLiteral("DE")},   {QStringLiteral("法国"), QStringLiteral("FR")},
        {QStringLiteral("加拿大"), QStringLiteral("CA")}, {QStringLiteral("澳大利亚"), QStringLiteral("AU")},
        {QStringLiteral("澳洲"), QStringLiteral("AU")},   {QStringLiteral("荷兰"), QStringLiteral("NL")},
        {QStringLiteral("土耳其"), QStringLiteral("TR")}, {QStringLiteral("俄罗斯"), QStringLiteral("RU")},
        {QStringLiteral("印度"), QStringLiteral("IN")},   {QStringLiteral("狮城"), QStringLiteral("SG")},
        {QStringLiteral("united states"), QStringLiteral("US")},
        {QStringLiteral("america"), QStringLiteral("US")}, {QStringLiteral("usa"), QStringLiteral("US")},
        {QStringLiteral("japan"), QStringLiteral("JP")}, {QStringLiteral("tokyo"), QStringLiteral("JP")},
        {QStringLiteral("osaka"), QStringLiteral("JP")}, {QStringLiteral("korea"), QStringLiteral("KR")},
        {QStringLiteral("seoul"), QStringLiteral("KR")}, {QStringLiteral("singapore"), QStringLiteral("SG")},
        {QStringLiteral("hong kong"), QStringLiteral("HK")},
        {QStringLiteral("hongkong"), QStringLiteral("HK")},
        {QStringLiteral("taiwan"), QStringLiteral("TW")}, {QStringLiteral("taipei"), QStringLiteral("TW")},
        {QStringLiteral("united kingdom"), QStringLiteral("GB")},
        {QStringLiteral("britain"), QStringLiteral("GB")}, {QStringLiteral("london"), QStringLiteral("GB")},
        {QStringLiteral("germany"), QStringLiteral("DE")},
        {QStringLiteral("frankfurt"), QStringLiteral("DE")},
        {QStringLiteral("berlin"), QStringLiteral("DE")}, {QStringLiteral("france"), QStringLiteral("FR")},
        {QStringLiteral("paris"), QStringLiteral("FR")}, {QStringLiteral("canada"), QStringLiteral("CA")},
        {QStringLiteral("toronto"), QStringLiteral("CA")},
        {QStringLiteral("vancouver"), QStringLiteral("CA")},
        {QStringLiteral("australia"), QStringLiteral("AU")},
        {QStringLiteral("sydney"), QStringLiteral("AU")}, {QStringLiteral("india"), QStringLiteral("IN")},
        {QStringLiteral("mumbai"), QStringLiteral("IN")}, {QStringLiteral("russia"), QStringLiteral("RU")},
        {QStringLiteral("moscow"), QStringLiteral("RU")},
        {QStringLiteral("netherlands"), QStringLiteral("NL")},
        {QStringLiteral("amsterdam"), QStringLiteral("NL")},
        {QStringLiteral("turkey"), QStringLiteral("TR")},
        {QStringLiteral("istanbul"), QStringLiteral("TR")},
        {QStringLiteral("brazil"), QStringLiteral("BR")},
        {QStringLiteral("philippines"), QStringLiteral("PH")},
        {QStringLiteral("thailand"), QStringLiteral("TH")},
        {QStringLiteral("bangkok"), QStringLiteral("TH")},
        {QStringLiteral("vietnam"), QStringLiteral("VN")},
        {QStringLiteral("malaysia"), QStringLiteral("MY")},
        {QStringLiteral("indonesia"), QStringLiteral("ID")},
        {QStringLiteral("jakarta"), QStringLiteral("ID")},
        {QStringLiteral("finland"), QStringLiteral("FI")},
        {QStringLiteral("sweden"), QStringLiteral("SE")}, {QStringLiteral("norway"), QStringLiteral("NO")},
        {QStringLiteral("poland"), QStringLiteral("PL")}, {QStringLiteral("italy"), QStringLiteral("IT")},
        {QStringLiteral("spain"), QStringLiteral("ES")},
        {QStringLiteral("switzerland"), QStringLiteral("CH")},
        {QStringLiteral("austria"), QStringLiteral("AT")},
        {QStringLiteral("ireland"), QStringLiteral("IE")},
        {QStringLiteral("portugal"), QStringLiteral("PT")},
        {QStringLiteral("romania"), QStringLiteral("RO")},
        {QStringLiteral("hungary"), QStringLiteral("HU")}, {QStringLiteral("czech"), QStringLiteral("CZ")},
        {QStringLiteral("denmark"), QStringLiteral("DK")},
        {QStringLiteral("israel"), QStringLiteral("IL")},
        {QStringLiteral("ukraine"), QStringLiteral("UA")},
        {QStringLiteral("south africa"), QStringLiteral("ZA")},
        {QStringLiteral("argentina"), QStringLiteral("AR")},
        {QStringLiteral("mexico"), QStringLiteral("MX")}, {QStringLiteral("chile"), QStringLiteral("CL")},
        {QStringLiteral("colombia"), QStringLiteral("CO")},
        {QStringLiteral("los angeles"), QStringLiteral("US")},
        {QStringLiteral("new york"), QStringLiteral("US")},
        {QStringLiteral("chicago"), QStringLiteral("US")},
        {QStringLiteral("dallas"), QStringLiteral("US")},
        {QStringLiteral("seattle"), QStringLiteral("US")},
        {QStringLiteral("miami"), QStringLiteral("US")},
        {QStringLiteral("san jose"), QStringLiteral("US")},
        {QStringLiteral("ashburn"), QStringLiteral("US")},
        {QStringLiteral("virginia"), QStringLiteral("US")}};

    for (const auto &entry : keywords) {
        if (haystackLower.contains(entry.first))
            return entry.second;
    }
    return {};
}

/// Regional-indicator pairs (🇺🇸) decode straight back to their ISO country code.
QString countryFromFlagEmoji(const QString &name)
{
    const QList<uint> ucs4 = name.toUcs4();
    for (int i = 0; i + 1 < ucs4.size(); ++i) {
        const uint a = ucs4.at(i);
        const uint b = ucs4.at(i + 1);
        if (a >= 0x1F1E6 && a <= 0x1F1FF && b >= 0x1F1E6 && b <= 0x1F1FF) {
            const QChar c1(static_cast<char16_t>(a - 0x1F1E6 + 'A'));
            const QChar c2(static_cast<char16_t>(b - 0x1F1E6 + 'A'));
            return QString(c1) + QString(c2);
        }
    }
    return {};
}

QString detectCountryCode(const QString &name)
{
    const QString flag = countryFromFlagEmoji(name);
    if (!flag.isEmpty())
        return flag;

    // Hiddify-style tags look like "🇺🇸 1 - US - VLESS/WS/TLS - 443 § 0".
    static const QRegularExpression dashed(QStringLiteral(" - ([A-Z]{2}) - "));
    const auto dashedMatch = dashed.match(name);
    if (dashedMatch.hasMatch() && dashedMatch.captured(1) != QLatin1String("XX"))
        return dashedMatch.captured(1);

    const QString keyword = countryFromKeywords(name.toLower());
    if (!keyword.isEmpty())
        return keyword;

    static const QRegularExpression isoCode(
        QStringLiteral("(?<![A-Za-z])(US|JP|KR|SG|HK|TW|UK|GB|DE|FR|CA|AU|IN|RU|NL|TR|BR|PH|TH|VN"
                       "|MY|FI|SE|NO|PL|IT|ES|ID|CH|AT|IE|PT|RO|HU|IL|ZA|AR|CL|MX|CO|AE|UA|KZ|CZ"
                       "|DK)(?![A-Za-z])"));
    const auto isoMatch = isoCode.match(name.toUpper());
    if (isoMatch.hasMatch()) {
        const QString code = isoMatch.captured(1);
        return code == QLatin1String("UK") ? QStringLiteral("GB") : code;
    }
    return {};
}

QString detectCountryFromServer(const QString &server)
{
    if (server.isEmpty())
        return {};
    const QString lower = server.toLower();
    static const QHash<QString, QString> ccTLDs = {
        {QStringLiteral("jp"), QStringLiteral("JP")}, {QStringLiteral("kr"), QStringLiteral("KR")},
        {QStringLiteral("sg"), QStringLiteral("SG")}, {QStringLiteral("hk"), QStringLiteral("HK")},
        {QStringLiteral("tw"), QStringLiteral("TW")}, {QStringLiteral("uk"), QStringLiteral("GB")},
        {QStringLiteral("de"), QStringLiteral("DE")}, {QStringLiteral("fr"), QStringLiteral("FR")},
        {QStringLiteral("nl"), QStringLiteral("NL")}, {QStringLiteral("ca"), QStringLiteral("CA")},
        {QStringLiteral("au"), QStringLiteral("AU")}, {QStringLiteral("in"), QStringLiteral("IN")},
        {QStringLiteral("ru"), QStringLiteral("RU")}, {QStringLiteral("br"), QStringLiteral("BR")},
        {QStringLiteral("tr"), QStringLiteral("TR")}, {QStringLiteral("us"), QStringLiteral("US")},
        {QStringLiteral("my"), QStringLiteral("MY")}, {QStringLiteral("th"), QStringLiteral("TH")},
        {QStringLiteral("vn"), QStringLiteral("VN")}, {QStringLiteral("id"), QStringLiteral("ID")},
        {QStringLiteral("ph"), QStringLiteral("PH")}, {QStringLiteral("it"), QStringLiteral("IT")},
        {QStringLiteral("es"), QStringLiteral("ES")}, {QStringLiteral("se"), QStringLiteral("SE")},
        {QStringLiteral("fi"), QStringLiteral("FI")}, {QStringLiteral("no"), QStringLiteral("NO")},
        {QStringLiteral("dk"), QStringLiteral("DK")}, {QStringLiteral("pl"), QStringLiteral("PL")},
        {QStringLiteral("cz"), QStringLiteral("CZ")}, {QStringLiteral("at"), QStringLiteral("AT")},
        {QStringLiteral("ch"), QStringLiteral("CH")}, {QStringLiteral("ie"), QStringLiteral("IE")},
        {QStringLiteral("pt"), QStringLiteral("PT")}, {QStringLiteral("ro"), QStringLiteral("RO")},
        {QStringLiteral("hu"), QStringLiteral("HU")}, {QStringLiteral("il"), QStringLiteral("IL")},
        {QStringLiteral("za"), QStringLiteral("ZA")}, {QStringLiteral("ar"), QStringLiteral("AR")},
        {QStringLiteral("cl"), QStringLiteral("CL")}, {QStringLiteral("mx"), QStringLiteral("MX")},
        {QStringLiteral("co"), QStringLiteral("CO")}, {QStringLiteral("ae"), QStringLiteral("AE")},
        {QStringLiteral("ua"), QStringLiteral("UA")}, {QStringLiteral("kz"), QStringLiteral("KZ")}};

    const QStringList parts = lower.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (!parts.isEmpty()) {
        const QString tld = parts.last();
        if (ccTLDs.contains(tld))
            return ccTLDs.value(tld);
    }

    const QString keyword = countryFromKeywords(lower);
    if (!keyword.isEmpty())
        return keyword;

    for (const QString &part : parts) {
        for (auto it = ccTLDs.constBegin(); it != ccTLDs.constEnd(); ++it) {
            if (part == it.key() || part.startsWith(it.key() + QLatin1Char('-'))
                || part.endsWith(QLatin1Char('-') + it.key()))
                return it.value();
        }
    }
    return {};
}

void finishNode(ProxyNode &node, int index)
{
    node.id = QStringLiteral("proxy_%1").arg(index);
    node.countryCode = detectCountryCode(node.name);
    if (node.countryCode.isEmpty())
        node.countryCode = detectCountryFromServer(node.address);
    node.isPremium = isPremiumTag(node.name);
}

bool parseVmessNode(const QString &line, int index, ProxyNode *out)
{
    const QString json = tryBase64Decode(line.mid(line.indexOf(QStringLiteral("://")) + 3).trimmed());
    if (json.isEmpty())
        return false;
    const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
    if (obj.isEmpty())
        return false;

    out->name = obj.value(QStringLiteral("ps")).toString().trimmed();
    if (out->name.isEmpty())
        out->name = QStringLiteral("VMess #%1").arg(index + 1);
    out->type = QStringLiteral("VMess");
    out->address = obj.value(QStringLiteral("add")).toString();
    const QJsonValue port = obj.value(QStringLiteral("port"));
    out->port = static_cast<quint16>(port.isString() ? port.toString().toInt() : port.toInt());
    finishNode(*out, index);
    return true;
}

bool parseShadowsocksNode(const QString &line, int index, ProxyNode *out)
{
    QString rest = line.mid(line.indexOf(QStringLiteral("://")) + 3).trimmed();
    QString fragment;
    const int hash = rest.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        fragment = QUrl::fromPercentEncoding(rest.mid(hash + 1).toUtf8()).trimmed();
        rest = rest.left(hash);
    }
    const int question = rest.indexOf(QLatin1Char('?'));
    if (question >= 0)
        rest = rest.left(question);

    // Either `ss://base64(method:pass@host:port)` or `ss://base64(method:pass)@host:port`.
    QString authority = rest;
    if (!authority.contains(QLatin1Char('@'))) {
        const QString decoded = tryBase64Decode(authority);
        if (!decoded.isEmpty())
            authority = decoded;
    }
    const int at = authority.lastIndexOf(QLatin1Char('@'));
    if (at < 0)
        return false;
    const QString hostPort = authority.mid(at + 1);
    const int colon = hostPort.lastIndexOf(QLatin1Char(':'));
    if (colon < 0)
        return false;

    out->name = fragment.isEmpty() ? QStringLiteral("Shadowsocks #%1").arg(index + 1) : fragment;
    out->type = QStringLiteral("Shadowsocks");
    out->address = hostPort.left(colon);
    out->port = static_cast<quint16>(hostPort.mid(colon + 1).toInt());
    finishNode(*out, index);
    return true;
}

bool parseGenericNode(const QString &line, int index, const QString &type, ProxyNode *out)
{
    const QUrl url(line);
    const QString host = url.host();
    if (host.isEmpty())
        return false;
    const QString fragment = url.fragment(QUrl::FullyDecoded).trimmed();

    out->name = fragment.isEmpty() ? QStringLiteral("%1 #%2").arg(type).arg(index + 1) : fragment;
    out->type = type;
    out->address = host;
    out->port = static_cast<quint16>(url.port(0));
    finishNode(*out, index);
    return true;
}

bool parseProxyLine(const QString &line, int index, ProxyNode *out)
{
    const int idx = line.indexOf(QStringLiteral("://"));
    if (idx <= 0)
        return false;
    const QString scheme = line.left(idx).toLower();

    if (scheme == QLatin1String("vmess"))
        return parseVmessNode(line, index, out);
    if (scheme == QLatin1String("ss"))
        return parseShadowsocksNode(line, index, out);

    static const QHash<QString, QString> generic = {
        {QStringLiteral("vless"), QStringLiteral("VLESS")},
        {QStringLiteral("trojan"), QStringLiteral("Trojan")},
        {QStringLiteral("ssr"), QStringLiteral("ShadowsocksR")},
        {QStringLiteral("hysteria"), QStringLiteral("Hysteria")},
        {QStringLiteral("hy"), QStringLiteral("Hysteria")},
        {QStringLiteral("hysteria2"), QStringLiteral("Hysteria2")},
        {QStringLiteral("hy2"), QStringLiteral("Hysteria2")},
        {QStringLiteral("tuic"), QStringLiteral("TUIC")},
        {QStringLiteral("anytls"), QStringLiteral("AnyTLS")},
        {QStringLiteral("wg"), QStringLiteral("WireGuard")},
        {QStringLiteral("ssh"), QStringLiteral("SSH")}};
    const QString type = generic.value(scheme);
    if (type.isEmpty())
        return false;
    return parseGenericNode(line, index, type, out);
}

/// sing-box outbounds/endpoints. Group entries (selector/urltest/…) are not nodes.
QVector<ProxyNode> parseSingboxJson(const QString &json)
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    const QJsonObject root = doc.object();

    // sing-box 1.11+ moved WireGuard/WARP out of `outbounds` into a top-level `endpoints` array,
    // so WARP profiles keep all their real nodes there while outbounds holds only the groups.
    QJsonArray entries = root.value(QStringLiteral("outbounds")).toArray();
    const QJsonArray endpoints = root.value(QStringLiteral("endpoints")).toArray();
    for (const QJsonValue &value : endpoints)
        entries.append(value);
    if (entries.isEmpty())
        return {};

    static const QSet<QString> skipTypes = {
        QStringLiteral("selector"),  QStringLiteral("urltest"),  QStringLiteral("url-test"),
        QStringLiteral("direct"),    QStringLiteral("block"),    QStringLiteral("dns"),
        QStringLiteral("fallback"),  QStringLiteral("balancer"), QStringLiteral("load-balance")};
    static const QSet<QString> proxyTypes = {
        QStringLiteral("vmess"),     QStringLiteral("vless"),     QStringLiteral("trojan"),
        QStringLiteral("shadowsocks"), QStringLiteral("ss"),      QStringLiteral("shadowtls"),
        QStringLiteral("hysteria"),  QStringLiteral("hysteria2"), QStringLiteral("wireguard"),
        QStringLiteral("warp"),      QStringLiteral("tuic"),      QStringLiteral("ssh"),
        QStringLiteral("anytls"),    QStringLiteral("socks"),     QStringLiteral("http")};

    QVector<ProxyNode> nodes;
    int index = 0;
    for (const QJsonValue &value : std::as_const(entries)) {
        const QJsonObject obj = value.toObject();
        const QString type = obj.value(QStringLiteral("type")).toString().toLower();
        const QString tag = obj.value(QStringLiteral("tag")).toString();
        if (type.isEmpty() || tag.isEmpty())
            continue;
        if (skipTypes.contains(type) || !proxyTypes.contains(type))
            continue;

        ProxyNode node;
        node.name = tag;
        node.type = displayProxyType(type);
        node.address = obj.value(QStringLiteral("server")).toString();
        node.port = static_cast<quint16>(obj.value(QStringLiteral("server_port")).toInt());

        // WireGuard endpoints carry their address under `peers`.
        if (node.address.isEmpty()) {
            const QJsonArray peers = obj.value(QStringLiteral("peers")).toArray();
            if (!peers.isEmpty()) {
                const QJsonObject peer = peers.first().toObject();
                node.address = peer.value(QStringLiteral("address")).toString();
                if (node.address.isEmpty())
                    node.address = peer.value(QStringLiteral("server")).toString();
                node.port = static_cast<quint16>(peer.value(QStringLiteral("port")).toInt(
                    peer.value(QStringLiteral("server_port")).toInt()));
            }
        }

        finishNode(node, index);
        nodes.append(node);
        ++index;
    }
    return nodes;
}

QVector<ProxyNode> parseProxyNodes(const QString &content)
{
    const QString trimmed = content.trimmed();
    if (trimmed.isEmpty())
        return {};
    if (trimmed.startsWith(QLatin1Char('{')))
        return parseSingboxJson(trimmed);

    const QString decoded = decodeSubscriptionBody(trimmed);
    if (decoded.trimmed().startsWith(QLatin1Char('{')))
        return parseSingboxJson(decoded.trimmed());

    QVector<ProxyNode> nodes;
    int index = 0;
    const QStringList lines = decoded.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (t.isEmpty())
            continue;
        ProxyNode node;
        if (parseProxyLine(t, index, &node)) {
            nodes.append(node);
            ++index;
        }
    }
    return nodes;
}

/// One in-flight TCP probe. Owns nothing the model needs after `applyDelay`.
struct DelayProbe {
    QTcpSocket *socket = nullptr;
    QTimer *timer = nullptr;
    QElapsedTimer clock;
    QString id;
    bool done = false;
};

} // namespace

ProxyListModel *ProxyListModel::s_instance = nullptr;

ProxyListModel::ProxyListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    s_instance = this;
    m_clashApi = new ClashApi(this);

    // ProxiesScreen lists filterModel, never this model directly.
    m_filterModel = new ProxyFilterModel(this);
    m_filterModel->setSourceModel(this);

    // main.cpp constructs this model before ProfileListModel, so bind once the event loop runs —
    // by then every singleton exists regardless of construction order.
    QMetaObject::invokeMethod(this, [this]() { bindToProfiles(); }, Qt::QueuedConnection);
}

ProxyListModel::~ProxyListModel()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void ProxyListModel::bindToProfiles()
{
    auto *profiles = ProfileListModel::instance();
    if (!profiles)
        return;
    connect(profiles, &ProfileListModel::activeProfileChanged, this, &ProxyListModel::reload);
    reload();
}

int ProxyListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_proxies.size();
}

QVariant ProxyListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_proxies.size())
        return {};

    const auto &p = m_proxies[index.row()];
    switch (role) {
    case IdRole: return p.id;
    case NameRole: return p.name;
    case TypeRole: return p.type;
    case AddressRole: return p.address;
    case CountryCodeRole: return p.countryCode;
    case DelayRole: return p.delay;
    case IsPremiumRole: return p.isPremium;
    case PortRole: return p.port;
    }
    return {};
}

QHash<int, QByteArray> ProxyListModel::roleNames() const
{
    return {
        {IdRole, "proxyId"},
        {NameRole, "name"},
        {TypeRole, "type"},
        {AddressRole, "address"},
        {CountryCodeRole, "countryCode"},
        {DelayRole, "delay"},
        {IsPremiumRole, "isPremium"},
        {PortRole, "port"}
    };
}

QString ProxyListModel::selectedId() const { return m_selectedId; }

void ProxyListModel::setSelectedId(const QString &id)
{
    if (m_selectedId != id) {
        m_selectedId = id;
        emit selectedIdChanged();
    }
}

void ProxyListModel::selectProxy(const QString &id)
{
    const QString previous = m_selectedId;
    setSelectedId(id);
    if (previous != m_selectedId)
        applySelectionToCore();
}

void ProxyListModel::selectProxy(int row)
{
    if (row < 0 || row >= m_proxies.size())
        return;
    selectProxy(m_proxies[row].id);
}

void ProxyListModel::applySelectionToCore()
{
    // Only meaningful while the core is up: the "select" group exists inside the config it
    // built. When it is not, the choice is still stored and ConnectionModel replays it on the
    // next successful connect, so staying quiet here is correct rather than a swallowed error.
    const ConnectionModel *conn = ConnectionModel::instance();
    if (!conn || conn->status() != ConnectionModel::Connected)
        return;

    const QString name = selectedName();
    if (name.isEmpty())
        return;

    const QString group = ClashApi::mainSelectorTag();
    m_clashApi->fetchGroupMembers(
        group,
        [this, group, name](const QStringList &members) {
            const QString tag = resolveCoreTag(name, members);
            if (tag.isEmpty()) {
                emit selectionFailed(QStringLiteral(
                    "The running tunnel has no node called \"%1\" — reconnect to pick up the "
                    "current profile.").arg(name));
                return;
            }
            m_clashApi->selectOutbound(
                group, tag,
                [this, name]() { emit selectionApplied(name); },
                [this, name](const QString &error) {
                    emit selectionFailed(
                        QStringLiteral("Could not switch to \"%1\": %2").arg(name, error));
                });
        },
        [this, name](const QString &error) {
            emit selectionFailed(
                QStringLiteral("Could not switch to \"%1\": %2").arg(name, error));
        });
}

QString ProxyListModel::resolveCoreTag(const QString &name, const QStringList &members)
{
    if (members.contains(name))
        return name;

    // ray2sing renames every share-link outbound to "<fragment> § <n>" while building the
    // config, so the tag the core knows is almost never the one parsed out of the profile.
    const QString suffixed = name + QStringLiteral(" § ");
    for (const QString &member : members) {
        if (member.startsWith(suffixed))
            return member;
    }
    // Profiles whose own tags already carry a "§" section keep only the part before it.
    for (const QString &member : members) {
        if (member.section(QStringLiteral("§"), 0, 0).trimmed() == name.trimmed())
            return member;
    }
    return {};
}

QString ProxyListModel::selectedName() const
{
    for (const auto &p : m_proxies) {
        if (p.id == m_selectedId)
            return p.name;
    }
    return {};
}

void ProxyListModel::reload()
{
    // The active profile's own content wins: falling back to the core's written config while a
    // different profile is selected would show whatever was last CONNECTED.
    auto *profiles = ProfileListModel::instance();
    const QString content = profiles ? profiles->activeProfileContent() : QString();

    QVector<ProxyNode> nodes = parseProxyNodes(content);
    if (nodes.isEmpty())
        nodes = parseProxyNodes(coreConfigContent());

    setNodes(nodes);
}

void ProxyListModel::setNodes(const QVector<ProxyNode> &nodes)
{
    m_pendingTests.clear();
    ++m_generation;

    beginResetModel();
    m_proxies = nodes;
    endResetModel();
    emit countChanged();

    bool stillPresent = false;
    for (const auto &p : m_proxies) {
        if (p.id == m_selectedId) {
            stillPresent = true;
            break;
        }
    }
    if (!stillPresent)
        setSelectedId(m_proxies.isEmpty() ? QString() : m_proxies.first().id);
}

QString ProxyListModel::coreConfigContent() const
{
    // VpnCore writes the running config here (see VpnCore::launchEngine).
    const QString path = VpnCore::runningConfigPath();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QByteArray raw = file.readAll();
    file.close();
    return QString::fromUtf8(raw);
}

void ProxyListModel::testAllDelays()
{
    if (m_testing || m_proxies.isEmpty())
        return;

    m_pendingTests.clear();
    QVector<int> unreachableRows;
    for (int i = 0; i < m_proxies.size(); ++i) {
        const auto &p = m_proxies[i];
        if (p.address.isEmpty() || p.port == 0) {
            // Nothing to dial (e.g. a WireGuard endpoint without a peer address).
            if (m_proxies[i].delay != UnreachableDelay)
                unreachableRows.append(i);
            continue;
        }
        m_pendingTests.append({p.id, p.address, p.port});
    }

    for (int row : std::as_const(unreachableRows)) {
        m_proxies[row].delay = UnreachableDelay;
        emit dataChanged(index(row), index(row), {DelayRole});
    }

    if (m_pendingTests.isEmpty()) {
        emit delayTestFinished();
        return;
    }

    m_testing = true;
    emit testingChanged();
    m_activeTests = 0;
    pumpDelayTests();
}

void ProxyListModel::pumpDelayTests()
{
    while (m_activeTests < kMaxConcurrentProbes && !m_pendingTests.isEmpty()) {
        const PendingProbe probe = m_pendingTests.takeFirst();
        startDelayTest(probe.id, probe.host, probe.port);
    }

    if (m_testing && m_activeTests == 0 && m_pendingTests.isEmpty()) {
        m_testing = false;
        emit testingChanged();
        emit delayTestFinished();
    }
}

void ProxyListModel::startDelayTest(const QString &id, const QString &host, quint16 port)
{
    const quint64 generation = m_generation;
    auto *probe = new DelayProbe;
    probe->id = id;
    probe->socket = new QTcpSocket(this);
    probe->timer = new QTimer(this);
    probe->timer->setSingleShot(true);
    probe->clock.start();
    ++m_activeTests;

    auto finish = [this, probe, generation](int delay) {
        if (probe->done)
            return;
        probe->done = true;
        // Drop our handlers first: abort() can still emit errorOccurred, and the handler would
        // then dereference a probe this call is about to delete.
        probe->socket->disconnect(this);
        probe->timer->disconnect(this);
        probe->timer->stop();
        probe->socket->abort();
        probe->socket->deleteLater();
        probe->timer->deleteLater();
        const QString probeId = probe->id;
        delete probe;

        applyDelay(probeId, delay, generation);
        --m_activeTests;
        pumpDelayTests();
    };

    connect(probe->socket, &QTcpSocket::connected, this, [finish, probe]() {
        finish(static_cast<int>(probe->clock.elapsed()));
    });
    connect(probe->socket, &QTcpSocket::errorOccurred, this,
            [finish](QAbstractSocket::SocketError) { finish(ProxyListModel::UnreachableDelay); });
    connect(probe->timer, &QTimer::timeout, this,
            [finish]() { finish(ProxyListModel::UnreachableDelay); });

    probe->timer->start(kProbeTimeoutMs);
    probe->socket->connectToHost(host, port);
}

void ProxyListModel::applyDelay(const QString &id, int delay, quint64 generation)
{
    if (generation != m_generation)
        return; // The list was reloaded while this probe was in flight.
    for (int i = 0; i < m_proxies.size(); ++i) {
        if (m_proxies[i].id != id)
            continue;
        if (m_proxies[i].delay == delay)
            return;
        m_proxies[i].delay = delay;
        emit dataChanged(index(i), index(i), {DelayRole});
        return;
    }
}

ProxyFilterModel::ProxyFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // The footer count is bound to this model, so every shape change has to notify.
    connect(this, &QAbstractItemModel::rowsInserted, this, &ProxyFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ProxyFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ProxyFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ProxyFilterModel::countChanged);
}

void ProxyFilterModel::setFilterText(const QString &text)
{
    if (m_filterText == text)
        return;
    beginFilterChange();
    m_filterText = text;
    endFilterChange();
    emit filterTextChanged();
    emit countChanged();
}

bool ProxyFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_filterText.isEmpty() || !sourceModel())
        return true;

    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    const int roles[] = {ProxyListModel::NameRole, ProxyListModel::TypeRole,
                         ProxyListModel::AddressRole, ProxyListModel::CountryCodeRole};
    for (int role : roles) {
        if (sourceModel()->data(idx, role).toString().contains(m_filterText, Qt::CaseInsensitive))
            return true;
    }
    return false;
}
