// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Errors.h>
#include <library/LibraryQuery.h>
#include <library/Migrator.h>
#include <library/PlayCountRule.h>
#include <library/SmartRule.h>
#include <library/TrackLanguage.h>

namespace {

using namespace linernotes::library;

struct DbHelper {
    static qint64 insertRoot(const QSqlDatabase &db)
    {
        QSqlQuery q(db);
        if (!q.exec(
                QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES ('/m', 100);"))) {
            return 0;
        }
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path,
        qint64 durationMs = 200000, qint64 firstSeenAt = 1000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                                 "first_seen_at, scanned_at) VALUES (?, ?, 1, 1, ?, ?, 1)"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(durationMs);
        q.addBindValue(firstSeenAt);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertAlbum(
        const QSqlDatabase &db, const QString &title, const QVariant &albumArtist = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("INSERT INTO albums (grouping_key, title, album_artist, created_at) "
                           "VALUES (?, ?, ?, 1)"));
        q.addBindValue(QString(title + albumArtist.toString()));
        q.addBindValue(title);
        q.addBindValue(albumArtist);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertArtist(const QSqlDatabase &db, const QString &name)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO artists (name, created_at) VALUES (?, 1)"));
        q.addBindValue(name);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertTrack(
        const QSqlDatabase &db, qint64 fileId, const QVariant &albumId = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO tracks (file_id, album_id, tags_read_at, created_at) "
                                 "VALUES (?, ?, 1, 1)"));
        q.addBindValue(fileId);
        q.addBindValue(albumId);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static void setMeta(const QSqlDatabase &db, qint64 trackId, const QString &title,
        const QString &artist, const QString &album)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "UPDATE effective_metadata SET title=?, artist=?, album=? WHERE track_id=?"));
        q.addBindValue(title);
        q.addBindValue(artist);
        q.addBindValue(album);
        q.addBindValue(trackId);
        q.exec();
    }

    static void addTrackArtist(const QSqlDatabase &db, qint64 trackId, qint64 artistId,
        const QString &role = QStringLiteral("artist"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO track_artists (track_id, artist_id, role, position) VALUES (?, ?, ?, 0)"));
        q.addBindValue(trackId);
        q.addBindValue(artistId);
        q.addBindValue(role);
        q.exec();
    }

    static void insertPlayEvent(const QSqlDatabase &db, qint64 trackId, qint64 startedAt,
        qint64 playedMs, qint64 trackDurationMs, int completed, int skipped)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO play_events (track_id, started_at, played_ms, track_duration_ms, "
            "completed, skipped) VALUES (?, ?, ?, ?, ?, ?)"));
        q.addBindValue(trackId);
        q.addBindValue(startedAt);
        q.addBindValue(playedMs);
        q.addBindValue(trackDurationMs);
        q.addBindValue(completed);
        q.addBindValue(skipped);
        q.exec();
    }

    static void setTrackPlayStats(const QSqlDatabase &db, qint64 trackId, int playCount,
        int skipCount, const QVariant &lastPlayedAt)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO track_play_stats (track_id, play_count, skip_count, "
            "last_played_at) VALUES (?, ?, ?, ?)"));
        q.addBindValue(trackId);
        q.addBindValue(playCount);
        q.addBindValue(skipCount);
        q.addBindValue(lastPlayedAt);
        q.exec();
    }

    static void addFavorite(const QSqlDatabase &db, const QString &entityType, qint64 entityId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO favorites (entity_type, entity_id, created_at) VALUES (?, ?, 1)"));
        q.addBindValue(entityType);
        q.addBindValue(entityId);
        q.exec();
    }

    static void setVersionType(const QSqlDatabase &db, qint64 trackId, const QString &versionType)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO track_versions (track_id, base_title, version_type, "
            "unresolved, updated_at) VALUES (?, 't', ?, 0, 1)"));
        q.addBindValue(trackId);
        q.addBindValue(versionType);
        q.exec();
    }
};

class TstSmartRule : public QObject {
    Q_OBJECT

private slots:
    void inferLanguage_data();
    void inferLanguage();
    void roundtrip();
    void oldFormatJson();
    void rejectInvalid_data();
    void rejectInvalid();
    void validateInvalid();
    void sqlPlayCountWithWindow();
    void sqlCompletedCountAndPlayCount();
    void sqlLastPlayed();
    void sqlVersionType();
    void sqlLanguage();
    void sqlAlbumFavoriteAndCompletion();
};

void TstSmartRule::inferLanguage_data()
{
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("album");
    QTest::addColumn<QString>("artist");
    QTest::addColumn<TrackLanguage>("expected");

    QTest::newRow("chinese_title") << QStringLiteral("晴天") << QString()
                                   << QStringLiteral("周杰伦") << TrackLanguage::Chinese;
    QTest::newRow("japanese_kana_title")
        << QStringLiteral("さくら") << QString() << QString() << TrackLanguage::Japanese;
    QTest::newRow("han_title_kana_album")
        << QStringLiteral("初恋") << QStringLiteral("宇多田ヒカル") << QStringLiteral("宇多田光")
        << TrackLanguage::Japanese;
    QTest::newRow("korean") << QStringLiteral("강남스타일") << QString() << QStringLiteral("싸이")
                            << TrackLanguage::Korean;
    QTest::newRow("western_title")
        << QStringLiteral("Hotel California") << QStringLiteral("Hotel California")
        << QStringLiteral("Eagles") << TrackLanguage::Western;
    QTest::newRow("western_title_han_artist") << QStringLiteral("Love Song") << QString()
                                              << QStringLiteral("方大同") << TrackLanguage::Western;
    QTest::newRow("empty_title_han_artist")
        << QString() << QString() << QStringLiteral("周杰伦") << TrackLanguage::Chinese;
    QTest::newRow("numeric_title") << QStringLiteral("1984") << QStringLiteral("1984")
                                   << QStringLiteral("1984") << TrackLanguage::Other;
}

void TstSmartRule::inferLanguage()
{
    QFETCH(QString, title);
    QFETCH(QString, album);
    QFETCH(QString, artist);
    QFETCH(TrackLanguage, expected);

    QCOMPARE(inferTrackLanguage(title, album, artist), expected);
}

void TstSmartRule::roundtrip()
{
    SmartRule rule;
    rule.match = SmartMatch::All;
    rule.playedFrom = QDate(2026, 1, 1);
    rule.playedTo = QDate(2026, 6, 30);
    rule.conditions = {
        SmartCondition {
            .field = SmartField::PlayCount,
            .op = SmartOp::Greater,
            .value = 2,
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::SkipCount,
            .op = SmartOp::Less,
            .value = 5,
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::CompletedCount,
            .op = SmartOp::Equals,
            .value = 1,
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::LastPlayed,
            .op = SmartOp::Between,
            .value = QStringLiteral("2026-03-01"),
            .value2 = QStringLiteral("2026-03-31"),
        },
        SmartCondition {
            .field = SmartField::VersionType,
            .op = SmartOp::Is,
            .value = QStringLiteral("live"),
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::Language,
            .op = SmartOp::Is,
            .value = QStringLiteral("ja"),
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::AlbumFavorite,
            .op = SmartOp::IsTrue,
            .value = { },
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::ArtistFavorite,
            .op = SmartOp::IsTrue,
            .value = { },
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::AlbumCompletion,
            .op = SmartOp::Less,
            .value = 100,
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::DateAdded,
            .op = SmartOp::Between,
            .value = QStringLiteral("2026-01-01"),
            .value2 = QStringLiteral("2026-12-31"),
        },
    };
    rule.sortKey = TrackSortKey::Year;
    rule.sortOrder = Qt::DescendingOrder;
    rule.limit = 50;

    const QString json = rule.toJson();
    const auto res = SmartRule::fromJson(json);

    QVERIFY(res.ok());
    QCOMPARE(res.value(), rule);
}

void TstSmartRule::oldFormatJson()
{
    const QString oldJson = QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "title", "op": "contains", "value": "Rock"}]})");
    const auto res = SmartRule::fromJson(oldJson);
    QVERIFY(res.ok());
    QCOMPARE(res.value().conditions.size(), 1);
    QCOMPARE(res.value().conditions.at(0).field, SmartField::Title);
    QCOMPARE(res.value().playedFrom, std::nullopt);
    QCOMPARE(res.value().playedTo, std::nullopt);
}

void TstSmartRule::rejectInvalid_data()
{
    QTest::addColumn<QString>("json");

    QTest::newRow("malformed_json") << QStringLiteral("not a json string");
    QTest::newRow("root_array") << QStringLiteral("[]");
    QTest::newRow("wrong_version")
        << QStringLiteral(R"({"version": 2, "match": "all", "conditions": []})");
    QTest::newRow("invalid_match")
        << QStringLiteral(R"({"version": 1, "match": "maybe", "conditions": []})");
    QTest::newRow("unknown_field") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "unknown", "op": "contains", "value": "x"}]})");
    QTest::newRow("unknown_op") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "title", "op": "magic", "value": "x"}]})");
    QTest::newRow("field_op_mismatch") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "title", "op": "between", "value": 1, "value2": 2}]})");
    QTest::newRow("missing_value") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "title", "op": "contains"}]})");
    QTest::newRow("missing_between_value2") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "year", "op": "between", "value": 2000}]})");
    QTest::newRow("limit_zero") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [], "limit": 0})");
    QTest::newRow("limit_negative")
        << QStringLiteral(R"({"version": 1, "match": "all", "conditions": [], "limit": -5})");
    QTest::newRow("unknown_sort_key") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [], "sortKey": "unknownKey"})");
    QTest::newRow("reject_playlist_order_sort_key") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [], "sortKey": "playlistOrder"})");
    QTest::newRow("invalid_sort_order") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [], "sortOrder": "sideways"})");
    QTest::newRow("unknown_version_type") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "versionType", "op": "is", "value": "invalid_ver"}]})");
    QTest::newRow("unknown_language") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "language", "op": "is", "value": "french"}]})");
    QTest::newRow("date_between_from_greater_than_to") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "dateAdded", "op": "between", "value": "2026-12-31", "value2": "2026-01-01"}]})");
    QTest::newRow("window_from_greater_than_to") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [], "playedFrom": "2026-12-31", "playedTo": "2026-01-01"})");
}

void TstSmartRule::validateInvalid()
{
    {
        SmartRule rule;
        rule.match = SmartMatch::All;
        SmartCondition cond;
        cond.field = SmartField::Title;
        cond.op = SmartOp::Between;
        cond.value = 1;
        cond.value2 = 2;
        rule.conditions.append(cond);
        auto res = rule.validate();
        QVERIFY(!res.ok());
    }
    {
        SmartRule rule;
        rule.playedFrom = QDate(2026, 12, 31);
        rule.playedTo = QDate(2026, 1, 1);
        auto res = rule.validate();
        QVERIFY(!res.ok());
    }
}

void TstSmartRule::rejectInvalid()
{
    QFETCH(QString, json);

    const auto res = SmartRule::fromJson(json);
    QVERIFY(!res.ok());
    QCOMPARE(res.error().code, QString(errc::kPlaylistRuleInvalid));
}

void TstSmartRule::sqlPlayCountWithWindow()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_sr_pc.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"), 200000);
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(
        qDb, t1, QStringLiteral("Song 1"), QStringLiteral("Art 1"), QStringLiteral("Alb 1"));

    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"), 200000);
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(
        qDb, t2, QStringLiteral("Song 2"), QStringLiteral("Art 2"), QStringLiteral("Alb 2"));

    const qint64 marchMs = QDateTime(QDate(2026, 3, 15), QTime(12, 0)).toMSecsSinceEpoch();
    const qint64 janMs = QDateTime(QDate(2026, 1, 15), QTime(12, 0)).toMSecsSinceEpoch();

    // t1: March has 3 counted events (150s for 200s track >= 50%) and 1 uncounted event (10s < 50%)
    DbHelper::insertPlayEvent(qDb, t1, marchMs + 1000, 150000, 200000, 0, 0);
    DbHelper::insertPlayEvent(qDb, t1, marchMs + 2000, 150000, 200000, 0, 0);
    DbHelper::insertPlayEvent(qDb, t1, marchMs + 3000, 150000, 200000, 0, 0);
    DbHelper::insertPlayEvent(qDb, t1, marchMs + 4000, 10000, 200000, 0, 0);
    DbHelper::setTrackPlayStats(qDb, t1, 3, 0, marchMs + 3000);

    // t2: January has 5 counted events, March has 0
    for (int i = 0; i < 5; ++i) {
        DbHelper::insertPlayEvent(
            qDb, t2, janMs + (static_cast<qint64>(i) * 1000), 150000, 200000, 0, 0);
    }
    DbHelper::setTrackPlayStats(qDb, t2, 5, 0, janMs + 4000);

    LibraryQuery query(qDb);

    // Without window: PlayCount > 2 returns t1 (3) and t2 (5)
    {
        SmartRule ruleNoWindow;
        ruleNoWindow.conditions = {
            SmartCondition {
                .field = SmartField::PlayCount,
                .op = SmartOp::Greater,
                .value = 2,
                .value2 = { },
            },
        };
        TrackFilter filter;
        filter.smartRule = ruleNoWindow;
        const auto rows
            = query.tracks(filter, TrackSortKey::Title, Qt::AscendingOrder, 0, 10).value();
        QCOMPARE(rows.size(), 2);
    }

    // With March window: PlayCount > 2 returns only t1 (3 in window vs t2 has 0)
    {
        SmartRule ruleWindow;
        ruleWindow.playedFrom = QDate(2026, 3, 1);
        ruleWindow.playedTo = QDate(2026, 3, 31);
        ruleWindow.conditions = {
            SmartCondition {
                .field = SmartField::PlayCount,
                .op = SmartOp::Greater,
                .value = 2,
                .value2 = { },
            },
        };
        TrackFilter filter;
        filter.smartRule = ruleWindow;
        const auto rows
            = query.tracks(filter, TrackSortKey::Title, Qt::AscendingOrder, 0, 10).value();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.at(0).trackId, t1);
    }

    // With March window: PlayCount > 3 returns 0 (since the 4th event is uncounted)
    {
        SmartRule ruleWindow;
        ruleWindow.playedFrom = QDate(2026, 3, 1);
        ruleWindow.playedTo = QDate(2026, 3, 31);
        ruleWindow.conditions = {
            SmartCondition {
                .field = SmartField::PlayCount,
                .op = SmartOp::Greater,
                .value = 3,
                .value2 = { },
            },
        };
        TrackFilter filter;
        filter.smartRule = ruleWindow;
        const auto rows
            = query.tracks(filter, TrackSortKey::Title, Qt::AscendingOrder, 0, 10).value();
        QCOMPARE(rows.size(), 0);
    }
}

void TstSmartRule::sqlCompletedCountAndPlayCount()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_sr_comp.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"), 200000);
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(qDb, t1, QStringLiteral("Played Not Completed"), QStringLiteral("Art"),
        QStringLiteral("Alb"));
    // t1: 2 play events with completed=0, played enough for PlayCount
    DbHelper::insertPlayEvent(qDb, t1, 1000, 150000, 200000, 0, 0);
    DbHelper::insertPlayEvent(qDb, t1, 2000, 150000, 200000, 0, 0);
    DbHelper::setTrackPlayStats(qDb, t1, 2, 0, 2000);

    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"), 200000);
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(
        qDb, t2, QStringLiteral("Completed"), QStringLiteral("Art"), QStringLiteral("Alb"));
    // t2: 1 play event with completed=1
    DbHelper::insertPlayEvent(qDb, t2, 3000, 200000, 200000, 1, 0);
    DbHelper::setTrackPlayStats(qDb, t2, 1, 0, 3000);

    LibraryQuery query(qDb);

    SmartRule rule;
    rule.match = SmartMatch::All;
    rule.conditions = {
        SmartCondition {
            .field = SmartField::PlayCount,
            .op = SmartOp::Greater,
            .value = 0,
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::CompletedCount,
            .op = SmartOp::Equals,
            .value = 0,
            .value2 = { },
        },
    };
    TrackFilter filter;
    filter.smartRule = rule;
    const auto rows = query.tracks(filter, TrackSortKey::Title, Qt::AscendingOrder, 0, 10).value();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).trackId, t1);
}

void TstSmartRule::sqlLastPlayed()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_sr_lp.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 r = DbHelper::insertRoot(qDb);

    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(
        qDb, t1, QStringLiteral("Recent"), QStringLiteral("Art"), QStringLiteral("Alb"));
    DbHelper::setTrackPlayStats(qDb, t1, 1, 0, nowMs - (1LL * 86400000LL)); // 1 day ago

    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(qDb, t2, QStringLiteral("Old"), QStringLiteral("Art"), QStringLiteral("Alb"));
    DbHelper::setTrackPlayStats(qDb, t2, 1, 0, nowMs - (60LL * 86400000LL)); // 60 days ago

    const qint64 f3 = DbHelper::insertFile(qDb, r, QStringLiteral("3.mp3"));
    const qint64 t3 = DbHelper::insertTrack(qDb, f3);
    DbHelper::setMeta(
        qDb, t3, QStringLiteral("Never Played"), QStringLiteral("Art"), QStringLiteral("Alb"));
    // t3 has no track_play_stats

    LibraryQuery query(qDb);

    // NotInLastDays 30: must match t2 (played 60 days ago), but NOT t3 (never played)
    {
        SmartRule rule;
        rule.conditions = {
            SmartCondition {
                .field = SmartField::LastPlayed,
                .op = SmartOp::NotInLastDays,
                .value = 30,
                .value2 = { },
            },
        };
        TrackFilter filter;
        filter.smartRule = rule;
        const auto rows
            = query.tracks(filter, TrackSortKey::Title, Qt::AscendingOrder, 0, 10).value();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.at(0).trackId, t2);
    }

    // Between: 65 days ago to 55 days ago -> matches t2
    {
        const QDate dFrom = QDateTime::fromMSecsSinceEpoch(nowMs - (65LL * 86400000LL)).date();
        const QDate dTo = QDateTime::fromMSecsSinceEpoch(nowMs - (55LL * 86400000LL)).date();
        SmartRule rule;
        rule.conditions = {
            SmartCondition {
                .field = SmartField::LastPlayed,
                .op = SmartOp::Between,
                .value = dFrom.toString(Qt::ISODate),
                .value2 = dTo.toString(Qt::ISODate),
            },
        };
        TrackFilter filter;
        filter.smartRule = rule;
        const auto rows
            = query.tracks(filter, TrackSortKey::Title, Qt::AscendingOrder, 0, 10).value();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.at(0).trackId, t2);
    }
}

void TstSmartRule::sqlVersionType()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_sr_vt.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(
        qDb, t1, QStringLiteral("Live Track"), QStringLiteral("Art"), QStringLiteral("Alb"));
    DbHelper::setVersionType(qDb, t1, QStringLiteral("live"));

    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(
        qDb, t2, QStringLiteral("Remix Track"), QStringLiteral("Art"), QStringLiteral("Alb"));
    DbHelper::setVersionType(qDb, t2, QStringLiteral("remix"));

    const qint64 f3 = DbHelper::insertFile(qDb, r, QStringLiteral("3.mp3"));
    const qint64 t3 = DbHelper::insertTrack(qDb, f3);
    DbHelper::setMeta(
        qDb, t3, QStringLiteral("Standard Track"), QStringLiteral("Art"), QStringLiteral("Alb"));
    // t3 has no track_versions row -> defaults to studio

    LibraryQuery query(qDb);

    // Is live -> matches t1
    {
        SmartRule rule;
        rule.conditions = {
            SmartCondition {
                .field = SmartField::VersionType,
                .op = SmartOp::Is,
                .value = QStringLiteral("live"),
                .value2 = { },
            },
        };
        TrackFilter filter;
        filter.smartRule = rule;
        const auto rows
            = query.tracks(filter, TrackSortKey::Title, Qt::AscendingOrder, 0, 10).value();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.at(0).trackId, t1);
    }

    // Is studio -> matches t3
    {
        SmartRule rule;
        rule.conditions = {
            SmartCondition {
                .field = SmartField::VersionType,
                .op = SmartOp::Is,
                .value = QStringLiteral("studio"),
                .value2 = { },
            },
        };
        TrackFilter filter;
        filter.smartRule = rule;
        const auto rows
            = query.tracks(filter, TrackSortKey::Title, Qt::AscendingOrder, 0, 10).value();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.at(0).trackId, t3);
    }
}

void TstSmartRule::sqlLanguage()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_sr_lang.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1);
    DbHelper::setMeta(qDb, t1, QStringLiteral("さくら"), QStringLiteral("森山直太朗"),
        QStringLiteral("アルバム"));

    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2);
    DbHelper::setMeta(
        qDb, t2, QStringLiteral("晴天"), QStringLiteral("周杰伦"), QStringLiteral("叶惠美"));

    LibraryQuery query(qDb);

    SmartRule rule;
    rule.conditions = {
        SmartCondition {
            .field = SmartField::Language,
            .op = SmartOp::Is,
            .value = QStringLiteral("ja"),
            .value2 = { },
        },
    };
    TrackFilter filter;
    filter.smartRule = rule;
    const auto rows = query.tracks(filter, TrackSortKey::Title, Qt::AscendingOrder, 0, 10).value();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).trackId, t1);
}

void TstSmartRule::sqlAlbumFavoriteAndCompletion()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_sr_af.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);

    // Album 1 (favorite, 2 tracks, 1 played -> 50% completion)
    const qint64 a1 = DbHelper::insertAlbum(qDb, QStringLiteral("Fav Album"));
    DbHelper::addFavorite(qDb, QStringLiteral("album"), a1);

    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1, a1);
    DbHelper::setMeta(
        qDb, t1, QStringLiteral("T1"), QStringLiteral("Art"), QStringLiteral("Fav Album"));
    DbHelper::setTrackPlayStats(qDb, t1, 1, 0, 1000);

    const qint64 f2 = DbHelper::insertFile(qDb, r, QStringLiteral("2.mp3"));
    const qint64 t2 = DbHelper::insertTrack(qDb, f2, a1);
    DbHelper::setMeta(
        qDb, t2, QStringLiteral("T2"), QStringLiteral("Art"), QStringLiteral("Fav Album"));
    // t2 has no play_stats

    // Album 2 (not favorite, 1 track, 1 played -> 100% completion)
    const qint64 a2 = DbHelper::insertAlbum(qDb, QStringLiteral("Other Album"));

    const qint64 f3 = DbHelper::insertFile(qDb, r, QStringLiteral("3.mp3"));
    const qint64 t3 = DbHelper::insertTrack(qDb, f3, a2);
    DbHelper::setMeta(
        qDb, t3, QStringLiteral("T3"), QStringLiteral("Art"), QStringLiteral("Other Album"));
    DbHelper::setTrackPlayStats(qDb, t3, 1, 0, 1000);

    LibraryQuery query(qDb);

    SmartRule rule;
    rule.match = SmartMatch::All;
    rule.conditions = {
        SmartCondition {
            .field = SmartField::AlbumFavorite,
            .op = SmartOp::IsTrue,
            .value = { },
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::AlbumCompletion,
            .op = SmartOp::Less,
            .value = 100,
            .value2 = { },
        },
    };
    TrackFilter filter;
    filter.smartRule = rule;
    const auto rows = query.tracks(filter, TrackSortKey::Title, Qt::AscendingOrder, 0, 10).value();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).trackId, t1);
    QCOMPARE(rows.at(1).trackId, t2);
}

} // namespace

QTEST_GUILESS_MAIN(TstSmartRule)
#include "tst_SmartRule.moc"
