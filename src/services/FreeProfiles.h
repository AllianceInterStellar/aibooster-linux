#ifndef FREEPROFILES_H
#define FREEPROFILES_H

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

/// Fetches the free nodes, the same way the mobile clients do:
///
///   GET primary then backup, UA "AiBooster/2.0", 15 s timeout.
///   The body is an INDEX ({"data":{"profiles":[{"sublink":…},…]}}), NOT a config — the first
///   profile's sublink holds the actual subscription body, which is fetched the same way.
class FreeProfiles : public QObject
{
    Q_OBJECT

public:
    explicit FreeProfiles(QObject *parent = nullptr);

    static const QString kPrimaryUrl;
    static const QString kBackupUrl;

    /// Index → first sublink → subscription body, trying each endpoint in turn. Feeding the
    /// index itself to the core produces no tunnel at all, so it is resolved here. A build
    /// whose endpoints were never configured reports that at once, without a request.
    void fetch(std::function<void(QString)> onSuccess, std::function<void(QString)> onError);

    /// Test hook: replace the primary/backup endpoints.
    void setEndpoints(const QStringList &urls);

    // ── Pure helpers, public for the headless tests ──
    /// data.profiles[0].sublink out of the index; empty when the body isn't that shape.
    static QString parseFirstFreeSublink(const QByteArray &indexBody);
    /// The endpoints worth requesting: placeholders left in by an unconfigured build (the
    /// reserved .invalid domain, which never resolves) and empty entries are dropped.
    static QStringList configuredEndpoints(const QStringList &urls);
    /// What the user is told when configuredEndpoints() leaves nothing to try.
    static QString unconfiguredMessage();

private:
    /// GET the first reachable url, handing the raw body to onBody.
    void tryGet(const QStringList &urls, int index, std::function<void(QByteArray)> onBody,
                std::function<void(QString)> onError);

    QNetworkAccessManager m_network;
    QStringList m_urls;
};

#endif
