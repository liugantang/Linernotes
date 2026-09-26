// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <common/TestSupport.h>

namespace {

using linernotes::test::fixturePath;

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

class TstScanCli : public QObject {
    Q_OBJECT

private slots:
    void scanCliSuccessfulRun();
    void noArgumentsPrintsUsageAndReturns2();
    void scanCliWithCacheGeneratesThumbnails();
};

void TstScanCli::scanCliSuccessfulRun()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    copyDirContents(fixturePath(QStringLiteral("library")), musicDir);

    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));

    // First scan
    {
        QProcess proc;
        proc.setProgram(QString::fromUtf8(SCANCLI_PATH));
        proc.setArguments({ QStringLiteral("--db"), dbPath, musicDir });
        proc.start();

        QVERIFY(proc.waitForFinished(30000));
        QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
        QCOMPARE(proc.exitCode(), 0);

        const QString stdoutStr = QString::fromUtf8(proc.readAllStandardOutput());
        QVERIFY2(stdoutStr.contains(QStringLiteral("Found")), "Expected 'Found' in output");
        QVERIFY2(stdoutStr.contains(QStringLiteral("added")), "Expected 'added' in output");
        QVERIFY2(stdoutStr.contains(QStringLiteral("Total tracks in database:")),
            "Expected 'Total tracks in database:' in output");
    }

    // Second scan (all unchanged)
    {
        QProcess proc;
        proc.setProgram(QString::fromUtf8(SCANCLI_PATH));
        proc.setArguments({ QStringLiteral("--db"), dbPath, musicDir });
        proc.start();

        QVERIFY(proc.waitForFinished(30000));
        QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
        QCOMPARE(proc.exitCode(), 0);

        const QString stdoutStr = QString::fromUtf8(proc.readAllStandardOutput());
        QVERIFY2(stdoutStr.contains(QStringLiteral("0 added")), "Expected '0 added' in output");
        QVERIFY2(stdoutStr.contains(QStringLiteral("unchanged")), "Expected 'unchanged' in output");
    }
}

void TstScanCli::noArgumentsPrintsUsageAndReturns2()
{
    QProcess proc;
    proc.setProgram(QString::fromUtf8(SCANCLI_PATH));
    proc.start();

    QVERIFY(proc.waitForFinished(5000));
    QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
    QCOMPARE(proc.exitCode(), 2);
}

void TstScanCli::scanCliWithCacheGeneratesThumbnails()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    copyDirContents(fixturePath(QStringLiteral("library")), musicDir);

    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));
    const QString cacheDir = tempDir.filePath(QStringLiteral("cache"));

    QProcess proc;
    proc.setProgram(QString::fromUtf8(SCANCLI_PATH));
    proc.setArguments(
        { QStringLiteral("--db"), dbPath, QStringLiteral("--cache"), cacheDir, musicDir });
    proc.start();

    QVERIFY(proc.waitForFinished(30000));
    QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
    QCOMPARE(proc.exitCode(), 0);

    // Verify cache directory has generated files
    QDirIterator it(cacheDir, QDir::Files, QDirIterator::Subdirectories);
    int fileCount = 0;
    while (it.hasNext()) {
        it.next();
        fileCount++;
    }
    QVERIFY(fileCount > 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstScanCli)

#include "tst_ScanCli.moc"
