// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Logs page shows the engine's own output. It used to tail a file the engine never
// wrote, so it was always empty; these tests pin the push path that replaced it.

#include "../src/models/logsmodel.h"

#include <QTest>

class TestLogsModel : public QObject
{
    Q_OBJECT

private slots:
    void engineLinesAppearWithTheirLevel();
    void pausingHoldsLinesUntilResumed();
    void theListIsCapped();
    void blankLinesAreDropped();

private:
    static QString message(const LogsModel &model, int row)
    {
        return model.data(model.index(row), LogsModel::MessageRole).toString();
    }
    static int level(const LogsModel &model, int row)
    {
        return model.data(model.index(row), LogsModel::LevelRole).toInt();
    }
};

void TestLogsModel::engineLinesAppearWithTheirLevel()
{
    LogsModel model;
    model.appendEngineLine(QStringLiteral("INFO[0000] inbound/mixed[mixed-in]: tcp server started"));
    model.appendEngineLine(QStringLiteral("ERROR outbound/vless[x]: dial tcp: i/o timeout"));
    model.appendEngineLine(QStringLiteral("Starting engine: /usr/libexec/aibooster/aibooster-core run"));

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(level(model, 0), int(LogsModel::Info));
    QCOMPARE(message(model, 0), QStringLiteral("inbound/mixed[mixed-in]: tcp server started"));
    QCOMPARE(level(model, 1), int(LogsModel::Error));
    // Lines without a level token are kept whole rather than guessed at.
    QVERIFY(message(model, 2).startsWith(QStringLiteral("Starting engine:")));
    // And the view-facing filter model sees them too.
    QCOMPARE(model.filterModel()->rowCount(), 3);
}

void TestLogsModel::pausingHoldsLinesUntilResumed()
{
    LogsModel model;
    model.appendEngineLine(QStringLiteral("INFO one"));
    model.setIsPaused(true);
    model.appendEngineLine(QStringLiteral("INFO two"));
    model.appendEngineLine(QStringLiteral("INFO three"));
    QCOMPARE(model.rowCount(), 1);

    model.setIsPaused(false);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(message(model, 2), QStringLiteral("three"));
}

void TestLogsModel::theListIsCapped()
{
    LogsModel model;
    for (int i = 0; i < 1200; ++i)
        model.appendEngineLine(QStringLiteral("INFO line %1").arg(i));
    QCOMPARE(model.rowCount(), 500);
    QCOMPARE(message(model, 499), QStringLiteral("line 1199"));
}

void TestLogsModel::blankLinesAreDropped()
{
    LogsModel model;
    model.appendEngineLine(QStringLiteral("   "));
    QCOMPARE(model.rowCount(), 0);
}

QTEST_MAIN(TestLogsModel)
#include "test_logsmodel.moc"
