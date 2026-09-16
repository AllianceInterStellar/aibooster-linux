#ifndef PREMIUMCONFIG_H
#define PREMIUMCONFIG_H

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

/// Fetches and decrypts the "AiBooster Premium" engine config, byte-for-byte compatible
/// with the three mobile clients:
///
///   GET primary (Lambda URL) then backup (CloudFront), UA "AiBooster/2.0", 15 s timeout.
///   Body {"data":"<base64>"} → base64 → [IV(12) | TAG(16) | ciphertext] → AES-256-GCM
///   (empty AAD) → UTF-8 engine JSON → sanitize() (the full Swift superset, S2–S6).
class PremiumConfig : public QObject
{
    Q_OBJECT

public:
    explicit PremiumConfig(QObject *parent = nullptr);

    static const QString kPrimaryUrl;
    static const QString kBackupUrl;
    /// Free-tier profile index — used when not logged in or not premium (see fetchFree).
    static const QString kFreeConfigUrl;
    static const QString kFreeBackupUrl;
    /// Display name of the premium profile, same across all clients.
    static const QString kProfileName;

    /// Fetch → decrypt → sanitize, trying primary then backup. One fetch at a time;
    /// a call while one is in flight reports an error.
    void fetch(std::function<void(QString)> onSuccess, std::function<void(QString)> onError);

    /// Free-tier config, no decryption. `?type=free` answers an INDEX
    /// ({"data":{"profiles":[{"sublink":…},…]}}), NOT a config — the first profile's sublink
    /// holds the actual subscription body. Feeding the index to the core produces no tunnel
    /// at all, so it must be resolved here exactly as the mobile clients and WinUI do.
    void fetchFree(std::function<void(QString)> onSuccess, std::function<void(QString)> onError);

    /// data.profiles[0].sublink out of the free index; empty when the body isn't that shape.
    static QString parseFirstFreeSublink(const QByteArray &indexBody);

    /// Test hook: replace the primary/backup endpoints.
    void setEndpoints(const QStringList &urls);

    // ── Pure helpers, public for the headless tests ──
    /// Full pipeline for one HTTP body: {"data":base64} → decrypt → UTF-8 string (no sanitize).
    static QString decryptPayload(const QByteArray &responseBody, QString *error = nullptr);
    /// AES-256-GCM open of the raw wire bytes [IV(12)|TAG(16)|CT] with the shared key.
    static QByteArray decryptWire(const QByteArray &wire, QString *error = nullptr);
    /// S2–S6 sanitization. Returns the input unchanged when it is not a JSON object.
    static QString sanitize(const QString &configJson);

private:
    void tryFetch(int index, std::function<void(QString)> onSuccess,
                  std::function<void(QString)> onError);
    /// GET the first reachable url, handing the raw body to onBody (no decryption).
    void tryGet(const QStringList &urls, int index, std::function<void(QByteArray)> onBody,
                std::function<void(QString)> onError);

    QNetworkAccessManager m_network;
    QStringList m_urls;
    bool m_inFlight = false;
};

#endif
