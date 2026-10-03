// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QObject>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <QTest>

#include <guide/LocalLyrics.h>

namespace {

using linernotes::guide::isUsableLyrics;
using linernotes::guide::sidecarLyrics;

class TstLocalLyrics : public QObject {
    Q_OBJECT

private slots:
    void readsSidecarLrcWithUppercaseExtension();
    void decodesGb18030LrcFile();
    void checksUsableLyrics();
};

void TstLocalLyrics::readsSidecarLrcWithUppercaseExtension()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString flacPath = tempDir.filePath(QStringLiteral("track01.flac"));
    const QString lrcPath = tempDir.filePath(QStringLiteral("track01.LRC"));

    {
        QFile flacFile(flacPath);
        QVERIFY(flacFile.open(QIODevice::WriteOnly));
        flacFile.write("dummy audio content");
    }

    {
        QFile lrcFile(lrcPath);
        QVERIFY(lrcFile.open(QIODevice::WriteOnly));
        lrcFile.write("[00:01.00] Line 1\n[00:02.00] Line 2\n[00:03.00] Line 3\n");
    }

    const auto lyricsOpt = sidecarLyrics(flacPath);
    QVERIFY(lyricsOpt.has_value());
    if (!lyricsOpt.has_value()) {
        return;
    }
    QCOMPARE(lyricsOpt.value(),
        QStringLiteral("[00:01.00] Line 1\n[00:02.00] Line 2\n[00:03.00] Line 3\n"));
}

void TstLocalLyrics::decodesGb18030LrcFile()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString audioPath = tempDir.filePath(QStringLiteral("song.mp3"));
    const QString lrcPath = tempDir.filePath(QStringLiteral("song.lrc"));

    {
        QFile audioFile(audioPath);
        QVERIFY(audioFile.open(QIODevice::WriteOnly));
        audioFile.write("dummy");
    }

    const QString expectedText
        = QStringLiteral("[00:01.00] 歌词第一行\n[00:02.00] 歌词第二行\n[00:03.00] 歌词第三行\n");
    QStringEncoder encoder("GB18030");
    QVERIFY(encoder.isValid());
    const QByteArray gbBytes = encoder(expectedText);

    {
        QFile lrcFile(lrcPath);
        QVERIFY(lrcFile.open(QIODevice::WriteOnly));
        lrcFile.write(gbBytes);
    }

    const auto lyricsOpt = sidecarLyrics(audioPath);
    QVERIFY(lyricsOpt.has_value());
    if (!lyricsOpt.has_value()) {
        return;
    }
    QCOMPARE(lyricsOpt.value(), expectedText);
}

void TstLocalLyrics::checksUsableLyrics()
{
    QVERIFY(!isUsableLyrics(QString()));
    QVERIFY(!isUsableLyrics(QStringLiteral("Line 1")));
    QVERIFY(!isUsableLyrics(QStringLiteral("Line 1\nLine 2")));
    QVERIFY(!isUsableLyrics(QStringLiteral("Line 1\n   \nLine 2\n\n")));
    QVERIFY(isUsableLyrics(QStringLiteral("Line 1\nLine 2\nLine 3")));
    QVERIFY(isUsableLyrics(QStringLiteral("Line 1\n\nLine 2\n   \nLine 3\n")));
}

} // namespace

QTEST_GUILESS_MAIN(TstLocalLyrics)

#include "tst_LocalLyrics.moc"
