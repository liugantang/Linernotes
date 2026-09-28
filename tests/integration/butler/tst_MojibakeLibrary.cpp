// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <butler/MojibakeAnalysis.h>
#include <butler/MojibakeLlm.h>
#include <butler/MojibakeSource.h>
#include <common/ManualClock.h>
#include <common/TestSupport.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/LibraryEnums.h>
#include <library/LibraryRoots.h>
#include <library/Migrator.h>
#include <library/Scanner.h>

namespace {

using linernotes::butler::analyzeGroup;
using linernotes::butler::fallbackProposals;
using linernotes::butler::MojibakeSource;
using linernotes::library::CorrectionKind;
using linernotes::library::CorrectionStatus;
using linernotes::library::CorrectionStore;
using linernotes::library::Database;
using linernotes::library::LibraryRoots;
using linernotes::library::Migrator;
using linernotes::library::Scanner;
using linernotes::test::fixturePath;
using linernotes::test::ManualClock;

void copyDirContents(const QString &srcDir, const QString &dstDir)
{
    QDirIterator it(srcDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString srcFile = it.next();
        const QString relPath = QDir(srcDir).relativeFilePath(srcFile);
        const QString dstFile = QDir(dstDir).filePath(relPath);
        QFileInfo(dstFile).dir().mkpath(QStringLiteral("."));
        QFile::copy(srcFile, dstFile);
    }
}

struct TrackMetadata {
    QString title;
    QString artist;
    QString album;
};

QMap<QString, TrackMetadata> queryEffectiveMetadata(Database &db)
{
    QMap<QString, TrackMetadata> result;
    const auto connRes = db.connection();
    if (!connRes.ok()) {
        return result;
    }
    QSqlQuery q(connRes.value());
    if (q.exec(QStringLiteral("SELECT f.path, em.title, em.artist, em.album FROM files f "
                              "JOIN tracks t ON t.file_id = f.id "
                              "JOIN effective_metadata em ON em.track_id = t.id"))) {
        while (q.next()) {
            const QString path = q.value(0).toString();
            const QString filename = QFileInfo(path).fileName();
            result.insert(filename,
                TrackMetadata {
                    .title = q.value(1).toString(),
                    .artist = q.value(2).toString(),
                    .album = q.value(3).toString(),
                });
        }
    }
    return result;
}

class TstMojibakeLibrary : public QObject {
    Q_OBJECT

private slots:
    void repairsFixtureLibrary();
    void rerunSkipsPendingFields();
};

void TstMojibakeLibrary::repairsFixtureLibrary()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    copyDirContents(fixturePath(QStringLiteral("library")), musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    QVERIFY(db.open(Migrator()).ok());
    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    Scanner scanner(db, Scanner::Options { });
    const auto scanRes = scanner.scanBlocking();
    QVERIFY(scanRes.ok());

    const ManualClock clock(1000000);
    const MojibakeSource source(db);

    const auto groupsRes = source.findGroups();
    QVERIFY(groupsRes.ok());
    const auto &groupKeys = groupsRes.value();

    // 应该包含乱码素材所在的组
    QVERIFY(!groupKeys.isEmpty());

    CorrectionStore store(db, clock);

    for (const QString &key : groupKeys) {
        const auto grpRes = source.loadGroup(key);
        QVERIFY(grpRes.ok());
        const auto &group = grpRes.value();

        const auto analysis = analyzeGroup(group);

        const auto batchRes
            = store.createBatch(CorrectionKind::Mojibake, QStringLiteral("Auto repair mojibake"));
        QVERIFY(batchRes.ok());
        const qint64 batchId = batchRes.value();

        if (!analysis.proposals.isEmpty()) {
            QVERIFY(store.addProposals(batchId, analysis.proposals).ok());
        }

        if (!analysis.ambiguous.isEmpty()) {
            const auto fb = fallbackProposals(analysis.ambiguous);
            if (!fb.isEmpty()) {
                QVERIFY(store.addProposals(batchId, fb).ok());
            }
        }

        const auto correctionsRes = store.corrections(batchId);
        QVERIFY(correctionsRes.ok());
        QList<qint64> pendingIds;
        for (const auto &row : correctionsRes.value()) {
            if (row.status == CorrectionStatus::Pending) {
                pendingIds.append(row.id);
            }
        }

        if (!pendingIds.isEmpty()) {
            QVERIFY(store.accept(pendingIds).ok());
        }
    }

    const auto metaMap = queryEffectiveMetadata(db);

    // 1. mp3_id3v23_gbk.mp3
    QVERIFY(metaMap.contains(QStringLiteral("mp3_id3v23_gbk.mp3")));
    const auto &gbk = metaMap.value(QStringLiteral("mp3_id3v23_gbk.mp3"));
    QCOMPARE(gbk.title, QStringLiteral("晚风里的歌"));
    QCOMPARE(gbk.artist, QStringLiteral("林晓风"));
    QCOMPARE(gbk.album, QStringLiteral("山谷的回响"));

    // 2. mp3_id3v23_shiftjis.mp3
    QVERIFY(metaMap.contains(QStringLiteral("mp3_id3v23_shiftjis.mp3")));
    const auto &sjis = metaMap.value(QStringLiteral("mp3_id3v23_shiftjis.mp3"));
    QCOMPARE(sjis.title, QStringLiteral("雨の日の散歩"));
    QCOMPARE(sjis.artist, QStringLiteral("佐藤風花"));
    QCOMPARE(sjis.album, QStringLiteral("静かな夜"));

    // 3. mp3_id3v23_euckr.mp3
    QVERIFY(metaMap.contains(QStringLiteral("mp3_id3v23_euckr.mp3")));
    const auto &euckr = metaMap.value(QStringLiteral("mp3_id3v23_euckr.mp3"));
    QCOMPARE(euckr.title, QStringLiteral("새벽의 노래"));
    QCOMPARE(euckr.artist, QStringLiteral("김바람"));
    QCOMPARE(euckr.album, QStringLiteral("도시의 꿈"));

    // 4. mp3_id3v1_gbk.mp3
    QVERIFY(metaMap.contains(QStringLiteral("mp3_id3v1_gbk.mp3")));
    const auto &v1Gbk = metaMap.value(QStringLiteral("mp3_id3v1_gbk.mp3"));
    QCOMPARE(v1Gbk.title, QStringLiteral("晚风里的歌"));
    QCOMPARE(v1Gbk.artist, QStringLiteral("林晓风"));
    QCOMPARE(v1Gbk.album, QStringLiteral("山谷的回响"));

    // 5. mp3_id3v1_big5.mp3
    QVERIFY(metaMap.contains(QStringLiteral("mp3_id3v1_big5.mp3")));
    const auto &v1Big5 = metaMap.value(QStringLiteral("mp3_id3v1_big5.mp3"));
    QCOMPARE(v1Big5.title, QStringLiteral("晚風裡的歌"));
    QCOMPARE(v1Big5.artist, QStringLiteral("林曉風"));
    QCOMPARE(v1Big5.album, QStringLiteral("山谷的迴響"));
}

void TstMojibakeLibrary::rerunSkipsPendingFields()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    copyDirContents(fixturePath(QStringLiteral("library")), musicDir);

    Database db(tempDir.filePath(QStringLiteral("library.db")));
    QVERIFY(db.open(Migrator()).ok());
    LibraryRoots roots(db);
    QVERIFY(roots.add(musicDir).ok());

    Scanner scanner(db, Scanner::Options { });
    const auto scanRes = scanner.scanBlocking();
    QVERIFY(scanRes.ok());

    const ManualClock clock(1000000);
    const MojibakeSource source(db);

    const auto groupsRes1 = source.findGroups();
    QVERIFY(groupsRes1.ok());
    const auto &groupKeys = groupsRes1.value();
    QVERIFY(!groupKeys.isEmpty());

    CorrectionStore store(db, clock);

    // 写入提议但不接受（保持 pending）
    for (const QString &key : groupKeys) {
        const auto grpRes = source.loadGroup(key);
        QVERIFY(grpRes.ok());
        const auto analysis = analyzeGroup(grpRes.value());

        const auto batchRes
            = store.createBatch(CorrectionKind::Mojibake, QStringLiteral("Pending batch"));
        QVERIFY(batchRes.ok());
        const qint64 batchId = batchRes.value();

        if (!analysis.proposals.isEmpty()) {
            QVERIFY(store.addProposals(batchId, analysis.proposals).ok());
        }
        if (!analysis.ambiguous.isEmpty()) {
            const auto fb = fallbackProposals(analysis.ambiguous);
            if (!fb.isEmpty()) {
                QVERIFY(store.addProposals(batchId, fb).ok());
            }
        }
    }

    // 再次 findGroups，所有 pending 字段应被跳过，不再返回这些组
    const auto groupsRes2 = source.findGroups();
    QVERIFY(groupsRes2.ok());
    QCOMPARE(groupsRes2.value().size(), 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstMojibakeLibrary)

#include "tst_MojibakeLibrary.moc"
