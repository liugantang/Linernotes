// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QtConcurrent/QtConcurrent>

#include <common/TestSupport.h>
#include <library/CoverStore.h>
#include <library/Errors.h>

#include <array>
#include <vector>

namespace {

using linernotes::library::CoverStore;
namespace errc = linernotes::library::errc;

QByteArray makeTestJpeg(int width, int height, const QColor &color = Qt::blue)
{
    QImage img(width, height, QImage::Format_RGB32);
    img.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "JPG", 85);
    return bytes;
}

class TstCoverStore : public QObject {
    Q_OBJECT

private slots:
    void ingestGeneratesThreeSizes();
    void smallImageIsNotEnlarged();
    void repeatedIngestDoesNotRewriteFiles();
    void corruptDataReturnsCoverDecode();
    void thumbnailPathSelectionRules();
    void pruneDeletesOrphans();
    void concurrentIngestIsThreadSafeAndClean();
};

void TstCoverStore::ingestGeneratesThreeSizes()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    CoverStore store(tempDir.path());
    const QByteArray jpegData = makeTestJpeg(1600, 1600);

    const auto res = store.ingest(jpegData);
    QVERIFY(res.ok());

    const auto &info = res.value();
    QVERIFY(info.hash.startsWith(QStringLiteral("v1:")));
    QCOMPARE(info.mime, QStringLiteral("image/jpeg"));
    QCOMPARE(info.width, 1600);
    QCOMPARE(info.height, 1600);

    const QString p128 = store.thumbnailPath(info.hash, 128);
    const QString p512 = store.thumbnailPath(info.hash, 512);
    const QString p1024 = store.thumbnailPath(info.hash, 1024);

    QVERIFY(!p128.isEmpty() && QFile::exists(p128));
    QVERIFY(!p512.isEmpty() && QFile::exists(p512));
    QVERIFY(!p1024.isEmpty() && QFile::exists(p1024));

    QImage img128(p128);
    QCOMPARE(img128.size(), QSize(128, 128));

    QImage img512(p512);
    QCOMPARE(img512.size(), QSize(512, 512));

    QImage img1024(p1024);
    QCOMPARE(img1024.size(), QSize(1024, 1024));
}

void TstCoverStore::smallImageIsNotEnlarged()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    CoverStore store(tempDir.path());
    const QByteArray smallJpeg = makeTestJpeg(100, 50, Qt::red);

    const auto res = store.ingest(smallJpeg);
    QVERIFY(res.ok());

    const auto &info = res.value();
    QCOMPARE(info.width, 100);
    QCOMPARE(info.height, 50);

    for (const int sz : CoverStore::kSizes) {
        const QString p = store.thumbnailPath(info.hash, sz);
        QVERIFY2(!p.isEmpty() && QFile::exists(p),
            qPrintable(QStringLiteral("Thumbnail missing for size %1").arg(sz)));
        QImage img(p);
        QCOMPARE(img.size(), QSize(100, 50));
    }
}

void TstCoverStore::repeatedIngestDoesNotRewriteFiles()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    CoverStore store(tempDir.path());
    const QByteArray jpegData = makeTestJpeg(400, 400);

    const auto res1 = store.ingest(jpegData);
    QVERIFY(res1.ok());

    QMap<int, QDateTime> originalMtimes;
    for (const int sz : CoverStore::kSizes) {
        const QString p = store.thumbnailPath(res1.value().hash, sz);
        QVERIFY(QFile::exists(p));
        originalMtimes.insert(sz, QFileInfo(p).lastModified());
    }

    // Repeated ingest
    const auto res2 = store.ingest(jpegData);
    QVERIFY(res2.ok());
    QCOMPARE(res2.value().hash, res1.value().hash);

    for (const int sz : CoverStore::kSizes) {
        const QString p = store.thumbnailPath(res1.value().hash, sz);
        QCOMPARE(QFileInfo(p).lastModified(), originalMtimes.value(sz));
    }
}

void TstCoverStore::corruptDataReturnsCoverDecode()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    CoverStore store(tempDir.path());

    const auto resEmpty = store.ingest(QByteArray());
    QVERIFY(!resEmpty.ok());
    QCOMPARE(resEmpty.error().code, QString(errc::kCoverDecode));

    const auto resCorrupt = store.ingest(QByteArray("not a valid image format at all"));
    QVERIFY(!resCorrupt.ok());
    QCOMPARE(resCorrupt.error().code, QString(errc::kCoverDecode));
}

void TstCoverStore::thumbnailPathSelectionRules()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    CoverStore store(tempDir.path());
    const QByteArray jpegData = makeTestJpeg(1600, 1600);

    const auto res = store.ingest(jpegData);
    QVERIFY(res.ok());
    const QString hash = res.value().hash;

    QVERIFY(store.thumbnailPath(hash, 128).endsWith(QStringLiteral("_128.jpg")));
    QVERIFY(store.thumbnailPath(hash, 512).endsWith(QStringLiteral("_512.jpg")));
    QVERIFY(store.thumbnailPath(hash, 800).endsWith(QStringLiteral("_512.jpg")));
    QVERIFY(store.thumbnailPath(hash, 1024).endsWith(QStringLiteral("_1024.jpg")));
    QVERIFY(store.thumbnailPath(hash, 2000).endsWith(QStringLiteral("_1024.jpg")));

    QVERIFY(store.thumbnailPath(hash, 64).isEmpty());
    QVERIFY(store.thumbnailPath(QStringLiteral("invalid_hash"), 512).isEmpty());
    QVERIFY(store.thumbnailPath(QStringLiteral("v1:nonexistenthash1234567890"), 512).isEmpty());
}

void TstCoverStore::pruneDeletesOrphans()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    CoverStore store(tempDir.path());
    const QByteArray img1 = makeTestJpeg(300, 300, Qt::red);
    const QByteArray img2 = makeTestJpeg(300, 300, Qt::green);
    const QByteArray img3 = makeTestJpeg(300, 300, Qt::blue);

    const auto res1 = store.ingest(img1);
    const auto res2 = store.ingest(img2);
    const auto res3 = store.ingest(img3);

    QVERIFY(res1.ok() && res2.ok() && res3.ok());

    const QString hash1 = res1.value().hash;
    const QString hash2 = res2.value().hash;
    const QString hash3 = res3.value().hash;

    const QSet<QString> keep { hash1, hash3 };
    const int removedCount = store.prune(keep);
    QCOMPARE(removedCount, 1);

    // hash1 and hash3 remain
    QVERIFY(!store.thumbnailPath(hash1, 512).isEmpty());
    QVERIFY(!store.thumbnailPath(hash3, 512).isEmpty());

    // hash2 was removed
    QVERIFY(store.thumbnailPath(hash2, 512).isEmpty());

    // Second prune removes nothing
    QCOMPARE(store.prune(keep), 0);
}

void TstCoverStore::concurrentIngestIsThreadSafeAndClean()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    CoverStore store(tempDir.path());
    const QByteArray jpegData = makeTestJpeg(1600, 1600, Qt::cyan);

    constexpr int kNumThreads = 8;
    std::vector<QFuture<linernotes::core::Result<CoverStore::Info>>> futures;
    futures.reserve(kNumThreads);

    for (int i = 0; i < kNumThreads; ++i) {
        futures.push_back(
            QtConcurrent::run([&store, &jpegData]() { return store.ingest(jpegData); }));
    }

    QString expectedHash;
    for (auto &f : futures) {
        f.waitForFinished();
        const auto res = f.result();
        QVERIFY(res.ok());
        if (expectedHash.isEmpty()) {
            expectedHash = res.value().hash;
        } else {
            QCOMPARE(res.value().hash, expectedHash);
        }
    }

    // Verify all thumbnails are valid
    for (const int sz : CoverStore::kSizes) {
        const QString p = store.thumbnailPath(expectedHash, sz);
        QVERIFY(!p.isEmpty() && QFile::exists(p));
        QImage img(p);
        QVERIFY(!img.isNull());
    }

    // Verify no temporary files remain in the cache directory
    QDirIterator it(tempDir.path(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QString fileName = it.fileName();
        QVERIFY2(!fileName.startsWith(QStringLiteral(".tmp_")),
            qPrintable(QStringLiteral("Leftover temp file found: %1").arg(filePath)));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstCoverStore)

#include "tst_CoverStore.moc"
