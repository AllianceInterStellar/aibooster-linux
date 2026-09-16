#ifndef PROFILELISTMODEL_H
#define PROFILELISTMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QQmlEngine>
#include <QSet>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

struct ProfileData {
    QString id;
    QString name;
    QString url;
    /// Raw subscription payload (engine JSON, a base64 blob or a list of vmess:// style links).
    QString content;
    bool isActive = false;
    bool isPremium = false;
    double totalTrafficGB = 0;
    double usedTrafficGB = 0;
    /// -1 = unknown (the subscription carried no `expire`); ProfileTile hides the row for < 0.
    int remainingDays = -1;
    qint64 lastUpdated = 0;
};

/// Traffic/expiry numbers parsed out of a `subscription-userinfo` header.
struct SubscriptionInfo {
    double totalGB = 0;
    double usedGB = 0;
    int remainingDays = -1;
    bool valid = false;
};

class ProfileListModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY activeProfileChanged)
    Q_PROPERTY(QString activeProfileName READ activeProfileName NOTIFY activeProfileChanged)

public:

    /// Pulls the real subscription URL out of a client deep link.
    ///
    /// A deep link is recognised by SHAPE, not by a list of application names: any scheme
    /// that is not a direct protocol link and that carries a `url=` parameter. That covers
    /// every client whose links this used to enumerate, plus any that did not exist when
    /// this was written, and it keeps no vendor's name in the source.
    ///
    /// Returns an empty string when @p input is not a deep link at all. When it *is* one
    /// but carries no url, returns an empty string and sets @p malformed — the caller has
    /// to tell "not a deep link" from "a broken deep link" to report it usefully.
    /// @p nameOut receives the link's `name=` parameter when present.
    static QString subscriptionUrlFromDeepLink(const QString &input, QString *nameOut = nullptr,
                                               bool *malformed = nullptr);
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        UrlRole,
        IsActiveRole,
        IsPremiumRole,
        TotalTrafficRole,
        UsedTrafficRole,
        RemainingDaysRole,
        TrafficProgressRole,
        TotalTrafficGBRole,
        UsedTrafficGBRole,
        HasContentRole,
        LastUpdatedRole
    };

    explicit ProfileListModel(QObject *parent = nullptr);
    ~ProfileListModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Last-constructed instance — ProxyListModel needs it to follow the active profile.
    static ProfileListModel *instance() { return s_instance; }

    bool isLoading() const { return m_pendingDownloads > 0; }
    QString activeProfileId() const;
    QString activeProfileName() const;
    /// Raw content of the active profile (empty when there is none) — the proxy list parses this.
    QString activeProfileContent() const;

    // ── QML API ──
    Q_INVOKABLE void setActive(const QString &id);
    Q_INVOKABLE void setActive(int row);
    Q_INVOKABLE void deleteProfile(const QString &id);
    Q_INVOKABLE void deleteProfile(int row);
    /// Re-download every profile that has a remote URL.
    Q_INVOKABLE void refreshProfiles();
    /// Add from a subscription URL, a deep link, a direct config link or raw/base64 content.
    Q_INVOKABLE void addProfile(const QString &urlOrContent, const QString &overrideName = QString());
    /// Re-download one profile, keeping its current name.
    Q_INVOKABLE void updateProfile(const QString &id);
    Q_INVOKABLE void updateProfile(int row);
    Q_INVOKABLE void renameProfile(const QString &id, const QString &name);
    Q_INVOKABLE QString profileContent(const QString &id) const;

signals:
    void countChanged();
    void loadingChanged();
    /// Emitted when the active profile changes, or when its content is replaced by a download.
    void activeProfileChanged();
    void profileAdded(const QString &name);
    void profileUpdated(const QString &name);
    void profileError(const QString &message);

private:
    void handleDownloadFinished(QNetworkReply *reply, const QString &url, const QString &overrideName);
    void downloadProfile(const QString &url, const QString &overrideName);
    void addDirectConfig(const QString &rawConfig, const QString &overrideName);
    void addRawContent(const QString &rawInput, const QString &overrideName);
    void storeProfile(const QString &url, const QString &name, const QString &content,
                      const SubscriptionInfo &info);

    int indexOfId(const QString &id) const;
    void load();
    void save() const;
    QString storagePath() const;
    void setLoadingDelta(int delta);

    QVector<ProfileData> m_profiles;
    QNetworkAccessManager *m_network = nullptr;
    QSet<QString> m_inFlight;
    int m_pendingDownloads = 0;
    QString m_lastActiveId;

    static ProfileListModel *s_instance;
};

#endif
