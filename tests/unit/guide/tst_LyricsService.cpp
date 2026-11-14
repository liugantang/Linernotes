// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <core/Clock.h>
#include <guide/Lyrics.h>
#include <guide/LyricsService.h>
#include <guide/LyricsStore.h>
#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::core::Clock;
using linernotes::guide::LyricsResult;
using linernotes::guide::LyricsService;
using linernotes::guide::LyricsSource;
using linernotes::guide::LyricsStatus;
using linernotes::guide::LyricsStore;
using linernotes::library::Database;
using linernotes::library::Migrator;

class FixedClock final : public Clock {
public:
    explicit FixedClock(qint64 nowMs = 1000000000)
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

struct TestDbHelper {
    static qint64 insertTrack(const QSqlDatabase &db, const QString &audioPath)
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
        q.addBindValue(audioPath);
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

    static bool insertRawTag(const QSqlDatabase &db, qint64 trackId, const QString &tagType,
        int priority, const QString &key, int ordinal, const QString &value)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
            "VALUES (?, ?, ?, ?, ?, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(tagType);
        q.addBindValue(priority);
        q.addBindValue(key);
        q.addBindValue(ordinal);
        q.addBindValue(value);
        return q.exec();
    }
};

class TstLyricsService : public QObject {
    Q_OBJECT

private slots:
    void manualTakesPrecedenceOverEmbedded();
    void unusableEmbeddedFallsBackToNextSource();
    void recentNotFoundReturnsImmediatelyWithoutNetwork();
};

void TstLyricsService::manualTakesPrecedenceOverEmbedded()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("svc1.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());

    const QString audioPath = tempDir.filePath(QStringLiteral("song.flac"));
    const qint64 trackId = TestDbHelper::insertTrack(connRes.value(), audioPath);
    QVERIFY(trackId > 0);

    // Insert embedded lyrics (3 lines)
    const QString embeddedText
        = QStringLiteral("Embedded line 1\nEmbedded line 2\nEmbedded line 3");
    QVERIFY(TestDbHelper::insertRawTag(connRes.value(), trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("LYRICS"), 0, embeddedText));

    FixedClock clock(1000000);
    LyricsStore store(db, clock);
    const QString manualText = QStringLiteral("Manual line 1\nManual line 2");
    QVERIFY(store.saveManual(trackId, manualText).ok());

    QNetworkAccessManager nam;
    LyricsService service(db, nam, clock);

    QSignalSpy finishedSpy(&service, &LyricsService::finished);
    service.request(trackId);

    QCOMPARE(finishedSpy.count(), 1);
    const auto result = finishedSpy.at(0).at(0).value<LyricsResult>();
    QCOMPARE(result.trackId, trackId);
    QCOMPARE(result.status, LyricsStatus::Found);
    QCOMPARE(result.lyrics.source, LyricsSource::Manual);
    QCOMPARE(result.lyrics.text, manualText);
}

void TstLyricsService::unusableEmbeddedFallsBackToNextSource()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("svc2.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());

    const QString audioPath = tempDir.filePath(QStringLiteral("song.flac"));
    const QString lrcPath = tempDir.filePath(QStringLiteral("song.lrc"));

    {
        QFile audioFile(audioPath);
        QVERIFY(audioFile.open(QIODevice::WriteOnly));
        audioFile.write("dummy");
    }

    const QString sidecarText
        = QStringLiteral("[00:01.00] Sidecar 1\n[00:02.00] Sidecar 2\n[00:03.00] Sidecar 3");
    {
        QFile lrcFile(lrcPath);
        QVERIFY(lrcFile.open(QIODevice::WriteOnly));
        lrcFile.write(sidecarText.toUtf8());
    }

    const qint64 trackId = TestDbHelper::insertTrack(connRes.value(), audioPath);
    QVERIFY(trackId > 0);

    // Insert unusable embedded lyrics (< 3 non-empty lines)
    QVERIFY(TestDbHelper::insertRawTag(connRes.value(), trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("LYRICS"), 0, QStringLiteral("Kumagai Eri")));

    FixedClock clock(1000000);
    QNetworkAccessManager nam;
    LyricsService service(db, nam, clock);

    QSignalSpy finishedSpy(&service, &LyricsService::finished);
    service.request(trackId);

    QCOMPARE(finishedSpy.count(), 1);
    const auto result = finishedSpy.at(0).at(0).value<LyricsResult>();
    QCOMPARE(result.trackId, trackId);
    QCOMPARE(result.status, LyricsStatus::Found);
    QCOMPARE(result.lyrics.source, LyricsSource::LrcFile);
    QCOMPARE(result.lyrics.text, sidecarText);
    QCOMPARE(result.lyrics.synced, true);
}

void TstLyricsService::recentNotFoundReturnsImmediatelyWithoutNetwork()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("svc3.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());

    const QString audioPath = tempDir.filePath(QStringLiteral("nonexistent_song.flac"));
    const qint64 trackId = TestDbHelper::insertTrack(connRes.value(), audioPath);
    QVERIFY(trackId > 0);

    FixedClock clock(100000000);
    LyricsStore store(db, clock);

    // Save negative cache (NotFound) at now - 5 days (well within 30 days)
    clock.setNowMs(100000000 - (5LL * 24 * 60 * 60 * 1000));
    QVERIFY(store.saveNotFound(trackId).ok());

    clock.setNowMs(100000000);

    QNetworkAccessManager nam;
    QSignalSpy namFinishedSpy(&nam, &QNetworkAccessManager::finished);

    LyricsService service(db, nam, clock);
    QSignalSpy finishedSpy(&service, &LyricsService::finished);

    service.request(trackId);

    QCOMPARE(finishedSpy.count(), 1);
    const auto result = finishedSpy.at(0).at(0).value<LyricsResult>();
    QCOMPARE(result.trackId, trackId);
    QCOMPARE(result.status, LyricsStatus::NotFound);

    // Ensure no network requests were sent
    QCOMPARE(namFinishedSpy.count(), 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstLyricsService)

#include "tst_LyricsService.moc"
