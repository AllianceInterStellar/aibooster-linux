#ifndef CLASHAPI_H
#define CLASHAPI_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class QNetworkRequest;

/// Thin client for the clash API hiddify-core exposes for as long as the tunnel runs
/// (experimental.clash_api in the config it builds). It is the only way the app can see what
/// the core is actually doing — byte counters, and which node the "select" group points at.
///
/// Port and secret come from SettingsModel, which is also what writes them into the core's
/// options; the secret must be a fixed non-empty string there or the core makes up a random
/// one and every call here comes back 401.
class ClashApi : public QObject
{
    Q_OBJECT

public:
    explicit ClashApi(QObject *parent = nullptr);

    /// Cumulative counters for the current core session, in bytes.
    struct Traffic {
        qint64 uploadTotal = 0;
        qint64 downloadTotal = 0;
    };

    /// GET /connections.
    void fetchTraffic(std::function<void(Traffic)> onSuccess,
                      std::function<void(QString)> onError);

    /// GET /proxies/<group> — the tags the group can actually be pointed at ("all").
    void fetchGroupMembers(const QString &group,
                           std::function<void(QStringList)> onSuccess,
                           std::function<void(QString)> onError);

    /// PUT /proxies/<group> {"name": "<tag>"} — points a selector at one of its members.
    void selectOutbound(const QString &group, const QString &tag,
                        std::function<void()> onSuccess,
                        std::function<void(QString)> onError);

    /// Tag of the group the generated config routes through (config.OutboundSelectTag).
    static QString mainSelectorTag() { return QStringLiteral("select"); }

private:
    QNetworkRequest buildRequest(const QString &path) const;

    QNetworkAccessManager m_network;
};

#endif
