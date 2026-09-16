#include "PremiumConfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <openssl/evp.h>

#include <chrono>
#include <memory>

// ── Deployment-specific endpoints ────────────────────────────────────────────────
// These identify ONE operator's subscription service; they are not part of the client's
// behaviour. Supply your own at configure time:
//
//   cmake -B build -DAIBOOSTER_PREMIUM_URL=… -DAIBOOSTER_PREMIUM_BACKUP_URL=… \
//         -DAIBOOSTER_FREE_URL=… -DAIBOOSTER_FREE_BACKUP_URL=…
//
// Left at their defaults the client builds and runs, and reports a clear error when a
// connection is attempted, rather than silently pointing at somebody else's servers.
const QString PremiumConfig::kPrimaryUrl = QStringLiteral(AIBOOSTER_PREMIUM_URL);
const QString PremiumConfig::kBackupUrl = QStringLiteral(AIBOOSTER_PREMIUM_BACKUP_URL);
const QString PremiumConfig::kFreeConfigUrl = QStringLiteral(AIBOOSTER_FREE_URL);
const QString PremiumConfig::kFreeBackupUrl = QStringLiteral(AIBOOSTER_FREE_BACKUP_URL);
const QString PremiumConfig::kProfileName = QStringLiteral("AiBooster Premium");

namespace {

const char *const kUserAgent = "AiBooster/2.0";
constexpr int kTimeoutMs = 15000;

/// AES-256 key (base64) for the subscription payload, injected at configure time with
/// -DAIBOOSTER_PREMIUM_AES_KEY_B64. It is deliberately NOT in this repository: it decrypts
/// one operator's paid subscription data, and publishing it would hand that data to
/// everyone. Builds without it decrypt nothing and say so.
const char *const kAesKeyB64 = AIBOOSTER_PREMIUM_AES_KEY_B64;

/// The placeholder the build ships with. Compared by value so an unconfigured build fails
/// with an explanation instead of an OpenSSL error about a malformed key.
const char *const kUnconfiguredAesKey = "REPLACE_WITH_YOUR_BASE64_AES_256_KEY";

constexpr int kIvLen = 12;
constexpr int kTagLen = 16;

} // namespace

PremiumConfig::PremiumConfig(QObject *parent)
    : QObject(parent)
    , m_urls({kPrimaryUrl, kBackupUrl})
{
}

void PremiumConfig::setEndpoints(const QStringList &urls)
{
    if (!urls.isEmpty())
        m_urls = urls;
}

void PremiumConfig::fetch(std::function<void(QString)> onSuccess,
                          std::function<void(QString)> onError)
{
    if (m_inFlight) {
        if (onError) onError(QStringLiteral("Premium config fetch already in progress"));
        return;
    }
    m_inFlight = true;
    tryFetch(0, std::move(onSuccess), std::move(onError));
}

void PremiumConfig::tryFetch(int index, std::function<void(QString)> onSuccess,
                             std::function<void(QString)> onError)
{
    if (index >= m_urls.size()) {
        m_inFlight = false;
        if (onError) onError(QStringLiteral("All premium config endpoints failed"));
        return;
    }

    QNetworkRequest request{QUrl{m_urls.at(index)}};
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));
    request.setTransferTimeout(std::chrono::milliseconds(kTimeoutMs));

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, index, onSuccess, onError]() {
        reply->deleteLater();

        // Any failure (network, HTTP status, bad payload, bad tag) falls through to the
        // backup endpoint, exactly like the mobile clients.
        auto failOver = [this, index, onSuccess, onError]() {
            tryFetch(index + 1, onSuccess, onError);
        };

        if (reply->error() != QNetworkReply::NoError) {
            failOver();
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 0 && (status < 200 || status > 299)) {
            failOver();
            return;
        }

        QString error;
        const QString decrypted = decryptPayload(reply->readAll(), &error);
        if (decrypted.isEmpty()) {
            failOver();
            return;
        }

        m_inFlight = false;
        if (onSuccess) onSuccess(sanitize(decrypted));
    });
}

void PremiumConfig::tryGet(const QStringList &urls, int index,
                           std::function<void(QByteArray)> onBody,
                           std::function<void(QString)> onError)
{
    if (index >= urls.size()) {
        if (onError) onError(QStringLiteral("All free config endpoints failed"));
        return;
    }

    QNetworkRequest request{QUrl{urls.at(index)}};
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));
    request.setTransferTimeout(std::chrono::milliseconds(kTimeoutMs));

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, urls, index, onBody, onError]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError
            || (status != 0 && (status < 200 || status > 299)) || body.isEmpty()) {
            tryGet(urls, index + 1, onBody, onError);
            return;
        }
        if (onBody) onBody(body);
    });
}

QString PremiumConfig::parseFirstFreeSublink(const QByteArray &indexBody)
{
    const QJsonDocument doc = QJsonDocument::fromJson(indexBody);
    if (!doc.isObject()) return {};
    const QJsonArray profiles = doc.object().value("data").toObject().value("profiles").toArray();
    if (profiles.isEmpty()) return {};
    return profiles.at(0).toObject().value("sublink").toString();
}

void PremiumConfig::fetchFree(std::function<void(QString)> onSuccess,
                              std::function<void(QString)> onError)
{
    tryGet({kFreeConfigUrl, kFreeBackupUrl}, 0,
           [this, onSuccess, onError](const QByteArray &indexBody) {
        const QString sublink = parseFirstFreeSublink(indexBody);
        if (sublink.isEmpty()) {
            // Not the index shape — assume the body already is a usable subscription.
            if (onSuccess) onSuccess(QString::fromUtf8(indexBody));
            return;
        }
        tryGet({sublink}, 0,
               [onSuccess](const QByteArray &body) {
                   if (onSuccess) onSuccess(QString::fromUtf8(body));
               },
               onError);
    }, onError);
}

QString PremiumConfig::decryptPayload(const QByteArray &responseBody, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return QString();
    };

    const QJsonObject obj = QJsonDocument::fromJson(responseBody).object();
    const QString dataB64 = obj.value(QStringLiteral("data")).toString();
    if (dataB64.isEmpty())
        return fail(QStringLiteral("Response carries no \"data\" field"));

    // Strict decode: reject anything that is not clean base64.
    const auto decoded = QByteArray::fromBase64Encoding(
        dataB64.toLatin1(), QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded)
        return fail(QStringLiteral("Payload is not valid base64"));

    const QByteArray plain = decryptWire(decoded.decoded, error);
    if (plain.isEmpty())
        return {};

    return QString::fromUtf8(plain);
}

QByteArray PremiumConfig::decryptWire(const QByteArray &wire, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return QByteArray();
    };

    // An unconfigured build must say what is wrong. Without this the placeholder decodes to
    // the wrong length and the user is told "Invalid AES key", which reads like corrupted
    // data rather than a build that was never given a key.
    if (qstrcmp(kAesKeyB64, kUnconfiguredAesKey) == 0) {
        return fail(QStringLiteral(
            "This build has no subscription key. It was compiled without "
            "-DAIBOOSTER_PREMIUM_AES_KEY_B64, so encrypted subscription payloads cannot be "
            "read. See README.md \u2192 Building."));
    }

    const QByteArray key = QByteArray::fromBase64(QByteArray(kAesKeyB64));
    if (key.size() != 32)
        return fail(QStringLiteral("Invalid AES key"));

    // Wire layout: [0..11]=IV, [12..27]=TAG, [28..]=ciphertext — tag BEFORE ciphertext.
    if (wire.size() <= kIvLen + kTagLen)
        return fail(QStringLiteral("Encrypted payload too short"));
    const QByteArray iv = wire.mid(0, kIvLen);
    const QByteArray tag = wire.mid(kIvLen, kTagLen);
    const QByteArray ct = wire.mid(kIvLen + kTagLen);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return fail(QStringLiteral("OpenSSL context allocation failed"));

    QByteArray plain;
    plain.resize(ct.size());
    int outLen = 0;
    int finalLen = 0;

    // Empty AAD, so there is no AAD EVP_DecryptUpdate call at all. The tag must be handed
    // over via ctrl BEFORE EVP_DecryptFinal_ex, which is what verifies it.
    const bool ok =
        EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvLen, nullptr) == 1
        && EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                              reinterpret_cast<const unsigned char *>(key.constData()),
                              reinterpret_cast<const unsigned char *>(iv.constData())) == 1
        && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(plain.data()), &outLen,
                             reinterpret_cast<const unsigned char *>(ct.constData()),
                             static_cast<int>(ct.size())) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
                               const_cast<char *>(tag.constData())) == 1
        && EVP_DecryptFinal_ex(ctx,
                               reinterpret_cast<unsigned char *>(plain.data()) + outLen,
                               &finalLen) == 1;
    EVP_CIPHER_CTX_free(ctx);

    if (!ok)
        return fail(QStringLiteral("AES-GCM decrypt failed (corrupt payload or tag mismatch)"));

    plain.truncate(outLen + finalLen);
    return plain;
}

QString PremiumConfig::sanitize(const QString &configJson)
{
    // The full Swift superset (ProfilesViewModel.sanitizePremiumConfig), S2–S6.
    // QJsonDocument keeps parsed integers as integers ("server_port":443 survives the
    // round trip as 443, never 443.0) — proven by the headless sanitize test.
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(configJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return configJson;

    QJsonObject root = doc.object();
    bool changed = false;

    // S2: strip the server-side marker; drop `experimental` entirely once empty.
    if (root.contains(QStringLiteral("experimental")) && root.value(QStringLiteral("experimental")).isObject()) {
        QJsonObject experimental = root.value(QStringLiteral("experimental")).toObject();
        experimental.remove(QStringLiteral("aibooster_premium"));
        if (experimental.isEmpty())
            root.remove(QStringLiteral("experimental"));
        else
            root.insert(QStringLiteral("experimental"), experimental);
        changed = true;
    }

    if (root.contains(QStringLiteral("outbounds")) && root.value(QStringLiteral("outbounds")).isArray()) {
        QJsonArray outbounds = root.value(QStringLiteral("outbounds")).toArray();

        // S3: sing-box has no "balancer" — rewrite to a selector.
        for (int i = 0; i < outbounds.size(); ++i) {
            if (!outbounds.at(i).isObject())
                continue;
            QJsonObject o = outbounds.at(i).toObject();
            if (o.value(QStringLiteral("type")).toString() != QLatin1String("balancer"))
                continue;
            o.insert(QStringLiteral("type"), QStringLiteral("selector"));
            o.remove(QStringLiteral("strategy"));
            const QJsonValue defaultValue = o.value(QStringLiteral("default"));
            if (defaultValue.isUndefined() || defaultValue.isNull()) {
                const QJsonArray inner = o.value(QStringLiteral("outbounds")).toArray();
                if (!inner.isEmpty())
                    o.insert(QStringLiteral("default"), inner.first());
            }
            outbounds.replace(i, o);
            changed = true;
        }

        // S4: the DNS/route rules below need these two tags to exist.
        bool hasDnsOut = false;
        bool hasDirect = false;
        for (const QJsonValue &v : std::as_const(outbounds)) {
            const QString tag = v.toObject().value(QStringLiteral("tag")).toString();
            if (tag == QLatin1String("dns-out")) hasDnsOut = true;
            if (tag == QLatin1String("direct")) hasDirect = true;
        }
        if (!hasDnsOut) {
            outbounds.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("dns")},
                                         {QStringLiteral("tag"), QStringLiteral("dns-out")}});
            changed = true;
        }
        if (!hasDirect) {
            outbounds.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("direct")},
                                         {QStringLiteral("tag"), QStringLiteral("direct")}});
            changed = true;
        }

        root.insert(QStringLiteral("outbounds"), outbounds);
    }

    // S5: give configs without a DNS block a working default.
    if (!root.contains(QStringLiteral("dns"))) {
        QJsonObject dns;
        dns.insert(QStringLiteral("servers"),
                   QJsonArray{QJsonObject{{QStringLiteral("tag"), QStringLiteral("remote")},
                                          {QStringLiteral("address"), QStringLiteral("https://1.1.1.1/dns-query")},
                                          {QStringLiteral("detour"), QStringLiteral("selector")}},
                              QJsonObject{{QStringLiteral("tag"), QStringLiteral("direct-dns")},
                                          {QStringLiteral("address"), QStringLiteral("local")},
                                          {QStringLiteral("detour"), QStringLiteral("direct")}}});
        dns.insert(QStringLiteral("rules"),
                   QJsonArray{QJsonObject{{QStringLiteral("outbound"), QJsonArray{QStringLiteral("direct")}},
                                          {QStringLiteral("server"), QStringLiteral("direct-dns")}}});
        root.insert(QStringLiteral("dns"), dns);
        changed = true;
    }

    // S6: an existing route section must at least hijack DNS; never create $.route though.
    if (root.contains(QStringLiteral("route")) && root.value(QStringLiteral("route")).isObject()) {
        QJsonObject route = root.value(QStringLiteral("route")).toObject();
        const QJsonValue rules = route.value(QStringLiteral("rules"));
        if (rules.isUndefined() || rules.isNull() || (rules.isArray() && rules.toArray().isEmpty())) {
            route.insert(QStringLiteral("rules"),
                         QJsonArray{QJsonObject{{QStringLiteral("protocol"), QStringLiteral("dns")},
                                                {QStringLiteral("outbound"), QStringLiteral("dns-out")}}});
            root.insert(QStringLiteral("route"), route);
            changed = true;
        }
    }

    if (!changed)
        return configJson;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}
