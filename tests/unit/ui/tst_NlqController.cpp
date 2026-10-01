// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDate>
#include <QObject>
#include <QSqlQuery>
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
using linernotes::ui::NlqChipKind;
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

    q.rule.playedFrom = QDate(2025, 12, 1);
    q.rule.playedTo = QDate(2026, 2, 28);
    q.sortKey = nlq::SortKey::PlayCount;
    q.sortOrder = Qt::DescendingOrder;
    q.limit = 20;

    const auto chips = linernotes::ui::nlqChips(q);
    QCOMPARE(chips.size(), 7);

    // 1. Entity
    QCOMPARE(chips.at(0).kind, NlqChipKind::Entity);
    QCOMPARE(chips.at(0).text, QStringLiteral("Tracks"));

    // 2. Match (Match all conditions)
    QCOMPARE(chips.at(1).kind, NlqChipKind::Match);
    QCOMPARE(chips.at(1).text, QStringLiteral("Match: all conditions"));

    // 3. Condition 1 (Title Contains Rock)
    QCOMPARE(chips.at(2).kind, NlqChipKind::Condition);
    QCOMPARE(chips.at(2).index, 0);
    QCOMPARE(chips.at(2).text, QStringLiteral("Title Contains Rock"));

    // 4. Condition 2 (Favorite Is True - bool field must not show value)
    QCOMPARE(chips.at(3).kind, NlqChipKind::Condition);
    QCOMPARE(chips.at(3).index, 1);
    QCOMPARE(chips.at(3).text, QStringLiteral("Favorite Is True"));

    // 5. Play period
    QCOMPARE(chips.at(4).kind, NlqChipKind::PlayWindow);
    QCOMPARE(chips.at(4).text, QStringLiteral("Play period: 2025-12-01 \u2013 2026-02-28"));

    // 6. Sort & Limit
    QCOMPARE(chips.at(5).kind, NlqChipKind::Sort);
    QCOMPARE(chips.at(5).text, QStringLiteral("Sort: Play count \u2193"));
    QCOMPARE(chips.at(6).kind, NlqChipKind::Limit);
    QCOMPARE(chips.at(6).text, QStringLiteral("Limit: 20"));

    // Between condition test (single condition: must not include Match chip)
    nlq::Query qBetween;
    qBetween.entity = nlq::Entity::Album;
    library::SmartCondition cYear;
    cYear.field = library::SmartField::Year;
    cYear.op = library::SmartOp::Between;
    cYear.value = 2000;
    cYear.value2 = 2010;
    qBetween.rule.conditions.append(cYear);
    qBetween.limit = 50;

    const auto chipsBetween = linernotes::ui::nlqChips(qBetween);
    QCOMPARE(chipsBetween.size(), 4);
    QCOMPARE(chipsBetween.at(0).text, QStringLiteral("Albums"));
    QCOMPARE(chipsBetween.at(1).text, QStringLiteral("Year Between 2000 \u2013 2010"));
    QCOMPARE(chipsBetween.at(3).text, QStringLiteral("Limit: 50"));
    QVERIFY(std::ranges::none_of(chipsBetween,
        [](const linernotes::ui::NlqChip &c) { return c.kind == NlqChipKind::Match; }));

    // Enum conditions test (VersionType and Language, match All)
    nlq::Query qEnum;
    library::SmartCondition cVer;
    cVer.field = library::SmartField::VersionType;
    cVer.op = library::SmartOp::Is;
    cVer.value = QStringLiteral("studio");
    qEnum.rule.conditions.append(cVer);

    library::SmartCondition cLang;
    cLang.field = library::SmartField::Language;
    cLang.op = library::SmartOp::Is;
    cLang.value = QStringLiteral("ja");
    qEnum.rule.conditions.append(cLang);

    const auto chipsEnum = linernotes::ui::nlqChips(qEnum);
    QCOMPARE(chipsEnum.size(), 6);
    QCOMPARE(chipsEnum.at(1).kind, NlqChipKind::Match);
    QCOMPARE(chipsEnum.at(1).text, QStringLiteral("Match: all conditions"));
    QCOMPARE(chipsEnum.at(2).text, QStringLiteral("Version Is Studio"));
    QCOMPARE(chipsEnum.at(3).text, QStringLiteral("Language Is Japanese"));

    // Match Any with two conditions test
    nlq::Query qMatchAny;
    qMatchAny.rule.match = library::SmartMatch::Any;
    library::SmartCondition c1;
    c1.field = library::SmartField::Genre;
    c1.op = library::SmartOp::Contains;
    c1.value = QStringLiteral("Rock");
    qMatchAny.rule.conditions.append(c1);

    library::SmartCondition c2;
    c2.field = library::SmartField::Genre;
    c2.op = library::SmartOp::Contains;
    c2.value = QStringLiteral("Pop");
    qMatchAny.rule.conditions.append(c2);

    const auto chipsAny = linernotes::ui::nlqChips(qMatchAny);
    QCOMPARE(chipsAny.size(), 6);
    QCOMPARE(chipsAny.at(0).kind, NlqChipKind::Entity);
    QCOMPARE(chipsAny.at(1).kind, NlqChipKind::Match);
    QCOMPARE(chipsAny.at(1).text, QStringLiteral("Match: any condition"));
    QCOMPARE(chipsAny.at(2).kind, NlqChipKind::Condition);
    QCOMPARE(chipsAny.at(3).kind, NlqChipKind::Condition);

    // Match Any with single condition: must not contain Match chip
    nlq::Query qSingleAny;
    qSingleAny.rule.match = library::SmartMatch::Any;
    qSingleAny.rule.conditions.append(c1);
    const auto chipsSingleAny = linernotes::ui::nlqChips(qSingleAny);
    QCOMPARE(chipsSingleAny.size(), 4);
    QCOMPARE(chipsSingleAny.at(0).kind, NlqChipKind::Entity);
    QCOMPARE(chipsSingleAny.at(1).kind, NlqChipKind::Condition);
    QVERIFY(std::ranges::none_of(chipsSingleAny,
        [](const linernotes::ui::NlqChip &c) { return c.kind == NlqChipKind::Match; }));
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

    // 1. Set entity to Album through public API
    controller->setEntity(nlq::Entity::Album);
    QCOMPARE(controller->state(), NlqController::State::Ready);
    QCOMPARE(controller->hasConversation(), true);
    QCOMPARE(controller->rows().size(), 2);

    // 2. Set condition: Title contains "One"
    library::SmartCondition cond;
    cond.field = library::SmartField::Album;
    cond.op = library::SmartOp::Contains;
    cond.value = QStringLiteral("One");
    controller->setCondition(0, cond);

    QCOMPARE(controller->state(), NlqController::State::Ready);
    QCOMPARE(controller->rows().size(), 1);
    QCOMPARE(controller->conditionAt(0).field, library::SmartField::Album);

    // 3. saveAsPlaylist: Album result expands into its tracks (Track 1, Track 2)
    controller->saveAsPlaylist(QStringLiteral("My Album Playlist"));
    auto *playlists = ctx.playlists();
    QVERIFY(playlists != nullptr);
    QCOMPARE(playlists->model()->count(), 1);
    const qint64 plId = playlists->model()
                            ->data(playlists->model()->index(0, 0),
                                linernotes::ui::PlaylistListModel::PlaylistIdRole)
                            .toLongLong();
    QVERIFY(plId > 0);

    // 4. removeChip: remove condition and verify results update back to 2 albums
    controller->removeChip(0);
    QCOMPARE(controller->rows().size(), 2);

    // 5. setMatch: update match mode and verify
    controller->setMatch(library::SmartMatch::Any);
    QCOMPARE(controller->match(), library::SmartMatch::Any);
    controller->setMatch(library::SmartMatch::All);
    QCOMPARE(controller->match(), library::SmartMatch::All);

    // 6. newConversation: resets state and rows
    controller->newConversation();
    QCOMPARE(controller->state(), NlqController::State::Idle);
    QCOMPARE(controller->hasConversation(), false);
    QCOMPARE(controller->rows().size(), 0);
    QCOMPARE(controller->chips().size(), 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstNlqController)
#include "tst_NlqController.moc"
