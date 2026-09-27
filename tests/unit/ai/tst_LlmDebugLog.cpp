// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <ai/AiEnums.h>
#include <ai/LlmDebugLog.h>
#include <core/Settings.h>

using linernotes::ai::LlmDebugEntry;
using linernotes::ai::LlmDebugLog;
using linernotes::ai::Purpose;
using linernotes::core::Settings;

namespace {

class TstLlmDebugLog : public QObject {
    Q_OBJECT

private slots:
    void disabledDropsEntries();
    void enabledRecordsAndAssignsIncrementalIds();
    void max200EntriesDropsOldest();
    void disableClearsEntries();
};

void TstLlmDebugLog::disabledDropsEntries()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    LlmDebugLog log(settings);

    QCOMPARE(log.isEnabled(), false);

    QSignalSpy spyAdded(&log, &LlmDebugLog::entryAdded);

    LlmDebugEntry entry;
    entry.purpose = Purpose::Query;
    entry.model = QStringLiteral("gpt-4o");
    log.add(entry);

    QCOMPARE(spyAdded.count(), 0);
    QCOMPARE(log.entries().size(), 0);
    QVERIFY(!log.entry(1).has_value());
}

void TstLlmDebugLog::enabledRecordsAndAssignsIncrementalIds()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    LlmDebugLog log(settings);

    QSignalSpy spyEnabled(&log, &LlmDebugLog::enabledChanged);
    log.setEnabled(true);
    QCOMPARE(spyEnabled.count(), 1);
    QCOMPARE(log.isEnabled(), true);

    QSignalSpy spyAdded(&log, &LlmDebugLog::entryAdded);

    LlmDebugEntry e1;
    e1.model = QStringLiteral("model-1");
    log.add(e1);

    QCOMPARE(spyAdded.count(), 1);
    QCOMPARE(spyAdded.at(0).at(0).toULongLong(), 1ULL);

    LlmDebugEntry e2;
    e2.model = QStringLiteral("model-2");
    log.add(e2);

    QCOMPARE(spyAdded.count(), 2);
    QCOMPARE(spyAdded.at(1).at(0).toULongLong(), 2ULL);

    QCOMPARE(log.entries().size(), 2);
    QCOMPARE(log.entries().at(0).id, 1ULL);
    QCOMPARE(log.entries().at(0).model, QStringLiteral("model-1"));
    QCOMPARE(log.entries().at(1).id, 2ULL);
    QCOMPARE(log.entries().at(1).model, QStringLiteral("model-2"));

    const auto fetched1 = log.entry(1);
    QVERIFY(fetched1.has_value());
    if (fetched1.has_value()) {
        QCOMPARE(fetched1->model, QStringLiteral("model-1"));
    }

    const auto fetched2 = log.entry(2);
    QVERIFY(fetched2.has_value());
    if (fetched2.has_value()) {
        QCOMPARE(fetched2->model, QStringLiteral("model-2"));
    }
}

void TstLlmDebugLog::max200EntriesDropsOldest()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    LlmDebugLog log(settings);
    log.setEnabled(true);

    for (int i = 1; i <= 250; ++i) {
        LlmDebugEntry e;
        e.model = QStringLiteral("m_%1").arg(i);
        log.add(e);
    }

    QCOMPARE(log.entries().size(), 200);
    QCOMPARE(log.entries().first().id, 51ULL);
    QCOMPARE(log.entries().first().model, QStringLiteral("m_51"));
    QCOMPARE(log.entries().last().id, 250ULL);
    QCOMPARE(log.entries().last().model, QStringLiteral("m_250"));

    QVERIFY(!log.entry(50).has_value());
    QVERIFY(log.entry(51).has_value());
}

void TstLlmDebugLog::disableClearsEntries()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    LlmDebugLog log(settings);
    log.setEnabled(true);

    LlmDebugEntry e;
    e.model = QStringLiteral("test");
    log.add(e);
    QCOMPARE(log.entries().size(), 1);

    QSignalSpy spyCleared(&log, &LlmDebugLog::cleared);
    QSignalSpy spyEnabled(&log, &LlmDebugLog::enabledChanged);

    log.setEnabled(false);
    QCOMPARE(spyCleared.count(), 1);
    QCOMPARE(spyEnabled.count(), 1);
    QCOMPARE(log.isEnabled(), false);
    QCOMPARE(log.entries().size(), 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstLlmDebugLog)
#include "tst_LlmDebugLog.moc"
