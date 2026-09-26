// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QTest>
#include <QtConcurrent/QtConcurrent>

#include <common/TestSupport.h>
#include <library/Errors.h>
#include <library/TagReader.h>

namespace {

using linernotes::library::TagReader;
using linernotes::library::TagReadResult;
using linernotes::test::fixturePath;
namespace errc = linernotes::library::errc;

class TstTagReader : public QObject {
    Q_OBJECT

private slots:
    void readsAudioProperties_data();
    void readsAudioProperties();
    void readsId3v2AndFlacTags();
    void readsMp4WavAndApeTags();
    void keepsLatin1RawBytes_data();
    void keepsLatin1RawBytes();
    void readsCoexistingV1AndV2();
    void readsEmbeddedFrontCover();
    void failsGracefully_data();
    void failsGracefully();
    void concurrentReadsAreSafe();
};

void TstTagReader::readsAudioProperties_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QString>("expectedContainer");
    QTest::addColumn<QString>("expectedCodec");
    QTest::addColumn<int>("expectedSampleRate");
    QTest::addColumn<int>("expectedChannels");
    QTest::addColumn<int>("expectedBitDepth");

    QTest::newRow("mp3") << QStringLiteral("library/mp3_id3v24_utf8.mp3") << QStringLiteral("mp3")
                         << QStringLiteral("mp3") << 22050 << 1 << 0;
    QTest::newRow("flac") << QStringLiteral("library/flac_vorbis.flac") << QStringLiteral("flac")
                          << QStringLiteral("flac") << 22050 << 1 << 16;
    QTest::newRow("ogg") << QStringLiteral("library/ogg_vorbis.ogg") << QStringLiteral("ogg")
                         << QStringLiteral("vorbis") << 22050 << 1 << 0;
    QTest::newRow("opus") << QStringLiteral("library/opus.opus") << QStringLiteral("opus")
                          << QStringLiteral("opus") << 48000 << 1 << 0;
    QTest::newRow("m4a_aac") << QStringLiteral("library/m4a_aac.m4a") << QStringLiteral("mp4")
                             << QStringLiteral("aac") << 22050 << 1 << 0;
    QTest::newRow("wav") << QStringLiteral("library/wav_id3.wav") << QStringLiteral("wav")
                         << QStringLiteral("pcm") << 22050 << 1 << 16;
    QTest::newRow("wavpack") << QStringLiteral("library/wavpack_ape.wv")
                             << QStringLiteral("wavpack") << QStringLiteral("wavpack") << 22050 << 1
                             << 16;
}

void TstTagReader::readsAudioProperties()
{
    QFETCH(QString, fileName);
    QFETCH(QString, expectedContainer);
    QFETCH(QString, expectedCodec);
    QFETCH(int, expectedSampleRate);
    QFETCH(int, expectedChannels);
    QFETCH(int, expectedBitDepth);

    const auto res = TagReader::read(fixturePath(fileName));
    QVERIFY(res.ok());
    const auto &audio = res.value().audio;
    QCOMPARE(audio.container, expectedContainer);
    QCOMPARE(audio.codec, expectedCodec);
    QVERIFY(audio.durationMs >= 900 && audio.durationMs <= 1100);
    QCOMPARE(audio.sampleRate, expectedSampleRate);
    QCOMPARE(audio.channels, expectedChannels);
    QCOMPARE(audio.bitDepth, expectedBitDepth);
}

void TstTagReader::readsId3v2AndFlacTags()
{
    // MP3 ID3v2.4 UTF-8: multi-artist, title, album, lyrics, cover flag
    {
        const auto res
            = TagReader::read(fixturePath(QStringLiteral("library/mp3_id3v24_utf8.mp3")));
        QVERIFY(res.ok());
        const auto &val = res.value();
        QVERIFY(val.hasEmbeddedCover);

        QStringList artists;
        QString title;
        for (const auto &t : val.tags) {
            if (t.key == QStringLiteral("ARTIST")) {
                artists.append(t.value);
            } else if (t.key == QStringLiteral("TITLE")) {
                title = t.value;
            }
        }
        QCOMPARE(title, QStringLiteral("晨曦微光"));
        QCOMPARE(artists, (QStringList { QStringLiteral("林晓风"), QStringLiteral("夜行者") }));
    }

    // FLAC Vorbis comment: multi-value artist, track/total, non-ascii path
    {
        const auto res = TagReader::read(fixturePath(QStringLiteral("library/flac_vorbis.flac")));
        QVERIFY(res.ok());
        const auto &val = res.value();
        QVERIFY(val.hasEmbeddedCover);

        QString trackNum;
        QString lyrics;
        for (const auto &t : val.tags) {
            if (t.key == QStringLiteral("TRACKNUMBER")) {
                trackNum = t.value;
            } else if (t.key == QStringLiteral("LYRICS")) {
                lyrics = t.value;
            }
        }
        QCOMPARE(trackNum, QStringLiteral("3"));
        QVERIFY(lyrics.contains(QStringLiteral("星光落在海面上")));
    }

    // FLAC without tags
    {
        const auto res = TagReader::read(fixturePath(QStringLiteral("library/flac_no_tags.flac")));
        QVERIFY(res.ok());
        QVERIFY(res.value().tags.isEmpty());
        QVERIFY(!res.value().hasEmbeddedCover);
    }
}

void TstTagReader::readsMp4WavAndApeTags()
{
    // MP4 / M4A AAC
    {
        const auto res = TagReader::read(fixturePath(QStringLiteral("library/m4a_aac.m4a")));
        QVERIFY(res.ok());
        const auto &val = res.value();
        QVERIFY(val.hasEmbeddedCover);
        QString title;
        for (const auto &t : val.tags) {
            if (t.key == QStringLiteral("TITLE")) {
                title = t.value;
            }
        }
        QCOMPARE(title, QStringLiteral("月光奏鸣"));
    }

    // WavPack APE
    {
        const auto res = TagReader::read(fixturePath(QStringLiteral("library/wavpack_ape.wv")));
        QVERIFY(res.ok());
        QString title;
        for (const auto &t : res.value().tags) {
            if (t.key == QStringLiteral("TITLE")) {
                title = t.value;
                QCOMPARE(t.tagType, QStringLiteral("ape"));
            }
        }
        QCOMPARE(title, QStringLiteral("无损压缩之梦"));
    }

    // WAV ID3 & OGG Vorbis
    {
        const auto resWav = TagReader::read(fixturePath(QStringLiteral("library/wav_id3.wav")));
        QVERIFY(resWav.ok());
        const auto resOgg = TagReader::read(fixturePath(QStringLiteral("library/ogg_vorbis.ogg")));
        QVERIFY(resOgg.ok());
    }
}

void TstTagReader::keepsLatin1RawBytes_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QString>("key");
    QTest::addColumn<QByteArray>("expectedRawBytes");

    QTest::newRow("v23_gbk") << QStringLiteral("library/mp3_id3v23_gbk.mp3")
                             << QStringLiteral("TITLE")
                             << QByteArray::fromHex("cdedb7e7c0efb5c4b8e8");
    QTest::newRow("v23_shiftjis") << QStringLiteral("library/mp3_id3v23_shiftjis.mp3")
                                  << QStringLiteral("TITLE")
                                  << QByteArray::fromHex("894a82cc93fa82cc8e5595e0");
    QTest::newRow("v23_euckr") << QStringLiteral("library/mp3_id3v23_euckr.mp3")
                               << QStringLiteral("TITLE")
                               << QByteArray::fromHex("bbf5baaec0c720b3ebb7a1");
    QTest::newRow("v1_gbk") << QStringLiteral("library/mp3_id3v1_gbk.mp3")
                            << QStringLiteral("TITLE")
                            << QByteArray::fromHex("cdedb7e7c0efb5c4b8e8");
    QTest::newRow("v1_big5") << QStringLiteral("library/mp3_id3v1_big5.mp3")
                             << QStringLiteral("TITLE")
                             << QByteArray::fromHex("b1dfadb7b8ccaababa71");
}

void TstTagReader::keepsLatin1RawBytes()
{
    QFETCH(QString, fileName);
    QFETCH(QString, key);
    QFETCH(QByteArray, expectedRawBytes);

    const auto res = TagReader::read(fixturePath(fileName));
    QVERIFY(res.ok());

    bool found = false;
    for (const auto &t : res.value().tags) {
        if (t.key == key) {
            found = true;
            QCOMPARE(t.rawBytes, expectedRawBytes);
            QCOMPARE(t.rawEncoding, QStringLiteral("latin1"));
            QCOMPARE(t.value, QString::fromLatin1(expectedRawBytes));
            break;
        }
    }
    QVERIFY(found);
}

void TstTagReader::readsCoexistingV1AndV2()
{
    const auto res = TagReader::read(fixturePath(QStringLiteral("library/mp3_v1_and_v2.mp3")));
    QVERIFY(res.ok());

    bool foundV2 = false;
    bool foundV1 = false;
    for (const auto &t : res.value().tags) {
        if (t.tagType == QStringLiteral("id3v2") && t.priority == 0
            && t.key == QStringLiteral("TITLE")) {
            QCOMPARE(t.value, QStringLiteral("晴空之下"));
            foundV2 = true;
        } else if (t.tagType == QStringLiteral("id3v1") && t.priority == 9
            && t.key == QStringLiteral("TITLE")) {
            QCOMPARE(t.value, QStringLiteral("Old Title V1"));
            foundV1 = true;
        }
    }
    QVERIFY(foundV2 && foundV1);
}

void TstTagReader::readsEmbeddedFrontCover()
{
    // Embedded cover exists
    {
        const auto res
            = TagReader::read(fixturePath(QStringLiteral("library/cover_1600_embed.mp3")));
        QVERIFY(res.ok());
        const auto &val = res.value();
        QVERIFY(val.hasEmbeddedCover);
        if (!val.frontCover.has_value()) {
            QFAIL("Embedded frontCover expected");
            return;
        }
        const auto &cover = *val.frontCover;
        QCOMPARE(cover.mimeType, QStringLiteral("image/jpeg"));
        QVERIFY(cover.data.size() >= 1000);

        QImage img;
        QVERIFY(img.loadFromData(cover.data) && !img.isNull());
    }

    // No cover returns nullopt
    {
        const auto res = TagReader::read(fixturePath(QStringLiteral("library/ogg_vorbis.ogg")));
        QVERIFY(res.ok());
        QVERIFY(!res.value().hasEmbeddedCover && !res.value().frontCover.has_value());
    }
}

void TstTagReader::failsGracefully_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<bool>("expectOk");

    QTest::newRow("corrupt_truncated") << QStringLiteral("library/corrupt_truncated.mp3") << true;
    QTest::newRow("corrupt_garbage") << QStringLiteral("library/corrupt_garbage.flac") << false;
    QTest::newRow("empty") << QStringLiteral("library/empty.mp3") << false;
    QTest::newRow("nonexistent") << QStringLiteral("library/nonexistent_file_12345.mp3") << false;
}

void TstTagReader::failsGracefully()
{
    QFETCH(QString, fileName);
    QFETCH(bool, expectOk);
    const QString path = fixturePath(fileName);
    const auto res = TagReader::read(path);

    QCOMPARE(res.ok(), expectOk);
    if (!res.ok()) {
        const auto &err = res.error();
        QVERIFY(err.code == errc::kTagRead || err.code == errc::kTagUnsupported);
        QVERIFY(err.detail.contains(path));
    }
}

void TstTagReader::concurrentReadsAreSafe()
{
    const QStringList sampleFiles = {
        QStringLiteral("library/mp3_id3v24_utf8.mp3"),
        QStringLiteral("library/flac_vorbis.flac"),
        QStringLiteral("library/m4a_aac.m4a"),
        QStringLiteral("library/wavpack_ape.wv"),
    };

    QList<QString> fullPaths;
    for (const auto &rel : sampleFiles) {
        fullPaths.append(fixturePath(rel));
    }

    QList<QString> taskList;
    for (int rep = 0; rep < 10; ++rep) {
        taskList.append(fullPaths);
    }

    const QList<linernotes::core::Result<TagReadResult>> results = QtConcurrent::blockingMapped(
        taskList, [](const QString &p) { return TagReader::read(p); });

    QCOMPARE(results.size(), taskList.size());
    for (const auto &r : results) {
        QVERIFY(r.ok());
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstTagReader)

#include "tst_TagReader.moc"
