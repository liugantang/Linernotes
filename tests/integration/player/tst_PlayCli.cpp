// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QProcess>
#include <QTest>

#include <common/TestSupport.h>

namespace {

class TstPlayCli : public QObject {
    Q_OBJECT

private slots:
    void playsFilesInOrderAndExits();
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

} // namespace

QTEST_MAIN(TstPlayCli)
#include "tst_PlayCli.moc"
