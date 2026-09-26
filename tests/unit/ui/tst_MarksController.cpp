// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/LibraryEnums.h>
#include <library/Migrator.h>
#include <ui/MarksController.h>

namespace {

using linernotes::library::Database;
using linernotes::library::FavoriteKind;
using linernotes::library::Migrator;
using linernotes::ui::MarksController;

class TstMarksController : public QObject {
    Q_OBJECT

private slots:
    void testMarksControllerSignals();
    void testToggleFavorite();
};

void TstMarksController::testMarksControllerSignals()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const auto conn = db.connection().value();
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("INSERT INTO library_roots (id, path, enabled, added_at) "
                                  "VALUES (1, '/music', 1, 100);")));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, "
                                  "mtime, first_seen_at, scanned_at) VALUES "
                                  "(1, 1, '/music/1.mp3', 60000, 1, 1, 1, 1);")));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO tracks (id, file_id, tags_read_at, created_at) "
                                  "VALUES (1, 1, 1, 1);")));

    MarksController controller(db);
    QSignalSpy spyMarksChanged(&controller, &MarksController::marksChanged);

    // setFavorite emits marksChanged once
    QVERIFY(controller.setFavorite(FavoriteKind::Track, { 1 }, true));
    QCOMPARE(spyMarksChanged.count(), 1);
    QCOMPARE(controller.isFavorite(FavoriteKind::Track, 1), true);

    // setRating emits marksChanged once
    QVERIFY(controller.setRating({ 1 }, 4));
    QCOMPARE(spyMarksChanged.count(), 2);
    QCOMPARE(controller.rating(1), 4);
}

void TstMarksController::testToggleFavorite()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    MarksController controller(db);

    const bool initial = controller.isFavorite(FavoriteKind::Track, 100);
    QCOMPARE(initial, false);

    // Toggle once
    QVERIFY(controller.toggleFavorite(FavoriteKind::Track, 100));
    QCOMPARE(controller.isFavorite(FavoriteKind::Track, 100), !initial);

    // Toggle twice -> restores original state
    QVERIFY(controller.toggleFavorite(FavoriteKind::Track, 100));
    QCOMPARE(controller.isFavorite(FavoriteKind::Track, 100), initial);
}

} // namespace

QTEST_MAIN(TstMarksController)
#include "tst_MarksController.moc"
