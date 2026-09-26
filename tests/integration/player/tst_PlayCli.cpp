// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <common/TestSupport.h>
#include <player/PlayMode.h>
#include <player/PlaybackSnapshot.h>

namespace {

class TstPlayCli : public QObject {
    Q_OBJECT

private slots:
    void playsFilesInOrderAndExits();
    void reportsErrorForMissingFile();
    void noArgumentsPrintsUsage();
    void quitCommandSavesState();
    void restoresFromStateFile();
    void sigtermExitsGracefully();
};

void TstPlayCli::playsFilesInOrderAndExits()
{
    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const QString p660 = linernotes::test::fixturePath(QStringLiteral("audio/tone_660_1s.flac"));
    QVERIFY(QFile::exists(p440));
    QVERIFY(QFile::exists(p660));

    QProcess proc;
    proc.setProgram(QString::fromUtf8(PLAYCLI_PATH));
    proc.setArguments({ QStringLiteral("--ao"), QStringLiteral("null"), p440, p660 });
    proc.start();

    QVERIFY(proc.waitForFinished(10000));
    QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
    QCOMPARE(proc.exitCode(), 0);

    const QString stdoutStr = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList lines = stdoutStr.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    int idx440 = -1;
    int idx660 = -1;
    int idxFinished = -1;

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (line.startsWith(QStringLiteral("PLAYING 0"))
            && line.contains(QStringLiteral("tone_440_1s"))) {
            if (idx440 == -1) {
                idx440 = i;
            }
        } else if (line.startsWith(QStringLiteral("PLAYING 1"))
            && line.contains(QStringLiteral("tone_660_1s"))) {
            if (idx660 == -1) {
                idx660 = i;
            }
        } else if (line == QStringLiteral("FINISHED")) {
            if (idxFinished == -1) {
                idxFinished = i;
            }
        }
    }

    QVERIFY2(idx440 != -1, "Expected 'PLAYING 0 ...tone_440_1s...' in output");
    QVERIFY2(idx660 != -1, "Expected 'PLAYING 1 ...tone_660_1s...' in output");
    QVERIFY2(idxFinished != -1, "Expected 'FINISHED' in output");
    QVERIFY2(idx440 < idx660, "PLAYING 0 must appear before PLAYING 1");
    QVERIFY2(idx660 < idxFinished, "PLAYING 1 must appear before FINISHED");
}

void TstPlayCli::reportsErrorForMissingFile()
{
    const QString missing = QStringLiteral("/nonexistent/path/missing_track.flac");
    const QString p440 = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    QVERIFY(QFile::exists(p440));

    QProcess proc;
    proc.setProgram(QString::fromUtf8(PLAYCLI_PATH));
    proc.setArguments({ QStringLiteral("--ao"), QStringLiteral("null"), missing, p440 });
    proc.start();

    QVERIFY(proc.waitForFinished(10000));
    QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
    QCOMPARE(proc.exitCode(), 0);

    const QString stdoutStr = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList lines = stdoutStr.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    int idxError = -1;
    int idx440 = -1;
    int idxFinished = -1;

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (line.startsWith(QStringLiteral("ERROR ")) && line.contains(missing)) {
            if (idxError == -1) {
                idxError = i;
            }
        } else if (line.startsWith(QStringLiteral("PLAYING 1"))
            && line.contains(QStringLiteral("tone_440_1s"))) {
            if (idx440 == -1) {
                idx440 = i;
            }
        } else if (line == QStringLiteral("FINISHED")) {
            if (idxFinished == -1) {
                idxFinished = i;
            }
        }
    }

    QVERIFY2(idxError != -1, "Expected 'ERROR /nonexistent/path/missing_track.flac' in output");
    QVERIFY2(idx440 != -1, "Expected 'PLAYING 1 ...tone_440_1s...' in output");
    QVERIFY2(idxFinished != -1, "Expected 'FINISHED' in output");
    QVERIFY2(idxError < idx440, "ERROR must appear before PLAYING 1");
    QVERIFY2(idx440 < idxFinished, "PLAYING 1 must appear before FINISHED");
}

void TstPlayCli::noArgumentsPrintsUsage()
{
    QProcess proc;
    proc.setProgram(QString::fromUtf8(PLAYCLI_PATH));
    proc.start();

    QVERIFY(proc.waitForFinished(5000));
    QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
    QCOMPARE(proc.exitCode(), 2);
}

void TstPlayCli::quitCommandSavesState()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString stateFilePath = tempDir.filePath(QStringLiteral("state.json"));

    const QString silence = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    QVERIFY(QFile::exists(silence));

    QProcess proc;
    proc.setProgram(QString::fromUtf8(PLAYCLI_PATH));
    proc.setArguments({ QStringLiteral("--ao"), QStringLiteral("null"),
        QStringLiteral("--state-file"), stateFilePath, silence });
    proc.start();

    QVERIFY(proc.waitForStarted(5000));

    QString accumulatedOutput;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 && !accumulatedOutput.contains(QStringLiteral("PLAYING 0"))) {
        if (proc.waitForReadyRead(100)) {
            accumulatedOutput += QString::fromUtf8(proc.readAllStandardOutput());
        }
    }
    QVERIFY2(
        accumulatedOutput.contains(QStringLiteral("PLAYING 0")), "Process should start playing");

    proc.write("q\n");
    proc.waitForBytesWritten(1000);

    QVERIFY(proc.waitForFinished(5000));
    QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
    QCOMPARE(proc.exitCode(), 0);

    QVERIFY(QFile::exists(stateFilePath));
    linernotes::player::PlaybackStateStore store(stateFilePath);
    const auto snapshot = store.load();
    QVERIFY(snapshot.has_value());
    if (!snapshot) {
        return;
    }
    const auto &snap = *snapshot;
    QCOMPARE(snap.items.size(), 1);
    QCOMPARE(snap.items.at(0).source, silence);
    QCOMPARE(snap.currentIndex, 0);
}

void TstPlayCli::restoresFromStateFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString stateFilePath = tempDir.filePath(QStringLiteral("state.json"));

    const QString silence = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    QVERIFY(QFile::exists(silence));

    linernotes::player::PlaybackSnapshot snap;
    snap.items = { { .source = silence } };
    snap.currentIndex = 0;
    snap.position = 1.0;
    snap.mode = linernotes::player::PlayMode::RepeatAll;

    linernotes::player::PlaybackStateStore store(stateFilePath);
    QVERIFY(store.save(snap));

    QProcess proc;
    proc.setProgram(QString::fromUtf8(PLAYCLI_PATH));
    proc.setArguments({ QStringLiteral("--ao"), QStringLiteral("null"),
        QStringLiteral("--state-file"), stateFilePath });
    proc.start();

    QVERIFY(proc.waitForStarted(5000));

    QString accumulatedOutput;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 && !accumulatedOutput.contains(QStringLiteral("STATE Paused"))) {
        if (proc.waitForReadyRead(100)) {
            accumulatedOutput += QString::fromUtf8(proc.readAllStandardOutput());
        }
    }
    QVERIFY2(accumulatedOutput.contains(QStringLiteral("STATE Paused")), "Expected 'STATE Paused'");

    proc.write("q\n");
    proc.waitForBytesWritten(1000);

    QVERIFY(proc.waitForFinished(5000));
    QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
    QCOMPARE(proc.exitCode(), 0);
}

void TstPlayCli::sigtermExitsGracefully()
{
    const QString silence = linernotes::test::fixturePath(QStringLiteral("audio/silence_5s.flac"));
    QVERIFY(QFile::exists(silence));

    QProcess proc;
    proc.setProgram(QString::fromUtf8(PLAYCLI_PATH));
    proc.setArguments({ QStringLiteral("--ao"), QStringLiteral("null"), silence });
    proc.start();

    QVERIFY(proc.waitForStarted(5000));

    QString accumulatedOutput;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 && !accumulatedOutput.contains(QStringLiteral("PLAYING 0"))) {
        if (proc.waitForReadyRead(100)) {
            accumulatedOutput += QString::fromUtf8(proc.readAllStandardOutput());
        }
    }
    QVERIFY2(
        accumulatedOutput.contains(QStringLiteral("PLAYING 0")), "Process should start playing");

    proc.terminate();

    QVERIFY(proc.waitForFinished(5000));
    QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
    QCOMPARE(proc.exitCode(), 0);
}

} // namespace

QTEST_MAIN(TstPlayCli)
#include "tst_PlayCli.moc"
