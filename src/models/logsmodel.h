#ifndef LOGSMODEL_H
#define LOGSMODEL_H

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QSortFilterProxyModel>
#include <QTimer>

class LogFilterModel;

class LogsModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(bool isPaused READ isPaused WRITE setIsPaused NOTIFY isPausedChanged)
    /// The view-facing model: LogsScreen lists this and drives its text/level filters.
    Q_PROPERTY(LogFilterModel *filterModel READ filterModel CONSTANT)

public:
    enum LogLevel { All, Debug, Info, Warning, Error, Fatal };
    Q_ENUM(LogLevel)

    enum Roles {
        TimeRole = Qt::UserRole + 1,
        LevelRole,
        MessageRole,
        LevelNameRole
    };

    explicit LogsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool isPaused() const;
    void setIsPaused(bool paused);

    LogFilterModel *filterModel() const { return m_filterModel; }

    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void togglePause();

signals:
    void isPausedChanged();

private:
    struct LogEntry {
        QString time;
        LogLevel level;
        QString message;
    };

    /// Path the core writes to: base dir (AppDataLocation, per VpnCore::setupAndStart) + data/box.log.
    static QString logFilePath();
    void poll();
    void appendLines(const QStringList &lines);
    static LogEntry parseLine(const QString &line, const QString &fallbackTime);

    QVector<LogEntry> m_logs;
    LogFilterModel *m_filterModel = nullptr;
    bool m_isPaused = false;
    QTimer *m_timer = nullptr;
    qint64 m_readPosition = 0;   ///< byte offset in box.log already consumed
    QString m_partialLine;       ///< trailing bytes of an unterminated last line
};

class LogFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int selectedLevel READ selectedLevel WRITE setSelectedLevel NOTIFY selectedLevelChanged)

public:
    explicit LogFilterModel(QObject *parent = nullptr);

    QString filterText() const;
    void setFilterText(const QString &text);

    int selectedLevel() const;
    void setSelectedLevel(int level);

signals:
    void filterTextChanged();
    void selectedLevelChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_filterText;
    int m_selectedLevel = 0;
};

#endif
