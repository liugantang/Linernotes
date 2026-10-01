// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Migrator.h>
#include <library/SmartRule.h>
#include <nlq/NlqQuery.h>
#include <nlq/VariantPruning.h>

namespace {

using namespace linernotes;
using namespace linernotes::library;
using namespace linernotes::nlq;

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

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path)
    {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                           "first_seen_at, scanned_at) VALUES (?, ?, 1, 1, 200000, 1000, 1)"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.exec();
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertAlbum(const QSqlDatabase &db, const QString &title)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO albums (grouping_key, title, created_at) "
                                 "VALUES (?, ?, 1)"));
        q.addBindValue(title);
        q.addBindValue(title);
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
};

class TstVariantPruning : public QObject {
    Q_OBJECT

private slots:
    void prunesAbsentVariantsAndKeepsMatching();
    void leavesConditionUntouchedWhenAllVariantsAbsent();
    void leavesSingleStringAndNonTextConditionsUntouched();
};

void TstVariantPruning::prunesAbsentVariantsAndKeepsMatching()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_prune.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 a1 = DbHelper::insertAlbum(qDb, QStringLiteral("ラブライブ！Solo Live!"));
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1, a1);
    DbHelper::setMeta(qDb, t1, QStringLiteral("Snow halation"), QStringLiteral("Artist"),
        QStringLiteral("ラブライブ！Solo Live!"));

    Query q;
    q.rule.conditions = {
        SmartCondition {
            .field = SmartField::Keyword,
            .op = SmartOp::Contains,
            .value = QStringList { QStringLiteral("Love Live"), QStringLiteral("ラブライブ"),
                QStringLiteral("μ's") },
            .value2 = { },
        },
    };

    QList<PrunedVariants> report;
    const auto res = pruneTextVariants(db, q, &report);
    QVERIFY(res.ok());

    const auto &prunedQuery = res.value();
    QCOMPARE(prunedQuery.rule.conditions.size(), 1);
    QCOMPARE(prunedQuery.rule.conditions.at(0).value.toStringList(),
        QStringList { QStringLiteral("ラブライブ") });

    QCOMPARE(report.size(), 1);
    QCOMPARE(report.at(0).conditionIndex, 0);
    QCOMPARE(report.at(0).kept, QStringList { QStringLiteral("ラブライブ") });
    QCOMPARE(
        report.at(0).dropped, (QStringList { QStringLiteral("Love Live"), QStringLiteral("μ's") }));
}

void TstVariantPruning::leavesConditionUntouchedWhenAllVariantsAbsent()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_prune_absent.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto qDb = db.connection().value();

    const qint64 r = DbHelper::insertRoot(qDb);
    const qint64 a1 = DbHelper::insertAlbum(qDb, QStringLiteral("ラブライブ！Solo Live!"));
    const qint64 f1 = DbHelper::insertFile(qDb, r, QStringLiteral("1.mp3"));
    const qint64 t1 = DbHelper::insertTrack(qDb, f1, a1);
    DbHelper::setMeta(qDb, t1, QStringLiteral("Snow halation"), QStringLiteral("Artist"),
        QStringLiteral("ラブライブ！Solo Live!"));

    const QStringList absentList
        = { QStringLiteral("NonExistentA"), QStringLiteral("NonExistentB") };
    Query q;
    q.rule.conditions = {
        SmartCondition {
            .field = SmartField::Keyword,
            .op = SmartOp::Contains,
            .value = absentList,
            .value2 = { },
        },
    };

    QList<PrunedVariants> report;
    const auto res = pruneTextVariants(db, q, &report);
    QVERIFY(res.ok());

    const auto &prunedQuery = res.value();
    QCOMPARE(prunedQuery.rule.conditions.size(), 1);
    QCOMPARE(prunedQuery.rule.conditions.at(0).value.toStringList(), absentList);
    QVERIFY(report.isEmpty());
}

void TstVariantPruning::leavesSingleStringAndNonTextConditionsUntouched()
{
    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database db(dir.filePath(QStringLiteral("test_prune_single.db")));
    QVERIFY(db.open(Migrator()).ok());

    Query q;
    q.rule.conditions = {
        SmartCondition {
            .field = SmartField::Title,
            .op = SmartOp::Contains,
            .value = QStringLiteral("SingleString"),
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::Year,
            .op = SmartOp::Equals,
            .value = 2020,
            .value2 = { },
        },
    };

    QList<PrunedVariants> report;
    const auto res = pruneTextVariants(db, q, &report);
    QVERIFY(res.ok());

    const auto &prunedQuery = res.value();
    QCOMPARE(prunedQuery.rule.conditions.size(), 2);
    QCOMPARE(prunedQuery.rule.conditions.at(0).value.toString(), QStringLiteral("SingleString"));
    QCOMPARE(prunedQuery.rule.conditions.at(1).value.toInt(), 2020);
    QVERIFY(report.isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstVariantPruning)
#include "tst_VariantPruning.moc"
