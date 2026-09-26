// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/LibraryEnums.h>
#include <library/Migrator.h>
#include <ui/TagEditorModel.h>

namespace {

using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::Migrator;
using linernotes::library::TagField;
using linernotes::ui::TagEditorModel;

struct TestDbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1048576, 2000, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, NULL, NULL, 0, 3000);"));
        q.addBindValue(fileId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertRawTag(
        const QSqlDatabase &db, qint64 trackId, const QString &key, const QString &value)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
            "VALUES (?, 'id3v2', 0, ?, 0, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(key);
        q.addBindValue(value);
        return q.exec();
    }

    static bool updateTagsReadAt(const QSqlDatabase &db, qint64 trackId, qint64 timestamp = 1000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE tracks SET tags_read_at = ? WHERE id = ?;"));
        q.addBindValue(timestamp);
        q.addBindValue(trackId);
        return q.exec();
    }
};

class TstTagEditorModel : public QObject {
    Q_OBJECT

private slots:
    void multiTrackLoadingAndBatchSave();
};

void TstTagEditorModel::multiTrackLoadingAndBatchSave()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test_tag_editor.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const auto conn = db.connection().value();
    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/f1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/f2.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);

    QVERIFY(
        TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Song 1")));
    QVERIFY(
        TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("Artist 1")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t1));

    QVERIFY(
        TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Song 2")));
    QVERIFY(
        TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("Artist 2")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t2));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());

    TagEditorModel model(db);
    QSignalSpy loadedSpy(&model, &TagEditorModel::loaded);
    QSignalSpy savedSpy(&model, &TagEditorModel::saved);

    QVERIFY(model.load({ t1, t2 }));
    QCOMPARE(loadedSpy.count(), 1);
    QCOMPARE(model.trackCount(), 2);

    // Title (row 0) should not be editable in multi-track mode
    const int titleRow = static_cast<int>(TagField::Title);
    QCOMPARE(model.data(model.index(titleRow, 0), TagEditorModel::EditableRole).toBool(), false);

    // Artist (row 1) should be editable and mixed == true
    const int artistRow = static_cast<int>(TagField::Artist);
    QCOMPARE(model.data(model.index(artistRow, 0), TagEditorModel::EditableRole).toBool(), true);
    QCOMPARE(model.data(model.index(artistRow, 0), TagEditorModel::MixedRole).toBool(), true);
    QCOMPARE(model.data(model.index(artistRow, 0), TagEditorModel::ValueRole).toString(),
        QStringLiteral(""));

    // Set Artist to "X"
    model.setValue(artistRow, QStringLiteral("X"));
    QCOMPARE(model.data(model.index(artistRow, 0), TagEditorModel::ValueRole).toString(),
        QStringLiteral("X"));
    QCOMPARE(model.data(model.index(artistRow, 0), TagEditorModel::EditedRole).toBool(), true);
    QCOMPARE(model.data(model.index(artistRow, 0), TagEditorModel::MixedRole).toBool(), false);

    // Save changes
    QVERIFY(model.save());
    QCOMPARE(savedSpy.count(), 1);

    // Verify both tracks have Artist = "X" in effective_metadata
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT artist FROM effective_metadata WHERE track_id IN (?, ?)"));
    q.addBindValue(t1);
    q.addBindValue(t2);
    QVERIFY(q.exec());
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("X"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("X"));
}

} // namespace

QTEST_MAIN(TstTagEditorModel)
#include "tst_TagEditorModel.moc"
