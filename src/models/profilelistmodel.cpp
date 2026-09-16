#include "profilelistmodel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#include <chrono>
#include <utility>

ProfileListModel *ProfileListModel::s_instance = nullptr;

namespace {

const char *const kUserAgent = "AiBooster/2.0";
constexpr int kDownloadTimeoutMs = 15000;
constexpr double kBytesPerGB = 1024.0 * 1024.0 * 1024.0;

QStringList directConfigPrefixes()
{
    return {QStringLiteral("vmess://"),     QStringLiteral("vless://"), QStringLiteral("ss://"),
            QStringLiteral("ssr://"),       QStringLiteral("trojan://"), QStringLiteral("hysteria://"),
            QStringLiteral("hysteria2://"), QStringLiteral("hy://"),     QStringLiteral("hy2://"),
            QStringLiteral("tuic://"),      QStringLiteral("wg://"),     QStringLiteral("ssh://")};
}

bool isProtocolLink(const QString &line)
{
    const QStringList prefixes = directConfigPrefixes();
    for (const QString &prefix : prefixes) {
        if (line.startsWith(prefix, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

/// Strict base64 decode that also accepts the URL-safe alphabet and missing padding.
/// Returns an empty string when the input is not valid base64 text.
QString tryBase64Decode(const QString &input)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QString compact = input;
    compact.remove(whitespace);
    if (compact.size() < 8)
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

/// Subscriptions are usually served as one long base64 line; JSON configs and plain link lists
/// are passed through untouched.
QString decodeSubscriptionBody(const QString &content)
{
    const QString trimmed = content.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('{')) || trimmed.startsWith(QLatin1Char('[')))
        return trimmed;

    const QStringList lines = trimmed.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (isProtocolLink(line.trimmed()))
            return trimmed;
    }

    const QString decoded = tryBase64Decode(trimmed);
    if (decoded.isEmpty())
        return trimmed;
    if (decoded.contains(QStringLiteral("://")) || decoded.contains(QLatin1Char('{'))
        || decoded.contains(QLatin1Char('#')) || decoded.contains(QStringLiteral("[Interface]")))
        return decoded;
    return trimmed;
}

/// `profile-title` may be sent verbatim or as `base64:<payload>`.
QString decodeProfileTitle(const QString &headerValue)
{
    const QString trimmed = headerValue.trimmed();
    if (trimmed.isEmpty())
        return {};
    if (trimmed.startsWith(QStringLiteral("base64:"), Qt::CaseInsensitive)) {
        const QString encoded = trimmed.mid(7).trimmed();
        if (encoded.isEmpty())
            return {};
        const auto result = QByteArray::fromBase64Encoding(encoded.toLatin1());
        if (!result)
            return {};
        return QString::fromUtf8(result.decoded).trimmed();
    }
    return trimmed;
}

/// Some providers put their headers in `#key: value` comment lines at the top of the body.
QHash<QString, QString> parseContentHeaders(const QString &content)
{
    QHash<QString, QString> headers;
    const QStringList lines = decodeSubscriptionBody(content).split(QLatin1Char('\n'));
    const int limit = qMin(lines.size(), 10);
    for (int i = 0; i < limit; ++i) {
        QString line = lines.at(i).trimmed();
        if (!line.startsWith(QLatin1Char('#')) && !line.startsWith(QStringLiteral("//")))
            continue;
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        QString key = line.left(colon);
        while (key.startsWith(QLatin1Char('#')) || key.startsWith(QLatin1Char('/')))
            key.remove(0, 1);
        key = key.trimmed().toLower();
        const QString value = line.mid(colon + 1).trimmed();
        if (!key.isEmpty() && !value.isEmpty())
            headers.insert(key, value);
    }
    return headers;
}

QString stripConfigSuffix(QString name)
{
    const QStringList suffixes = {QStringLiteral(".json"), QStringLiteral(".yaml"),
                                  QStringLiteral(".yml"), QStringLiteral(".txt")};
    for (const QString &suffix : suffixes) {
        if (name.endsWith(suffix, Qt::CaseInsensitive)) {
            name.chop(suffix.size());
            break;
        }
    }
    return name.trimmed();
}

QString parseContentDisposition(const QString &headerValue)
{
    if (headerValue.trimmed().isEmpty())
        return {};
    static const QRegularExpression re(QStringLiteral("filename\\*?=(?:UTF-8''|\")?([^\";]+)\"?"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto match = re.match(headerValue);
    if (!match.hasMatch())
        return {};
    return stripConfigSuffix(QUrl::fromPercentEncoding(match.captured(1).toUtf8()));
}

QString parseUrlFragment(const QString &url)
{
    const QUrl parsed(url);
    return parsed.fragment(QUrl::FullyDecoded).trimmed();
}

QString parseUrlFilename(const QString &url)
{
    const QUrl parsed(url);
    const QString path = parsed.path();
    if (path.isEmpty())
        return {};
    QString filename = path.section(QLatin1Char('/'), -1);
    filename = stripConfigSuffix(filename);
    return filename == QLatin1String("/") ? QString() : filename;
}

QString detectProtocolFromLine(const QString &line)
{
    const int idx = line.indexOf(QStringLiteral("://"));
    if (idx <= 0)
        return {};
    const QString scheme = line.left(idx).toLower();
    static const QHash<QString, QString> names = {
        {QStringLiteral("vmess"), QStringLiteral("VMess")},
        {QStringLiteral("vless"), QStringLiteral("VLESS")},
        {QStringLiteral("ss"), QStringLiteral("Shadowsocks")},
        {QStringLiteral("ssconf"), QStringLiteral("Shadowsocks")},
        {QStringLiteral("ssr"), QStringLiteral("ShadowsocksR")},
        {QStringLiteral("trojan"), QStringLiteral("Trojan")},
        {QStringLiteral("hysteria"), QStringLiteral("Hysteria")},
        {QStringLiteral("hy"), QStringLiteral("Hysteria")},
        {QStringLiteral("hysteria2"), QStringLiteral("Hysteria2")},
        {QStringLiteral("hy2"), QStringLiteral("Hysteria2")},
        {QStringLiteral("tuic"), QStringLiteral("TUIC")},
        {QStringLiteral("wg"), QStringLiteral("WireGuard")},
        {QStringLiteral("ssh"), QStringLiteral("SSH")}};
    return names.value(scheme);
}

QString detectProtocolFromContent(const QString &content)
{
    const QString decoded = decodeSubscriptionBody(content);
    const QStringList lines = decoded.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString protocol = detectProtocolFromLine(line.trimmed());
        if (!protocol.isEmpty())
            return protocol;
    }
    if (decoded.contains(QStringLiteral("[Interface]")))
        return QStringLiteral("WireGuard");
    if (decoded.trimmed().startsWith(QLatin1Char('{')))
        return QStringLiteral("AI Booster config");
    return {};
}

QString parseVmessName(const QString &vmessUrl)
{
    const QString payload = vmessUrl.mid(vmessUrl.indexOf(QStringLiteral("://")) + 3).trimmed();
    const QString json = tryBase64Decode(payload);
    if (json.isEmpty())
        return {};
    const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
    return obj.value(QStringLiteral("ps")).toString().trimmed();
}

QString parseConfigLineName(const QString &line)
{
    if (line.startsWith(QStringLiteral("vmess://"), Qt::CaseInsensitive))
        return parseVmessName(line);
    const int hash = line.indexOf(QLatin1Char('#'));
    if (hash < 0)
        return {};
    return QUrl::fromPercentEncoding(line.mid(hash + 1).toUtf8()).trimmed();
}

QHash<QString, QString> parseQueryParams(const QString &query)
{
    QHash<QString, QString> params;
    QString body = query;
    const int mark = body.indexOf(QLatin1Char('?'));
    if (mark >= 0)
        body = body.mid(mark + 1);
    const QStringList pairs = body.split(QLatin1Char('&'), Qt::SkipEmptyParts);
    for (const QString &pair : pairs) {
        const int eq = pair.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = QUrl::fromPercentEncoding(pair.left(eq).toUtf8());
        const QString value = QUrl::fromPercentEncoding(pair.mid(eq + 1).toUtf8());
        if (!key.isEmpty())
            params.insert(key, value);
    }
    return params;
}

SubscriptionInfo parseSubscriptionInfo(const QString &header)
{
    SubscriptionInfo info;
    qint64 upload = 0, download = 0, total = 0, expire = 0;
    const QStringList parts = header.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = part.left(eq).trimmed().toLower();
        bool ok = false;
        const qint64 value = part.mid(eq + 1).trimmed().toLongLong(&ok);
        if (!ok)
            continue;
        info.valid = true;
        if (key == QLatin1String("upload"))
            upload = value;
        else if (key == QLatin1String("download"))
            download = value;
        else if (key == QLatin1String("total"))
            total = value;
        else if (key == QLatin1String("expire"))
            expire = value;
    }
    if (!info.valid)
        return info;

    info.totalGB = static_cast<double>(total) / kBytesPerGB;
    info.usedGB = static_cast<double>(upload + download) / kBytesPerGB;
    if (expire > 0) {
        const qint64 remaining = expire - QDateTime::currentSecsSinceEpoch();
        info.remainingDays = static_cast<int>(qMax<qint64>(0, remaining / 86400));
    } else {
        info.remainingDays = -1;
    }
    return info;
}

QString formatTraffic(double gb)
{
    if (gb <= 0)
        return QStringLiteral("0 GB");
    if (gb >= 1024.0)
        return QString::number(gb / 1024.0, 'f', 2) + QStringLiteral(" TB");
    if (gb >= 10.0)
        return QString::number(gb, 'f', 1) + QStringLiteral(" GB");
    if (gb >= 1.0)
        return QString::number(gb, 'f', 2) + QStringLiteral(" GB");
    return QString::number(gb * 1024.0, 'f', 0) + QStringLiteral(" MB");
}

} // namespace

ProfileListModel::ProfileListModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_network(new QNetworkAccessManager(this))
{
    s_instance = this;
    load();
    m_lastActiveId = activeProfileId();
}

ProfileListModel::~ProfileListModel()
{
    if (s_instance == this)
        s_instance = nullptr;
}

int ProfileListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_profiles.size();
}

QVariant ProfileListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_profiles.size())
        return {};

    const auto &p = m_profiles[index.row()];
    switch (role) {
    case IdRole: return p.id;
    case NameRole: return p.name;
    case UrlRole: return p.url;
    case IsActiveRole: return p.isActive;
    case IsPremiumRole: return p.isPremium;
    case TotalTrafficRole: return formatTraffic(p.totalTrafficGB);
    case UsedTrafficRole: return formatTraffic(p.usedTrafficGB);
    case RemainingDaysRole: return p.remainingDays;
    case TrafficProgressRole:
        return p.totalTrafficGB > 0 ? qBound(0.0, p.usedTrafficGB / p.totalTrafficGB, 1.0) : 0.0;
    case TotalTrafficGBRole: return p.totalTrafficGB;
    case UsedTrafficGBRole: return p.usedTrafficGB;
    case HasContentRole: return !p.content.isEmpty();
    case LastUpdatedRole: return p.lastUpdated;
    }
    return {};
}

QHash<int, QByteArray> ProfileListModel::roleNames() const
{
    return {
        {IdRole, "profileId"},
        {NameRole, "name"},
        {UrlRole, "url"},
        {IsActiveRole, "isActive"},
        {IsPremiumRole, "isPremium"},
        {TotalTrafficRole, "totalTraffic"},
        {UsedTrafficRole, "usedTraffic"},
        {RemainingDaysRole, "remainingDays"},
        {TrafficProgressRole, "trafficProgress"},
        {TotalTrafficGBRole, "totalTrafficGB"},
        {UsedTrafficGBRole, "usedTrafficGB"},
        {HasContentRole, "hasContent"},
        {LastUpdatedRole, "lastUpdated"}
    };
}

QString ProfileListModel::activeProfileId() const
{
    for (const auto &p : m_profiles) {
        if (p.isActive)
            return p.id;
    }
    return {};
}

QString ProfileListModel::activeProfileName() const
{
    for (const auto &p : m_profiles) {
        if (p.isActive)
            return p.name;
    }
    return {};
}

QString ProfileListModel::activeProfileContent() const
{
    for (const auto &p : m_profiles) {
        if (p.isActive)
            return p.content;
    }
    return {};
}

QString ProfileListModel::profileContent(const QString &id) const
{
    const int row = indexOfId(id);
    return row < 0 ? QString() : m_profiles[row].content;
}

int ProfileListModel::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].id == id)
            return i;
    }
    return -1;
}

void ProfileListModel::setActive(const QString &id)
{
    bool changed = false;
    for (int i = 0; i < m_profiles.size(); ++i) {
        const bool shouldBeActive = (m_profiles[i].id == id);
        if (m_profiles[i].isActive != shouldBeActive) {
            m_profiles[i].isActive = shouldBeActive;
            emit dataChanged(index(i), index(i), {IsActiveRole});
            changed = true;
        }
    }
    if (!changed)
        return;
    save();
    m_lastActiveId = activeProfileId();
    emit activeProfileChanged();
}

void ProfileListModel::setActive(int row)
{
    // ProfilesScreen's delegate passes the row index rather than the profile id.
    if (row < 0 || row >= m_profiles.size())
        return;
    setActive(m_profiles[row].id);
}

void ProfileListModel::deleteProfile(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;

    const bool wasActive = m_profiles[row].isActive;
    beginRemoveRows(QModelIndex(), row, row);
    m_profiles.removeAt(row);
    endRemoveRows();
    save();
    emit countChanged();
    if (wasActive) {
        m_lastActiveId.clear();
        emit activeProfileChanged();
    }
}

void ProfileListModel::deleteProfile(int row)
{
    if (row < 0 || row >= m_profiles.size())
        return;
    deleteProfile(m_profiles[row].id);
}

void ProfileListModel::refreshProfiles()
{
    // Re-download every remote profile. Direct-config profiles have no URL and are left alone.
    const QVector<ProfileData> snapshot = m_profiles;
    for (const auto &p : snapshot) {
        if (!p.url.isEmpty())
            downloadProfile(p.url, p.name);
    }
}

void ProfileListModel::updateProfile(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;
    const ProfileData target = m_profiles[row];
    if (target.url.isEmpty()) {
        emit profileError(QStringLiteral("\"%1\" has no subscription URL to update").arg(target.name));
        return;
    }
    downloadProfile(target.url, target.name);
}

void ProfileListModel::updateProfile(int row)
{
    if (row < 0 || row >= m_profiles.size())
        return;
    updateProfile(m_profiles[row].id);
}

void ProfileListModel::renameProfile(const QString &id, const QString &name)
{
    const int row = indexOfId(id);
    if (row < 0 || name.trimmed().isEmpty() || m_profiles[row].name == name.trimmed())
        return;
    m_profiles[row].name = name.trimmed();
    emit dataChanged(index(row), index(row), {NameRole});
    save();
}


QString ProfileListModel::subscriptionUrlFromDeepLink(const QString &input, QString *nameOut,
                                                      bool *malformed)
{
    if (malformed)
        *malformed = false;

    const QString trimmed = input.trimmed();

    // A direct protocol link (vless://, ss://, …) is a config, never a deep link — check
    // first so one can never be mistaken for the other.
    if (isProtocolLink(trimmed))
        return {};

    // Plain subscription URLs are downloaded as they are.
    if (trimmed.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        || trimmed.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        return {};
    }

    static const QRegularExpression schemeRe(QStringLiteral("^[A-Za-z][A-Za-z0-9+.\\-]*://"));
    const QRegularExpressionMatch match = schemeRe.match(trimmed);
    if (!match.hasMatch())
        return {};

    QString rest = trimmed.mid(match.capturedLength());
    // Several clients put the payload behind an "import" path segment.
    if (rest.startsWith(QStringLiteral("import/")) || rest.startsWith(QStringLiteral("import?")))
        rest = rest.mid(7);

    const QHash<QString, QString> params = parseQueryParams(rest);
    const QString url = params.value(QStringLiteral("url"));
    if (url.isEmpty()) {
        // It looked like a deep link but carries nothing to fetch. Saying so beats storing
        // the raw text as a profile that can never connect.
        if (malformed)
            *malformed = true;
        return {};
    }

    if (nameOut)
        *nameOut = params.value(QStringLiteral("name"));
    return url;
}

void ProfileListModel::addProfile(const QString &urlOrContent, const QString &overrideName)
{
    const QString trimmed = urlOrContent.trimmed();
    if (trimmed.isEmpty()) {
        emit profileError(QStringLiteral("Nothing to add — the input is empty"));
        return;
    }

    // Client deep links carry the real subscription URL in a `url=` parameter.
    QString linkName;
    bool malformedDeepLink = false;
    const QString deepLinkUrl = subscriptionUrlFromDeepLink(trimmed, &linkName, &malformedDeepLink);
    if (malformedDeepLink) {
        emit profileError(QStringLiteral("Deep link carries no subscription URL"));
        return;
    }
    if (!deepLinkUrl.isEmpty()) {
        downloadProfile(deepLinkUrl, overrideName.isEmpty() ? linkName : overrideName);
        return;
    }

    const QString firstLine = trimmed.split(QLatin1Char('\n')).value(0).trimmed();
    if (isProtocolLink(firstLine)) {
        addDirectConfig(trimmed, overrideName);
        return;
    }

    if (trimmed.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        || trimmed.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        QString name = overrideName;
        if (name.isEmpty()) {
            const QUrl parsed(trimmed);
            name = parseQueryParams(parsed.query()).value(QStringLiteral("name"));
            if (name.isEmpty())
                name = parsed.fragment(QUrl::FullyDecoded);
        }
        downloadProfile(trimmed, name);
        return;
    }

    addRawContent(trimmed, overrideName);
}

void ProfileListModel::downloadProfile(const QString &url, const QString &overrideName)
{
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.host().isEmpty()) {
        emit profileError(QStringLiteral("Invalid subscription URL: %1").arg(url));
        return;
    }
    if (m_inFlight.contains(url))
        return;

    m_inFlight.insert(url);
    setLoadingDelta(1);

    QNetworkRequest request(parsed);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setRawHeader("Accept", "*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));
    request.setTransferTimeout(kDownloadTimeoutMs);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, overrideName]() {
        handleDownloadFinished(reply, url, overrideName);
    });
}

void ProfileListModel::handleDownloadFinished(QNetworkReply *reply, const QString &url,
                                              const QString &overrideName)
{
    reply->deleteLater();
    m_inFlight.remove(url);
    setLoadingDelta(-1);

    if (reply->error() != QNetworkReply::NoError) {
        // In a VPN app the usual cause is a connected-but-broken tunnel blackholing the download.
        emit profileError(QStringLiteral("Can't reach the subscription server (%1). "
                                         "If a VPN is connected, disconnect it and try again.")
                              .arg(reply->errorString()));
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 0 && (status < 200 || status > 299)) {
        emit profileError(QStringLiteral("Server returned %1").arg(status));
        return;
    }

    const QString content = QString::fromUtf8(reply->readAll());
    if (content.trimmed().isEmpty()) {
        emit profileError(QStringLiteral("Subscription returned an empty response"));
        return;
    }

    QHash<QString, QString> headers;
    const auto rawPairs = reply->rawHeaderPairs();
    for (const auto &pair : rawPairs)
        headers.insert(QString::fromLatin1(pair.first).toLower(), QString::fromUtf8(pair.second));

    const QHash<QString, QString> contentHeaders = parseContentHeaders(content);

    QString name = overrideName.trimmed();
    if (name.isEmpty())
        name = decodeProfileTitle(headers.value(QStringLiteral("profile-title")));
    if (name.isEmpty())
        name = decodeProfileTitle(contentHeaders.value(QStringLiteral("profile-title")));
    if (name.isEmpty())
        name = parseContentDisposition(headers.value(QStringLiteral("content-disposition")));
    if (name.isEmpty())
        name = parseUrlFragment(url);
    if (name.isEmpty())
        name = parseUrlFilename(url);
    if (name.isEmpty())
        name = detectProtocolFromContent(content);
    if (name.isEmpty())
        name = QStringLiteral("Remote Profile");

    QString subInfoHeader = headers.value(QStringLiteral("subscription-userinfo"));
    if (subInfoHeader.isEmpty())
        subInfoHeader = contentHeaders.value(QStringLiteral("subscription-userinfo"));

    const SubscriptionInfo info =
        subInfoHeader.isEmpty() ? SubscriptionInfo{} : parseSubscriptionInfo(subInfoHeader);

    storeProfile(url, name, content, info);
}

void ProfileListModel::addDirectConfig(const QString &rawConfig, const QString &overrideName)
{
    const QString decoded = decodeSubscriptionBody(rawConfig);
    QStringList configLines;
    const QStringList lines = decoded.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (isProtocolLink(t))
            configLines.append(t);
    }

    // A pasted blob can carry the same metadata a subscription sends as HTTP headers, in
    // `#key: value` comment lines at the top.
    const QHash<QString, QString> contentHeaders = parseContentHeaders(decoded);
    const QString subInfoHeader = contentHeaders.value(QStringLiteral("subscription-userinfo"));
    const SubscriptionInfo info =
        subInfoHeader.isEmpty() ? SubscriptionInfo{} : parseSubscriptionInfo(subInfoHeader);

    QString name = overrideName.trimmed();
    if (name.isEmpty())
        name = decodeProfileTitle(contentHeaders.value(QStringLiteral("profile-title")));
    if (name.isEmpty()) {
        if (configLines.size() == 1) {
            name = parseConfigLineName(configLines.first());
            if (name.isEmpty())
                name = detectProtocolFromLine(configLines.first());
        } else if (configLines.size() > 1) {
            QStringList protocols;
            for (const QString &line : std::as_const(configLines)) {
                const QString protocol = detectProtocolFromLine(line);
                if (!protocol.isEmpty() && !protocols.contains(protocol))
                    protocols.append(protocol);
            }
            name = QStringLiteral("%1 configs").arg(configLines.size());
            if (!protocols.isEmpty())
                name += QStringLiteral(" (%1)").arg(protocols.join(QStringLiteral(", ")));
        }
        if (name.isEmpty())
            name = QStringLiteral("Imported Config");
    }

    storeProfile(QString(), name, decoded, info);
}

void ProfileListModel::addRawContent(const QString &rawInput, const QString &overrideName)
{
    const QString decoded = decodeSubscriptionBody(rawInput);
    if (decoded.trimmed().isEmpty()) {
        emit profileError(QStringLiteral("No valid profile content found"));
        return;
    }

    if (decoded.trimmed().startsWith(QLatin1Char('{'))) {
        QJsonParseError parseError{};
        QJsonDocument::fromJson(decoded.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit profileError(QStringLiteral("Invalid config JSON: %1").arg(parseError.errorString()));
            return;
        }
        QString name = overrideName.trimmed();
        if (name.isEmpty())
            name = QStringLiteral("Imported Config");
        storeProfile(QString(), name, decoded, SubscriptionInfo{});
        return;
    }

    int configCount = 0;
    const QStringList lines = decoded.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (isProtocolLink(line.trimmed()))
            ++configCount;
    }
    if (configCount == 0) {
        emit profileError(QStringLiteral("No valid profile content found — enter an HTTP(S) "
                                         "subscription URL or a config link"));
        return;
    }

    addDirectConfig(decoded, overrideName);
}

void ProfileListModel::storeProfile(const QString &url, const QString &name, const QString &content,
                                    const SubscriptionInfo &info)
{
    // A remote profile is keyed by its URL, so a refresh updates in place instead of duplicating.
    int existing = -1;
    if (!url.isEmpty()) {
        for (int i = 0; i < m_profiles.size(); ++i) {
            if (m_profiles[i].url == url) {
                existing = i;
                break;
            }
        }
    }

    if (existing >= 0) {
        ProfileData &p = m_profiles[existing];
        p.name = name;
        p.content = content;
        if (info.valid) {
            p.totalTrafficGB = info.totalGB;
            p.usedTrafficGB = info.usedGB;
            p.remainingDays = info.remainingDays;
        }
        p.lastUpdated = QDateTime::currentMSecsSinceEpoch();
        const bool wasActive = p.isActive;
        emit dataChanged(index(existing), index(existing));
        save();
        emit profileUpdated(name);
        if (wasActive)
            emit activeProfileChanged();
        return;
    }

    ProfileData p;
    p.id = QString::number(QDateTime::currentMSecsSinceEpoch());
    while (indexOfId(p.id) >= 0)
        p.id += QStringLiteral("0");
    p.name = name;
    p.url = url;
    p.content = content;
    p.totalTrafficGB = info.totalGB;
    p.usedTrafficGB = info.usedGB;
    p.remainingDays = info.remainingDays;
    p.lastUpdated = QDateTime::currentMSecsSinceEpoch();
    p.isActive = (activeProfileId().isEmpty());

    beginInsertRows(QModelIndex(), m_profiles.size(), m_profiles.size());
    m_profiles.append(p);
    endInsertRows();
    save();
    emit countChanged();
    emit profileAdded(name);
    if (p.isActive) {
        m_lastActiveId = p.id;
        emit activeProfileChanged();
    }
}

void ProfileListModel::setLoadingDelta(int delta)
{
    const bool was = isLoading();
    m_pendingDownloads = qMax(0, m_pendingDownloads + delta);
    if (was != isLoading())
        emit loadingChanged();
}

QString ProfileListModel::storagePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/profiles.json");
}

void ProfileListModel::load()
{
    QFile file(storagePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QByteArray raw = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    QJsonArray array;
    if (doc.isArray())
        array = doc.array();
    else if (doc.isObject())
        array = doc.object().value(QStringLiteral("profiles")).toArray();
    if (array.isEmpty())
        return;

    QVector<ProfileData> loaded;
    loaded.reserve(array.size());
    for (const QJsonValue &value : std::as_const(array)) {
        const QJsonObject obj = value.toObject();
        const QString id = obj.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        ProfileData p;
        p.id = id;
        p.name = obj.value(QStringLiteral("name")).toString();
        p.url = obj.value(QStringLiteral("url")).toString();
        p.content = obj.value(QStringLiteral("content")).toString();
        p.isActive = obj.value(QStringLiteral("isActive")).toBool();
        p.isPremium = obj.value(QStringLiteral("isPremium")).toBool();
        p.totalTrafficGB = obj.value(QStringLiteral("totalTrafficGB")).toDouble();
        p.usedTrafficGB = obj.value(QStringLiteral("usedTrafficGB")).toDouble();
        p.remainingDays = obj.value(QStringLiteral("remainingDays")).toInt(-1);
        p.lastUpdated = static_cast<qint64>(obj.value(QStringLiteral("lastUpdated")).toDouble());
        loaded.append(p);
    }

    beginResetModel();
    m_profiles = loaded;
    endResetModel();
    emit countChanged();
}

void ProfileListModel::save() const
{
    QJsonArray array;
    for (const auto &p : m_profiles) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), p.id);
        obj.insert(QStringLiteral("name"), p.name);
        obj.insert(QStringLiteral("url"), p.url);
        obj.insert(QStringLiteral("content"), p.content);
        obj.insert(QStringLiteral("isActive"), p.isActive);
        obj.insert(QStringLiteral("isPremium"), p.isPremium);
        obj.insert(QStringLiteral("totalTrafficGB"), p.totalTrafficGB);
        obj.insert(QStringLiteral("usedTrafficGB"), p.usedTrafficGB);
        obj.insert(QStringLiteral("remainingDays"), p.remainingDays);
        obj.insert(QStringLiteral("lastUpdated"), static_cast<double>(p.lastUpdated));
        array.append(obj);
    }
    QJsonObject root;
    root.insert(QStringLiteral("profiles"), array);

    // QSaveFile keeps the on-disk list intact if the write is interrupted.
    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}
