#include "logsmodel.h"

#include <QRegularExpression>
#include <QTime>

namespace {
// The engine logs for as long as it runs; keep the most recent lines only.
constexpr int kMaxLogEntries = 500;

const char kLevelTokens[] = "TRACE|DEBUG|INFO|WARN|ERROR|FATAL|PANIC";

// `+0800 2026-08-06 10:23:01 INFO message` — when the core has log.timestamp on.
const QRegularExpression &timestampedLine() {
    static const QRegularExpression re(
        QString(R"(^[+-]\d{4} \d{4}-\d{2}-\d{2} (\d{2}:\d{2}:\d{2})(?:\.\d+)? (%1)\s+(.*)$)")
            .arg(kLevelTokens));
    return re;
}
// `INFO message` / `INFO[0012] message` — the shape our config actually produces.
const QRegularExpression &plainLine() {
    static const QRegularExpression re(
        QString(R"(^(%1)(?:\[\d+\])?\s+(.*)$)").arg(kLevelTokens));
    return re;
}
}  // namespace

LogsModel::LogsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // The screen lists filterModel, never this model directly, so it has to exist from the
    // start — a null one leaves the Logs page permanently empty.
    m_filterModel = new LogFilterModel(this);
    m_filterModel->setSourceModel(this);
}

void LogsModel::appendLine(const QString &line)
{
    // Paused means "stop scrolling so I can read", not "lose what happens meanwhile". Hold
    // the lines back and deliver them on resume, capped like the list itself.
    if (m_isPaused) {
        m_heldWhilePaused.append(line);
        if (m_heldWhilePaused.size() > kMaxLogEntries)
            m_heldWhilePaused.remove(0, m_heldWhilePaused.size() - kMaxLogEntries);
        return;
    }
    appendLines({line});
}

void LogsModel::appendLines(const QStringList &lines)
{
    const QString fallbackTime = QTime::currentTime().toString("HH:mm:ss");
    QVector<LogEntry> fresh;
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (!line.isEmpty())
            fresh.append(parseLine(line, fallbackTime));
    }
    if (fresh.isEmpty())
        return;

    const int overflow = qMax(0, m_logs.size() + fresh.size() - kMaxLogEntries);
    if (overflow > 0) {
        beginRemoveRows(QModelIndex(), 0, overflow - 1);
        m_logs.remove(0, overflow);
        endRemoveRows();
    }

    beginInsertRows(QModelIndex(), m_logs.size(), m_logs.size() + fresh.size() - 1);
    m_logs.append(fresh);
    endInsertRows();
}

LogsModel::LogEntry LogsModel::parseLine(const QString &line, const QString &fallbackTime)
{
    auto levelFor = [](const QString &token) {
        if (token == "TRACE" || token == "DEBUG") return Debug;
        if (token == "INFO") return Info;
        if (token == "WARN") return Warning;
        if (token == "ERROR") return Error;
        return Fatal;   // FATAL, PANIC
    };

    auto m = timestampedLine().match(line);
    if (m.hasMatch())
        return {m.captured(1), levelFor(m.captured(2)), m.captured(3)};

    m = plainLine().match(line);
    if (m.hasMatch())
        return {fallbackTime, levelFor(m.captured(1)), m.captured(2)};

    // Unrecognised shape (core banner, stack trace continuation) — keep it, don't guess a level.
    return {fallbackTime, Info, line};
}

int LogsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_logs.size();
}

QVariant LogsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_logs.size())
        return {};

    const auto &e = m_logs[index.row()];
    switch (role) {
    case TimeRole: return e.time;
    case LevelRole: return static_cast<int>(e.level);
    case MessageRole: return e.message;
    case LevelNameRole: {
        static const char *names[] = {"ALL", "DEBG", "INFO", "WARN", "ERRO", "FATL"};
        return QString(names[static_cast<int>(e.level)]);
    }
    }
    return {};
}

QHash<int, QByteArray> LogsModel::roleNames() const
{
    return {
        {TimeRole, "time"},
        {LevelRole, "level"},
        {MessageRole, "message"},
        {LevelNameRole, "levelName"}
    };
}

bool LogsModel::isPaused() const { return m_isPaused; }

void LogsModel::setIsPaused(bool paused)
{
    if (m_isPaused != paused) {
        m_isPaused = paused;
        if (!paused && !m_heldWhilePaused.isEmpty()) {
            const QStringList held = m_heldWhilePaused;
            m_heldWhilePaused.clear();
            appendLines(held);
        }
        emit isPausedChanged();
    }
}

void LogsModel::clearLogs()
{
    beginResetModel();
    m_logs.clear();
    m_heldWhilePaused.clear();
    endResetModel();
}

void LogsModel::togglePause()
{
    setIsPaused(!m_isPaused);
}

LogFilterModel::LogFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

QString LogFilterModel::filterText() const { return m_filterText; }

void LogFilterModel::setFilterText(const QString &text)
{
    if (m_filterText != text) {
        // begin/endFilterChange() arrived in Qt 6.7; current LTS distributions ship 6.4,
        // where invalidateFilter() is the equivalent (it re-runs the filter and emits the
        // model resets that keep views and persistent indexes coherent).
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        beginFilterChange();
        m_filterText = text;
        endFilterChange();
#else
        m_filterText = text;
        invalidateFilter();
#endif
        emit filterTextChanged();
    }
}

int LogFilterModel::selectedLevel() const { return m_selectedLevel; }

void LogFilterModel::setSelectedLevel(int level)
{
    if (m_selectedLevel != level) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        beginFilterChange();
        m_selectedLevel = level;
        endFilterChange();
#else
        m_selectedLevel = level;
        invalidateFilter();
#endif
        emit selectedLevelChanged();
    }
}

bool LogFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    auto idx = sourceModel()->index(sourceRow, 0, sourceParent);
    int level = sourceModel()->data(idx, LogsModel::LevelRole).toInt();
    QString message = sourceModel()->data(idx, LogsModel::MessageRole).toString();

    if (m_selectedLevel != 0 && level != m_selectedLevel)
        return false;

    if (!m_filterText.isEmpty() && !message.contains(m_filterText, Qt::CaseInsensitive))
        return false;

    return true;
}
