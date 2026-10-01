// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDate>
#include <QObject>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <core/Settings.h>
#include <library/Database.h>
#include <library/LibraryEnums.h>
#include <library/Migrator.h>
#include <library/SmartRule.h>
#include <nlq/NlqQuery.h>
#include <ui/AppContext.h>
#include <ui/NlqController.h>
#include <ui/PlaylistController.h>
#include <ui/SmartLabels.h>

#include <algorithm>

using linernotes::core::Settings;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::ui::AppContext;
using linernotes::ui::NlqController;
namespace library = linernotes::library;
namespace nlq = linernotes::nlq;

namespace {

class TstNlqController : public QObject {
    Q_OBJECT

private slots:
    void testChipsGeneration();
    void testNlqControllerDbAndActions();
};

void TstNlqController::testChipsGeneration()
{
    nlq::Query q;
    q.entity = nlq::Entity::Track;

    library::SmartCondition cTitle;
    cTitle.field = library::SmartField::Title;
    cTitle.op = library::SmartOp::Contains;
    cTitle.value = QStringLiteral("Rock");
    q.rule.conditions.append(cTitle);

    library::SmartCondition cFav;
    cFav.field = library::SmartField::Favorite;
    cFav.op = library::SmartOp::IsTrue;
    q.rule.conditions.append(cFav);

    library::SmartCondition cKeyword;
    cKeyword.field = library::SmartField::Keyword;
    cKeyword.op = library::SmartOp::Contains;
    cKeyword.value = QStringList { QStringLiteral("ラブライブ"), QStringLiteral("μ's"),
        QStringLiteral("Aqours") };
    q.rule.conditions.append(cKeyword);

    q.rule.playedFrom = QDate(2025, 12, 1);
    q.rule.playedTo = QDate(2026, 2, 28);
    q.sortKey = nlq::SortKey::PlayCount;
    q.sortOrder = Qt::DescendingOrder;
    q.limit = 20;

    const QStringList chips = linernotes::ui::nlqChips(q);
    QCOMPARE(chips.size(), 4);
    QCOMPARE(chips.at(0), QStringLiteral("Title Contains Rock"));
    QCOMPARE(chips.at(1), QStringLiteral("Favorite Is True"));
    QCOMPARE(chips.at(2), QStringLiteral("Keyword Contains ラブライブ / μ's / Aqours"));
    QCOMPARE(chips.at(3), QStringLiteral("Play period: 2025-12-01 \u2013 2026-02-28"));

    // Empty query (no conditions, no play period) -> empty list
    nlq::Query qEmpty;
    qEmpty.entity = nlq::Entity::Track;
    qEmpty.sortKey = nlq::SortKey::Default;
    qEmpty.limit = 50;

    const QStringList chipsEmpty = linernotes::ui::nlqChips(qEmpty);
    QVERIFY(chipsEmpty.isEmpty());
}

void TstNlqController::testNlqControllerDbAndActions()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test_nlq.db"));

    {
        Database db(dbPath);
        QVERIFY(db.open(Migrator()).ok());
        const auto conn = db.connection().value();
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("INSERT INTO library_roots (id, path, enabled, added_at) "
                                      "VALUES (1, '/music', 0, 100);")));
        QVERIFY(q.exec(
            QStringLiteral("INSERT INTO albums (id, grouping_key, title, album_artist, created_at) "
                           "VALUES (1, 'K1', 'Album One', 'Artist One', 100), "
                           "(2, 'K2', 'Album Two', 'Artist Two', 200);")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, "
                                      "mtime, first_seen_at, scanned_at) VALUES "
                                      "(1, 1, '/music/1.mp3', 60000, 1, 1, 1, 1), "
                                      "(2, 1, '/music/2.mp3', 120000, 1, 1, 2, 1), "
                                      "(3, 1, '/music/3.mp3', 180000, 1, 1, 3, 1);")));
        QVERIFY(q.exec(
            QStringLiteral("INSERT INTO tracks (id, file_id, album_id, tags_read_at, created_at) "
                           "VALUES (1, 1, 1, 1, 1), (2, 2, 1, 1, 1), (3, 3, 2, 1, 1);")));
        QVERIFY(q.exec(
            QStringLiteral("UPDATE effective_metadata SET title = 'Track 1', album = 'Album One', "
                           "artist = 'Artist One', disc_number = 1, track_number = 1 WHERE "
                           "track_id = 1;")));
        QVERIFY(q.exec(
            QStringLiteral("UPDATE effective_metadata SET title = 'Track 2', album = 'Album One', "
                           "artist = 'Artist One', disc_number = 1, track_number = 2 WHERE "
                           "track_id = 2;")));
        QVERIFY(q.exec(
            QStringLiteral("UPDATE effective_metadata SET title = 'Track 3', album = 'Album Two', "
                           "artist = 'Artist Two', disc_number = 1, track_number = 1 WHERE "
                           "track_id = 3;")));
    }

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));

    AppContext::Options options {
        .databasePath = dbPath,
        .coverCacheDir = tempDir.filePath(QStringLiteral("covers")),
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = { },
        .backupDir = { },
    };

    AppContext ctx(settings, options);
    QVERIFY(ctx.start().ok());

    auto *controller = ctx.nlq();
    QVERIFY(controller != nullptr);
    QCOMPARE(controller->state(), NlqController::State::Idle);
    QCOMPARE(controller->hasConversation(), false);

    // 1. Offline query for albums
    controller->submit(QStringLiteral("albums"));
    QCOMPARE(controller->state(), NlqController::State::Ready);
    QCOMPARE(controller->hasConversation(), true);
    QCOMPARE(controller->entity(), nlq::Entity::Album);
    QCOMPARE(controller->rows().size(), 2);
    QCOMPARE(controller->chips().size(), 0);

    // 2. saveAsPlaylist: Album result expands into its tracks (Track 1, Track 2, Track 3)
    controller->saveAsPlaylist(QStringLiteral("My Album Playlist"));
    auto *playlists = ctx.playlists();
    QVERIFY(playlists != nullptr);
    QCOMPARE(playlists->model()->count(), 1);
    const qint64 plId = playlists->model()
                            ->data(playlists->model()->index(0, 0),
                                linernotes::ui::PlaylistListModel::PlaylistIdRole)
                            .toLongLong();
    QVERIFY(plId > 0);

    // 3. newConversation: resets state and rows
    controller->newConversation();
    QCOMPARE(controller->state(), NlqController::State::Idle);
    QCOMPARE(controller->hasConversation(), false);
    QCOMPARE(controller->rows().size(), 0);
    QCOMPARE(controller->chips().size(), 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstNlqController)
#include "tst_NlqController.moc"
