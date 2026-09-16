#ifndef PROXYLISTMODEL_H
#define PROXYLISTMODEL_H

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>
#include <QVector>

class ClashApi;
class ProxyFilterModel;

struct ProxyNode {
    QString id;
    QString name;
    QString type;
    QString address;
    quint16 port = 0;
    QString countryCode;
    /// -1 = not tested yet (ProxyTile renders "—"), 9999 = unreachable.
    int delay = -1;
    bool isPremium = false;
};

class ProxyListModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectedIdChanged)
    Q_PROPERTY(bool testing READ isTesting NOTIFY testingChanged)
    /// The view-facing model: ProxiesScreen lists this and drives its search box.
    Q_PROPERTY(ProxyFilterModel *filterModel READ filterModel CONSTANT)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TypeRole,
        AddressRole,
        CountryCodeRole,
        DelayRole,
        IsPremiumRole,
        PortRole
    };

    /// Sentinel delay for a node that failed or timed out the TCP probe.
    static constexpr int UnreachableDelay = 9999;

    explicit ProxyListModel(QObject *parent = nullptr);
    ~ProxyListModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString selectedId() const;
    void setSelectedId(const QString &id);
    bool isTesting() const { return m_testing; }
    ProxyFilterModel *filterModel() const { return m_filterModel; }

    /// Last-constructed instance — ConnectionModel re-applies the selection after a connect.
    static ProxyListModel *instance() { return s_instance; }

    // ── QML API ──
    Q_INVOKABLE void selectProxy(const QString &id);
    Q_INVOKABLE void selectProxy(int row);
    /// TCP-connect latency probe against every node, run concurrently.
    Q_INVOKABLE void testAllDelays();
    /// Re-parse the nodes of the currently active profile.
    Q_INVOKABLE void reload();
    Q_INVOKABLE QString selectedName() const;
    /// Point the running core's "select" group at the selected node. No-op (and no error) when
    /// nothing is connected — the selection is then applied by the next successful connect.
    void applySelectionToCore();

signals:
    void countChanged();
    void selectedIdChanged();
    void testingChanged();
    void delayTestFinished();
    /// The core refused the switch. Nothing else tells the user the tunnel is still on the
    /// previous node, so the screen has to show this.
    void selectionFailed(const QString &error);
    /// The core confirmed the switch — the tile that looks selected really is carrying traffic.
    void selectionApplied(const QString &name);

private:
    /// Maps a node name parsed out of the profile onto the tag the running core knows it by.
    /// Empty when the core has nothing matching.
    static QString resolveCoreTag(const QString &name, const QStringList &members);

    void bindToProfiles();
    void setNodes(const QVector<ProxyNode> &nodes);
    QString coreConfigContent() const;

    void pumpDelayTests();
    void startDelayTest(const QString &id, const QString &host, quint16 port);
    void applyDelay(const QString &id, int delay, quint64 generation);

    /// A queued probe. Nodes are addressed by id so a profile reload mid-test can't mis-assign a
    /// result to whatever node happens to sit at that row afterwards.
    struct PendingProbe {
        QString id;
        QString host;
        quint16 port = 0;
    };

    QVector<ProxyNode> m_proxies;
    QString m_selectedId;
    ProxyFilterModel *m_filterModel = nullptr;
    ClashApi *m_clashApi = nullptr;

    QVector<PendingProbe> m_pendingTests;
    int m_activeTests = 0;
    bool m_testing = false;
    /// Bumped on every reload. Node ids are positional, so a probe still in flight from the
    /// previous profile would otherwise write its delay onto whatever now sits at that id.
    quint64 m_generation = 0;

    static ProxyListModel *s_instance;
};

/// Free-text search over the node list. Matches name, type and address so a user can type
/// either "japan", "vless" or a hostname.
class ProxyFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit ProxyFilterModel(QObject *parent = nullptr);

    QString filterText() const { return m_filterText; }
    void setFilterText(const QString &text);

signals:
    void filterTextChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_filterText;
};

#endif
