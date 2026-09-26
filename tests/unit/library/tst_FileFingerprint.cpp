// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QByteArray>
#include <QByteArrayView>
#include <QFile>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include <library/Errors.h>
#include <library/FileFingerprint.h>

namespace {

using linernotes::library::FileFingerprint;
namespace errc = linernotes::library::errc;

class TstFileFingerprint : public QObject {
    Q_OBJECT

private slots:
    void sameContentProducesSameFingerprint();
    void modificationAtHeadTailOrAppendChangesFingerprint();
    void largeFileModificationInMiddleYieldsSameFingerprint();
    void smallFileModificationChangesFingerprintAndEmptyFileComputable();
    void nonexistentPathReturnsFileReadError();
};

void TstFileFingerprint::sameContentProducesSameFingerprint()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString path1 = tempDir.filePath(QStringLiteral("file1.bin"));
    const QString path2 = tempDir.filePath(QStringLiteral("file2.bin"));

    const QByteArray content(static_cast<qsizetype>(50) * 1024, 'X');
    {
        QFile f1(path1);
        QVERIFY(f1.open(QIODevice::WriteOnly));
        f1.write(content);
    }
    {
        QFile f2(path2);
        QVERIFY(f2.open(QIODevice::WriteOnly));
        f2.write(content);
    }

    const auto res1 = FileFingerprint::compute(path1);
    QVERIFY(res1.ok());
    const auto res2 = FileFingerprint::compute(path2);
    QVERIFY(res2.ok());

    QCOMPARE(res1.value(), res2.value());

    // Format: "v1:" + 40 lowercase hex chars
    const auto &fp = res1.value();
    QVERIFY(fp.startsWith(QStringLiteral("v1:")));
    QCOMPARE(fp.length(), 3 + 40);
    const QRegularExpression hexPattern(QStringLiteral("^v1:[0-9a-f]{40}$"));
    QVERIFY(hexPattern.match(fp).hasMatch());
}

void TstFileFingerprint::modificationAtHeadTailOrAppendChangesFingerprint()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const qsizetype size = static_cast<qsizetype>(200) * 1024; // 200 KiB (> 128 KiB)
    const QByteArray baseData(size, 'A');

    const QString basePath = tempDir.filePath(QStringLiteral("base.bin"));
    {
        QFile f(basePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(baseData);
    }
    const auto baseRes = FileFingerprint::compute(basePath);
    QVERIFY(baseRes.ok());

    // 1. Modify 1 byte at head (offset 0)
    const QString headPath = tempDir.filePath(QStringLiteral("head.bin"));
    {
        QByteArray headData = baseData;
        headData.replace(0, 1, QByteArrayView("B", 1));
        QFile f(headPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(headData);
    }
    const auto headRes = FileFingerprint::compute(headPath);
    QVERIFY(headRes.ok());
    QVERIFY(headRes.value() != baseRes.value());

    // 2. Modify 1 byte at tail (offset size - 1)
    const QString tailPath = tempDir.filePath(QStringLiteral("tail.bin"));
    {
        QByteArray tailData = baseData;
        tailData.replace(size - 1, 1, QByteArrayView("B", 1));
        QFile f(tailPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(tailData);
    }
    const auto tailRes = FileFingerprint::compute(tailPath);
    QVERIFY(tailRes.ok());
    QVERIFY(tailRes.value() != baseRes.value());

    // 3. Append 1 byte
    const QString appendPath = tempDir.filePath(QStringLiteral("append.bin"));
    {
        QByteArray appendData = baseData;
        appendData.append('B');
        QFile f(appendPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(appendData);
    }
    const auto appendRes = FileFingerprint::compute(appendPath);
    QVERIFY(appendRes.ok());
    QVERIFY(appendRes.value() != baseRes.value());
}

void TstFileFingerprint::largeFileModificationInMiddleYieldsSameFingerprint()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Algorithm limitation: For files > 128 KiB, only [file size + first 64 KiB + last 64 KiB] are
    // hashed. Changing only the middle bytes (e.g. at 100 KiB) without changing file size produces
    // identical fingerprints. This design trade-off is intentional for scanning performance; audio
    // tag edits almost always affect the header or the file size.
    const qsizetype size = static_cast<qsizetype>(300) * 1024; // 300 KiB
    const QByteArray dataA(size, 'M');
    QByteArray dataB = dataA;
    dataB.replace(static_cast<qsizetype>(100) * 1024, 1,
        QByteArrayView("Z", 1)); // middle modification at 100 KiB

    const QString pathA = tempDir.filePath(QStringLiteral("largeA.bin"));
    const QString pathB = tempDir.filePath(QStringLiteral("largeB.bin"));
    {
        QFile fA(pathA);
        QVERIFY(fA.open(QIODevice::WriteOnly));
        fA.write(dataA);
    }
    {
        QFile fB(pathB);
        QVERIFY(fB.open(QIODevice::WriteOnly));
        fB.write(dataB);
    }

    const auto resA = FileFingerprint::compute(pathA);
    const auto resB = FileFingerprint::compute(pathB);
    QVERIFY(resA.ok());
    QVERIFY(resB.ok());
    QCOMPARE(resA.value(), resB.value());
}

void TstFileFingerprint::smallFileModificationChangesFingerprintAndEmptyFileComputable()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Small file (<= 128 KiB): any modification changes fingerprint
    const qsizetype size = static_cast<qsizetype>(100) * 1024; // 100 KiB <= 128 KiB
    const QByteArray dataA(size, 'S');
    QByteArray dataB = dataA;
    dataB.replace(
        static_cast<qsizetype>(50) * 1024, 1, QByteArrayView("X", 1)); // middle of small file

    const QString pathA = tempDir.filePath(QStringLiteral("smallA.bin"));
    const QString pathB = tempDir.filePath(QStringLiteral("smallB.bin"));
    {
        QFile fA(pathA);
        QVERIFY(fA.open(QIODevice::WriteOnly));
        fA.write(dataA);
    }
    {
        QFile fB(pathB);
        QVERIFY(fB.open(QIODevice::WriteOnly));
        fB.write(dataB);
    }

    const auto resA = FileFingerprint::compute(pathA);
    const auto resB = FileFingerprint::compute(pathB);
    QVERIFY(resA.ok());
    QVERIFY(resB.ok());
    QVERIFY(resA.value() != resB.value());

    // Empty file (0 bytes) can be computed
    const QString emptyPath = tempDir.filePath(QStringLiteral("empty.bin"));
    {
        QFile fEmpty(emptyPath);
        QVERIFY(fEmpty.open(QIODevice::WriteOnly));
    }
    const auto emptyRes = FileFingerprint::compute(emptyPath);
    QVERIFY(emptyRes.ok());
    QVERIFY(emptyRes.value().startsWith(QStringLiteral("v1:")));
    QCOMPARE(emptyRes.value().length(), 3 + 40);
}

void TstFileFingerprint::nonexistentPathReturnsFileReadError()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString nonExistent = tempDir.filePath(QStringLiteral("does_not_exist.bin"));
    const auto res = FileFingerprint::compute(nonExistent);
    QVERIFY(!res.ok());
    QCOMPARE(res.error().code, errc::kFileRead);
    QVERIFY(res.error().detail.contains(nonExistent));
}

} // namespace

QTEST_GUILESS_MAIN(TstFileFingerprint)

#include "tst_FileFingerprint.moc"
