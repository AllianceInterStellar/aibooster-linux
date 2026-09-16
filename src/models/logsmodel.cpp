#include "logsmodel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTime>

namespace {
// Tail rather than load: box.log grows without bound while the core runs.
constexpr int kMaxLogEntries = 500;
constexpr qint64 kTailReadBytes = 256 * 1024;
constexpr int kPollIntervalMs = 2000;

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

    m_timer = new QTimer(this);
    m_timer->setInterval(kPollIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &LogsModel::poll);
    m_timer->start();
    poll();   // don't make the user wait a full interval for the first screenful
}

QString LogsModel::logFilePath()
{
    // VpnCore::setupAndStart passes AppDataLocation as the core's base dir, and the generated
    // sing-box config writes its log to the relative path data/box.log.
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/data/box.log";
}

void LogsModel::poll()
{
    if (m_isPaused)
        return;

    QFile f(logFilePath());
    if (!f.exists()) {
        // The core hasn't run yet (or its data dir was wiped on the next start).
        if (m_readPosition != 0 || !m_logs.isEmpty()) {
            m_readPosition = 0;
            m_partialLine.clear();
            clearLogs();
        }
        return;
    }

    const qint64 size = QFileInfo(f).size();
    if (size < m_readPosition) {
        // Rotated or truncated — start over rather than reading from a stale offset.
        m_readPosition = 0;
        m_partialLine.clear();
        clearLogs();
    }
    if (size == m_readPosition)
        return;

    if (!f.open(QIODevice::ReadOnly))
        return;

    // On the first poll of an already-large file, skip to the last chunk.
    qint64 from = m_readPosition;
    if (size - from > kTailReadBytes) {
        from = size - kTailReadBytes;
        m_partialLine.clear();   // we've almost certainly landed mid-line
    }
    f.seek(from);
    const QByteArray chunk = f.read(size - from);
    f.close();
    m_readPosition = size;

    QString text = m_partialLine + QString::fromUtf8(chunk);
    m_partialLine.clear();
    QStringList lines = text.split(QLatin1Char('\n'));
    // A chunk boundary can split a line; hold the tail until the rest arrives.
    if (!text.endsWith(QLatin1Char('\n')) && !lines.isEmpty())
        m_partialLine = lines.takeLast();

    appendLines(lines);
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
        emit isPausedChanged();
    }
}

void LogsModel::clearLogs()
{
    beginResetModel();
    m_logs.clear();
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
