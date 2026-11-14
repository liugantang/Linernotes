// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <core/Clock.h>
#include <guide/LyricsStore.h>
#include <library/Database.h>
#include <library/Migrator.h>

#include <optional>

namespace {

using linernotes::core::Clock;
using linernotes::guide::LyricsStore;
using linernotes::guide::StoredLyrics;
using linernotes::library::Database;
using linernotes::library::Migrator;

class FixedClock final : public Clock {
public:
    explicit FixedClock(qint64 nowMs = 1000000)
        : m_nowMs(nowMs)
    {
    }
    ~FixedClock() override = default;
    Q_DISABLE_COPY_MOVE(FixedClock)

    qint64 nowMs() const override { return m_nowMs; }
    void setNowMs(qint64 ms) { m_nowMs = ms; }

private:
    qint64 m_nowMs = 0;
};

// 读取失败与“没有记录”都返回 nullopt；测试中失败会体现为后续断言不成立
std::optional<StoredLyrics> getStored(const LyricsStore &store, qint64 trackId)
{
    auto res = store.get(trackId);
    return res.ok() ? res.value() : std::nullopt;
}

struct TestDbHelper {
    static qint64 insertTrack(const QSqlDatabase &db)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(QStringLiteral("/music"));
        q.addBindValue(1000);
        if (!q.exec()) {
            return -1;
        }
        const qint64 rootId = q.lastInsertId().toLongLong();

        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1024, 2000, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(QStringLiteral("/music/test.flac"));
        if (!q.exec()) {
            return -1;
        }
        const qint64 fileId = q.lastInsertId().toLongLong();

        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, tags_read_at, created_at) VALUES (?, 2000, 2000);"));
        q.addBindValue(fileId);
        if (!q.exec()) {
            return -1;
        }
        return q.lastInsertId().toLongLong();
    }
};

class TstLyricsStore : public QObject {
    Q_OBJECT

private slots:
    void manualNotOverwrittenByOnlineOrNotFound();
    void removeOnlinePreservesManual();
    void saveManualEmptyTextDeletesRow();
    void savesAndGetsLrclibAndNotFound();
};

void TstLyricsStore::manualNotOverwrittenByOnlineOrNotFound()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("lyrics1.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const qint64 trackId = TestDbHelper::insertTrack(connRes.value());
    QVERIFY(trackId > 0);

    FixedClock clock(5000);
    LyricsStore store(db, clock);

    // Save manual lyrics
    QVERIFY(store.saveManual(trackId, QStringLiteral("Manual lyrics line 1\nLine 2")).ok());
    auto storedOpt = getStored(store, trackId);
    QVERIFY(storedOpt.has_value());
    if (!storedOpt.has_value()) {
        return;
    }
    QCOMPARE(storedOpt->kind, StoredLyrics::Kind::Manual);
    QCOMPARE(storedOpt->text, QStringLiteral("Manual lyrics line 1\nLine 2"));

    // saveLrclib should NOT overwrite manual lyrics
    clock.setNowMs(6000);
    QVERIFY(store.saveLrclib(trackId, QStringLiteral("Lrclib text"), true, false, 999).ok());
    storedOpt = getStored(store, trackId);
    QVERIFY(storedOpt.has_value());
    if (!storedOpt.has_value()) {
        return;
    }
    QCOMPARE(storedOpt->kind, StoredLyrics::Kind::Manual);
    QCOMPARE(storedOpt->text, QStringLiteral("Manual lyrics line 1\nLine 2"));

    // saveNotFound should NOT overwrite manual lyrics
    clock.setNowMs(7000);
    QVERIFY(store.saveNotFound(trackId).ok());
    storedOpt = getStored(store, trackId);
    QVERIFY(storedOpt.has_value());
    if (!storedOpt.has_value()) {
        return;
    }
    QCOMPARE(storedOpt->kind, StoredLyrics::Kind::Manual);
    QCOMPARE(storedOpt->text, QStringLiteral("Manual lyrics line 1\nLine 2"));
}

void TstLyricsStore::removeOnlinePreservesManual()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("lyrics2.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const qint64 trackId = TestDbHelper::insertTrack(connRes.value());
    QVERIFY(trackId > 0);

    FixedClock clock(5000);
    LyricsStore store(db, clock);

    // 1. For manual: removeOnline leaves it intact
    QVERIFY(store.saveManual(trackId, QStringLiteral("Manual text")).ok());
    QVERIFY(store.removeOnline(trackId).ok());
    auto storedOpt = getStored(store, trackId);
    QVERIFY(storedOpt.has_value());
    if (!storedOpt.has_value()) {
        return;
    }
    QCOMPARE(storedOpt->kind, StoredLyrics::Kind::Manual);

    // 2. Overwrite with empty manual to clear, then save lrclib
    QVERIFY(store.saveManual(trackId, QString()).ok());
    storedOpt = getStored(store, trackId);
    QVERIFY(!storedOpt.has_value());

    QVERIFY(store.saveLrclib(trackId, QStringLiteral("Lrclib text"), false, false, 123).ok());
    storedOpt = getStored(store, trackId);
    QVERIFY(storedOpt.has_value());
    if (!storedOpt.has_value()) {
        return;
    }
    QCOMPARE(storedOpt->kind, StoredLyrics::Kind::Lrclib);

    // removeOnline removes lrclib row
    QVERIFY(store.removeOnline(trackId).ok());
    storedOpt = getStored(store, trackId);
    QVERIFY(!storedOpt.has_value());
}

void TstLyricsStore::saveManualEmptyTextDeletesRow()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("lyrics3.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const qint64 trackId = TestDbHelper::insertTrack(connRes.value());
    QVERIFY(trackId > 0);

    FixedClock clock(5000);
    LyricsStore store(db, clock);

    QVERIFY(store.saveManual(trackId, QStringLiteral("Line 1\nLine 2")).ok());
    auto storedOpt = getStored(store, trackId);
    QVERIFY(storedOpt.has_value());

    QVERIFY(store.saveManual(trackId, QString()).ok());
    storedOpt = getStored(store, trackId);
    QVERIFY(!storedOpt.has_value());
}

void TstLyricsStore::savesAndGetsLrclibAndNotFound()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("lyrics4.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const qint64 trackId = TestDbHelper::insertTrack(connRes.value());
    QVERIFY(trackId > 0);

    FixedClock clock(123456);
    LyricsStore store(db, clock);

    // Save lrclib
    QVERIFY(store.saveLrclib(trackId, QStringLiteral("[00:01.00] Synced"), true, false, 888).ok());
    auto storedOpt = getStored(store, trackId);
    QVERIFY(storedOpt.has_value());
    if (!storedOpt.has_value()) {
        return;
    }
    const auto &stored = storedOpt.value();
    QCOMPARE(stored.kind, StoredLyrics::Kind::Lrclib);
    QCOMPARE(stored.text, QStringLiteral("[00:01.00] Synced"));
    QCOMPARE(stored.synced, true);
    QCOMPARE(stored.instrumental, false);
    QVERIFY(stored.lrclibId.has_value());
    if (!stored.lrclibId.has_value()) {
        return;
    }
    QCOMPARE(stored.lrclibId.value(), 888);
    QCOMPARE(stored.fetchedAtMs, 123456);

    // Save not found
    clock.setNowMs(654321);
    QVERIFY(store.saveNotFound(trackId).ok());
    storedOpt = getStored(store, trackId);
    QVERIFY(storedOpt.has_value());
    if (!storedOpt.has_value()) {
        return;
    }
    const auto &storedNf = storedOpt.value();
    QCOMPARE(storedNf.kind, StoredLyrics::Kind::NotFound);
    QVERIFY(storedNf.text.isEmpty());
    QCOMPARE(storedNf.fetchedAtMs, 654321);
}

} // namespace

QTEST_GUILESS_MAIN(TstLyricsStore)

#include "tst_LyricsStore.moc"
