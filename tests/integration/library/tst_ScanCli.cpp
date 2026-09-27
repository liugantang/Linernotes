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
};

void TstScanCli::scanCliSuccessfulRun()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString musicDir = tempDir.filePath(QStringLiteral("music"));
    copyDirContents(fixturePath(QStringLiteral("library")), musicDir);

    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));

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

} // namespace

QTEST_GUILESS_MAIN(TstScanCli)

#include "tst_ScanCli.moc"
