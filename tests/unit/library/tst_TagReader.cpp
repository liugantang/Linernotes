// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QTest>
#include <QtConcurrent/QtConcurrent>

#include <common/TestSupport.h>
#include <library/Errors.h>
#include <library/TagReader.h>

namespace {

using linernotes::library::RawTag;
using linernotes::library::TagReader;
using linernotes::library::TagReadResult;
using linernotes::test::fixturePath;
namespace errc = linernotes::library::errc;

class TstTagReader : public QObject {
    Q_OBJECT

private slots:
    void readsAudioProperties_data();
    void readsAudioProperties();
    void readsId3v24Utf8Fields();
    void keepsLatin1RawBytes_data();
    void keepsLatin1RawBytes();
    void readsEachContainerSeparately();
    void readsFlacVorbisMultiValue();
    void readsMp4Tags();
    void readsWavpackApe();
    void readsWavId3();
    void readsOggAndOpus();
    void noTagsFileHasEmptyTagList();
    void readsNonAsciiPath();
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
    QTest::newRow("m4a_alac") << QStringLiteral("library/m4a_alac.m4a") << QStringLiteral("mp4")
                              << QStringLiteral("alac") << 22050 << 1 << 16;
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

    const QString path = fixturePath(fileName);
    const auto res = TagReader::read(path);
    QVERIFY(res.ok());

    const auto &audio = res.value().audio;
    QCOMPARE(audio.container, expectedContainer);
    QCOMPARE(audio.codec, expectedCodec);
    QVERIFY2(audio.durationMs >= 900 && audio.durationMs <= 1100,
        qPrintable(QStringLiteral("durationMs=%1 outside 900..1100").arg(audio.durationMs)));
    QCOMPARE(audio.sampleRate, expectedSampleRate);
    QCOMPARE(audio.channels, expectedChannels);
    QCOMPARE(audio.bitDepth, expectedBitDepth);
}

void TstTagReader::readsId3v24Utf8Fields()
{
    const QString path = fixturePath(QStringLiteral("library/mp3_id3v24_utf8.mp3"));
    const auto res = TagReader::read(path);
    QVERIFY(res.ok());

    const auto &val = res.value();
    QVERIFY(val.hasEmbeddedCover);

    auto findTags = [&](const QString &key) {
        QList<RawTag> matches;
        for (const auto &t : val.tags) {
            if (t.key == key) {
                matches.append(t);
            }
        }
        return matches;
    };

    const auto titleTags = findTags(QStringLiteral("TITLE"));
    QCOMPARE(titleTags.size(), 1);
    QCOMPARE(titleTags.at(0).value, QStringLiteral("晨曦微光"));
    QCOMPARE(titleTags.at(0).priority, 0);
    QCOMPARE(titleTags.at(0).tagType, QStringLiteral("id3v2"));
    QVERIFY(titleTags.at(0).rawBytes.isEmpty());
    QVERIFY(titleTags.at(0).rawEncoding.isEmpty());

    const auto artistTags = findTags(QStringLiteral("ARTIST"));
    QCOMPARE(artistTags.size(), 2);
    QCOMPARE(artistTags.at(0).ordinal, 0);
    QCOMPARE(artistTags.at(0).value, QStringLiteral("林晓风"));
    QCOMPARE(artistTags.at(1).ordinal, 1);
    QCOMPARE(artistTags.at(1).value, QStringLiteral("夜行者"));
    QVERIFY(artistTags.at(0).rawBytes.isEmpty());
    QVERIFY(artistTags.at(1).rawBytes.isEmpty());

    const auto albumArtistTags = findTags(QStringLiteral("ALBUMARTIST"));
    QCOMPARE(albumArtistTags.size(), 1);
    QCOMPARE(albumArtistTags.at(0).value, QStringLiteral("林晓风"));

    const auto albumTags = findTags(QStringLiteral("ALBUM"));
    QCOMPARE(albumTags.size(), 1);
    QCOMPARE(albumTags.at(0).value, QStringLiteral("山谷的回响"));

    const auto dateTags = findTags(QStringLiteral("DATE"));
    QCOMPARE(dateTags.size(), 1);
    QCOMPARE(dateTags.at(0).value, QStringLiteral("2003-07-31"));

    const auto trackTags = findTags(QStringLiteral("TRACKNUMBER"));
    QCOMPARE(trackTags.size(), 1);
    QCOMPARE(trackTags.at(0).value, QStringLiteral("3/12"));

    const auto discTags = findTags(QStringLiteral("DISCNUMBER"));
    QCOMPARE(discTags.size(), 1);
    QCOMPARE(discTags.at(0).value, QStringLiteral("1/2"));

    const auto genreTags = findTags(QStringLiteral("GENRE"));
    QCOMPARE(genreTags.size(), 1);
    QCOMPARE(genreTags.at(0).value, QStringLiteral("Folk"));

    const auto compTags = findTags(QStringLiteral("COMPOSER"));
    QCOMPARE(compTags.size(), 1);
    QCOMPARE(compTags.at(0).value, QStringLiteral("林晓风"));

    const auto mbTrackTags = findTags(QStringLiteral("MUSICBRAINZ_TRACKID"));
    QCOMPARE(mbTrackTags.size(), 1);
    QCOMPARE(mbTrackTags.at(0).value, QStringLiteral("c3898183-b787-43c2-bf72-888e0013b860"));

    const auto rgTrackTags = findTags(QStringLiteral("REPLAYGAIN_TRACK_GAIN"));
    QCOMPARE(rgTrackTags.size(), 1);
    QCOMPARE(rgTrackTags.at(0).value, QStringLiteral("-6.50 dB"));

    const auto lyricsTags = findTags(QStringLiteral("LYRICS"));
    QCOMPARE(lyricsTags.size(), 1);
    QVERIFY(lyricsTags.at(0).value.contains(QStringLiteral("风穿过山谷")));
}

void TstTagReader::keepsLatin1RawBytes_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QString>("key");
    QTest::addColumn<QByteArray>("expectedRawBytes");

    // GBK ID3v2.3
    QTest::newRow("v23_gbk_title")
        << QStringLiteral("library/mp3_id3v23_gbk.mp3") << QStringLiteral("TITLE")
        << QByteArray::fromHex("cdedb7e7c0efb5c4b8e8");
    QTest::newRow("v23_gbk_artist")
        << QStringLiteral("library/mp3_id3v23_gbk.mp3") << QStringLiteral("ARTIST")
        << QByteArray::fromHex("c1d6cffeb7e7");
    QTest::newRow("v23_gbk_album")
        << QStringLiteral("library/mp3_id3v23_gbk.mp3") << QStringLiteral("ALBUM")
        << QByteArray::fromHex("c9bdb9c8b5c4bbd8cfec");

    // Shift-JIS ID3v2.3
    QTest::newRow("v23_shiftjis_title")
        << QStringLiteral("library/mp3_id3v23_shiftjis.mp3") << QStringLiteral("TITLE")
        << QByteArray::fromHex("894a82cc93fa82cc8e5595e0");
    QTest::newRow("v23_shiftjis_artist")
        << QStringLiteral("library/mp3_id3v23_shiftjis.mp3") << QStringLiteral("ARTIST")
        << QByteArray::fromHex("8db293a1959789d4");
    QTest::newRow("v23_shiftjis_album")
        << QStringLiteral("library/mp3_id3v23_shiftjis.mp3") << QStringLiteral("ALBUM")
        << QByteArray::fromHex("90c382a982c896e9");

    // EUC-KR ID3v2.3
    QTest::newRow("v23_euckr_title")
        << QStringLiteral("library/mp3_id3v23_euckr.mp3") << QStringLiteral("TITLE")
        << QByteArray::fromHex("bbf5baaec0c720b3ebb7a1");
    QTest::newRow("v23_euckr_artist")
        << QStringLiteral("library/mp3_id3v23_euckr.mp3") << QStringLiteral("ARTIST")
        << QByteArray::fromHex("b1e8b9d9b6f7");
    QTest::newRow("v23_euckr_album")
        << QStringLiteral("library/mp3_id3v23_euckr.mp3") << QStringLiteral("ALBUM")
        << QByteArray::fromHex("b5b5bdc3c0c720b2de");

    // GBK ID3v1
    QTest::newRow("v1_gbk_title") << QStringLiteral("library/mp3_id3v1_gbk.mp3")
                                  << QStringLiteral("TITLE")
                                  << QByteArray::fromHex("cdedb7e7c0efb5c4b8e8");
    QTest::newRow("v1_gbk_artist")
        << QStringLiteral("library/mp3_id3v1_gbk.mp3") << QStringLiteral("ARTIST")
        << QByteArray::fromHex("c1d6cffeb7e7");
    QTest::newRow("v1_gbk_album") << QStringLiteral("library/mp3_id3v1_gbk.mp3")
                                  << QStringLiteral("ALBUM")
                                  << QByteArray::fromHex("c9bdb9c8b5c4bbd8cfec");
    QTest::newRow("v1_gbk_date") << QStringLiteral("library/mp3_id3v1_gbk.mp3")
                                 << QStringLiteral("DATE") << QByteArray("2023");
    QTest::newRow("v1_gbk_comment")
        << QStringLiteral("library/mp3_id3v1_gbk.mp3") << QStringLiteral("COMMENT")
        << QByteArray::fromHex("d0e9b9b9b2e2cad4");

    // Big5 ID3v1
    QTest::newRow("v1_big5_title")
        << QStringLiteral("library/mp3_id3v1_big5.mp3") << QStringLiteral("TITLE")
        << QByteArray::fromHex("b1dfadb7b8ccaababa71");
    QTest::newRow("v1_big5_artist")
        << QStringLiteral("library/mp3_id3v1_big5.mp3") << QStringLiteral("ARTIST")
        << QByteArray::fromHex("aa4cbee5adb7");
    QTest::newRow("v1_big5_album")
        << QStringLiteral("library/mp3_id3v1_big5.mp3") << QStringLiteral("ALBUM")
        << QByteArray::fromHex("a473a8a6aabab06ac554");
    QTest::newRow("v1_big5_date") << QStringLiteral("library/mp3_id3v1_big5.mp3")
                                  << QStringLiteral("DATE") << QByteArray("2023");
    QTest::newRow("v1_big5_comment")
        << QStringLiteral("library/mp3_id3v1_big5.mp3") << QStringLiteral("COMMENT")
        << QByteArray::fromHex("b5eaba63b4fab8d5");
}

void TstTagReader::keepsLatin1RawBytes()
{
    QFETCH(QString, fileName);
    QFETCH(QString, key);
    QFETCH(QByteArray, expectedRawBytes);

    const QString path = fixturePath(fileName);
    const auto res = TagReader::read(path);
    QVERIFY(res.ok());

    const auto &tags = res.value().tags;
    bool found = false;
    for (const auto &t : tags) {
        if (t.key == key) {
            found = true;
            QCOMPARE(t.rawBytes, expectedRawBytes);
            QCOMPARE(t.rawEncoding, QStringLiteral("latin1"));
            QCOMPARE(t.value, QString::fromLatin1(expectedRawBytes));
            break;
        }
    }
    QVERIFY2(found, qPrintable(QStringLiteral("Tag key %1 not found in %2").arg(key, fileName)));
}

void TstTagReader::readsEachContainerSeparately()
{
    const QString path = fixturePath(QStringLiteral("library/mp3_v1_and_v2.mp3"));
    const auto res = TagReader::read(path);
    QVERIFY(res.ok());

    const auto &tags = res.value().tags;
    bool foundV2Title = false;
    bool foundV1Title = false;
    bool foundV2Artist = false;
    bool foundV1Artist = false;

    for (const auto &t : tags) {
        if (t.tagType == QStringLiteral("id3v2") && t.priority == 0) {
            if (t.key == QStringLiteral("TITLE")) {
                QCOMPARE(t.value, QStringLiteral("晴空之下"));
                foundV2Title = true;
            } else if (t.key == QStringLiteral("ARTIST")) {
                QCOMPARE(t.value, QStringLiteral("云端漫步"));
                foundV2Artist = true;
            }
        } else if (t.tagType == QStringLiteral("id3v1") && t.priority == 9) {
            if (t.key == QStringLiteral("TITLE")) {
                QCOMPARE(t.value, QStringLiteral("Old Title V1"));
                foundV1Title = true;
            } else if (t.key == QStringLiteral("ARTIST")) {
                QCOMPARE(t.value, QStringLiteral("Old Artist V1"));
                foundV1Artist = true;
            }
        }
    }

    QVERIFY(foundV2Title);
    QVERIFY(foundV1Title);
    QVERIFY(foundV2Artist);
    QVERIFY(foundV1Artist);
}

void TstTagReader::readsFlacVorbisMultiValue()
{
    const QString path = fixturePath(QStringLiteral("library/flac_vorbis.flac"));
    const auto res = TagReader::read(path);
    QVERIFY(res.ok());

    const auto &val = res.value();
    QVERIFY(val.hasEmbeddedCover);

    QList<RawTag> artists;
    QString trackNum;
    QString trackTotal;
    QString lyrics;

    for (const auto &t : val.tags) {
        if (t.key == QStringLiteral("ARTIST")) {
            artists.append(t);
        } else if (t.key == QStringLiteral("TRACKNUMBER")) {
            trackNum = t.value;
        } else if (t.key == QStringLiteral("TRACKTOTAL")) {
            trackTotal = t.value;
        } else if (t.key == QStringLiteral("LYRICS")) {
            lyrics = t.value;
        }
    }

    QCOMPARE(artists.size(), 2);
    QCOMPARE(artists.at(0).ordinal, 0);
    QCOMPARE(artists.at(0).value, QStringLiteral("林晓风"));
    QCOMPARE(artists.at(1).ordinal, 1);
    QCOMPARE(artists.at(1).value, QStringLiteral("夜行者"));

    QCOMPARE(trackNum, QStringLiteral("3"));
    QCOMPARE(trackTotal, QStringLiteral("12"));
    QVERIFY(lyrics.contains(QStringLiteral("星光落在海面上")));
}

void TstTagReader::readsMp4Tags()
{
    const QString path = fixturePath(QStringLiteral("library/m4a_aac.m4a"));
    const auto res = TagReader::read(path);
    QVERIFY(res.ok());

    const auto &val = res.value();
    QVERIFY(val.hasEmbeddedCover);

    QString title;
    QString artist;
    QString albumArtist;
    QString trackNumber;
    QString discNumber;
    QString genre;
    QString lyrics;

    for (const auto &t : val.tags) {
        if (t.key == QStringLiteral("TITLE")) {
            title = t.value;
        } else if (t.key == QStringLiteral("ARTIST")) {
            artist = t.value;
        } else if (t.key == QStringLiteral("ALBUMARTIST")) {
            albumArtist = t.value;
        } else if (t.key == QStringLiteral("TRACKNUMBER")) {
            trackNumber = t.value;
        } else if (t.key == QStringLiteral("DISCNUMBER")) {
            discNumber = t.value;
        } else if (t.key == QStringLiteral("GENRE")) {
            genre = t.value;
        } else if (t.key == QStringLiteral("LYRICS")) {
            lyrics = t.value;
        }
    }

    QCOMPARE(title, QStringLiteral("月光奏鸣"));
    QCOMPARE(artist, QStringLiteral("夜色乐团"));
    QCOMPARE(albumArtist, QStringLiteral("夜色乐团"));
    QCOMPARE(trackNumber, QStringLiteral("3/12"));
    QCOMPARE(discNumber, QStringLiteral("1/2"));
    QCOMPARE(genre, QStringLiteral("Classical"));
    QCOMPARE(lyrics, QStringLiteral("月色如水流淌"));
}

void TstTagReader::readsWavpackApe()
{
    const QString path = fixturePath(QStringLiteral("library/wavpack_ape.wv"));
    const auto res = TagReader::read(path);
    QVERIFY(res.ok());

    const auto &tags = res.value().tags;
    QString title;
    QString artist;
    QString album;
    QString date;
    QString track;
    QString tagType;
    int priority = -1;

    for (const auto &t : tags) {
        tagType = t.tagType;
        priority = t.priority;
        if (t.key == QStringLiteral("TITLE")) {
            title = t.value;
        } else if (t.key == QStringLiteral("ARTIST")) {
            artist = t.value;
        } else if (t.key == QStringLiteral("ALBUM")) {
            album = t.value;
        } else if (t.key == QStringLiteral("DATE")) {
            date = t.value;
        } else if (t.key == QStringLiteral("TRACKNUMBER")) {
            track = t.value;
        }
    }

    QCOMPARE(tagType, QStringLiteral("ape"));
    QCOMPARE(priority, 0);
    QCOMPARE(title, QStringLiteral("无损压缩之梦"));
    QCOMPARE(artist, QStringLiteral("音频极客"));
    QCOMPARE(album, QStringLiteral("极致保真"));
    QCOMPARE(date, QStringLiteral("2019"));
    QCOMPARE(track, QStringLiteral("5"));
}

void TstTagReader::readsWavId3()
{
    const QString path = fixturePath(QStringLiteral("library/wav_id3.wav"));
    const auto res = TagReader::read(path);
    QVERIFY(res.ok());

    const auto &tags = res.value().tags;
    QString title;
    QString artist;
    QString album;
    QString date;
    QString tagType;
    int priority = -1;

    for (const auto &t : tags) {
        tagType = t.tagType;
        priority = t.priority;
        if (t.key == QStringLiteral("TITLE")) {
            title = t.value;
        } else if (t.key == QStringLiteral("ARTIST")) {
            artist = t.value;
        } else if (t.key == QStringLiteral("ALBUM")) {
            album = t.value;
        } else if (t.key == QStringLiteral("DATE")) {
            date = t.value;
        }
    }

    QCOMPARE(tagType, QStringLiteral("id3v2"));
    QCOMPARE(priority, 0);
    QCOMPARE(title, QStringLiteral("声波漫游"));
    QCOMPARE(artist, QStringLiteral("脉冲乐队"));
    QCOMPARE(album, QStringLiteral("模拟时代"));
    QCOMPARE(date, QStringLiteral("2020-01-01"));
}

void TstTagReader::readsOggAndOpus()
{
    // OGG
    {
        const QString path = fixturePath(QStringLiteral("library/ogg_vorbis.ogg"));
        const auto res = TagReader::read(path);
        QVERIFY(res.ok());

        QString title;
        QString artist;
        for (const auto &t : res.value().tags) {
            if (t.key == QStringLiteral("TITLE")) {
                title = t.value;
            } else if (t.key == QStringLiteral("ARTIST")) {
                artist = t.value;
            }
        }
        QCOMPARE(title, QStringLiteral("森林的呼吸"));
        QCOMPARE(artist, QStringLiteral("风之子"));
    }

    // Opus
    {
        const QString path = fixturePath(QStringLiteral("library/opus.opus"));
        const auto res = TagReader::read(path);
        QVERIFY(res.ok());

        QString title;
        QString artist;
        for (const auto &t : res.value().tags) {
            if (t.key == QStringLiteral("TITLE")) {
                title = t.value;
            } else if (t.key == QStringLiteral("ARTIST")) {
                artist = t.value;
            }
        }
        QCOMPARE(title, QStringLiteral("数码回声"));
        QCOMPARE(artist, QStringLiteral("电波游侠"));
    }
}

void TstTagReader::noTagsFileHasEmptyTagList()
{
    const QString path = fixturePath(QStringLiteral("library/flac_no_tags.flac"));
    const auto res = TagReader::read(path);
    QVERIFY(res.ok());

    const auto &val = res.value();
    QVERIFY(val.tags.isEmpty());
    QVERIFY(!val.hasEmbeddedCover);
    QCOMPARE(val.audio.container, QStringLiteral("flac"));
    QCOMPARE(val.audio.codec, QStringLiteral("flac"));
}

void TstTagReader::readsNonAsciiPath()
{
    const QString path = fixturePath(QStringLiteral("library/中文 文件名.flac"));
    const auto res = TagReader::read(path);
    QVERIFY(res.ok());

    const auto &tags = res.value().tags;
    QString title;
    QString artist;
    QString album;

    for (const auto &t : tags) {
        if (t.key == QStringLiteral("TITLE")) {
            title = t.value;
        } else if (t.key == QStringLiteral("ARTIST")) {
            artist = t.value;
        } else if (t.key == QStringLiteral("ALBUM")) {
            album = t.value;
        }
    }

    QCOMPARE(title, QStringLiteral("さくら咲く頃"));
    QCOMPARE(artist, QStringLiteral("花吹雪"));
    QCOMPARE(album, QStringLiteral("春の歌"));
}

void TstTagReader::failsGracefully_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<bool>("expectOk");

    // 只剩前 300 字节的 mp3 仍含有效的 ID3/MPEG 帧头，TagLib 能读出（时长等不可信）；
    // 真正无法解码的情况由播放时的错误处理兜底
    QTest::newRow("corrupt_truncated") << QStringLiteral("library/corrupt_truncated.mp3") << true;
    QTest::newRow("corrupt_garbage") << QStringLiteral("library/corrupt_garbage.flac") << false;
    QTest::newRow("empty") << QStringLiteral("library/empty.mp3") << false;
    QTest::newRow("not_audio") << QStringLiteral("library/not_audio.ogg") << false;
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
        QVERIFY2(err.detail.contains(path),
            qPrintable(QStringLiteral("Error detail '%1' does not contain path '%2'")
                    .arg(err.detail, path)));
    }
}

void TstTagReader::concurrentReadsAreSafe()
{
    const QStringList sampleFiles = {
        QStringLiteral("library/mp3_id3v24_utf8.mp3"),
        QStringLiteral("library/mp3_id3v23_gbk.mp3"),
        QStringLiteral("library/mp3_id3v23_shiftjis.mp3"),
        QStringLiteral("library/mp3_id3v23_euckr.mp3"),
        QStringLiteral("library/mp3_id3v1_gbk.mp3"),
        QStringLiteral("library/mp3_id3v1_big5.mp3"),
        QStringLiteral("library/mp3_v1_and_v2.mp3"),
        QStringLiteral("library/flac_vorbis.flac"),
        QStringLiteral("library/flac_no_tags.flac"),
        QStringLiteral("library/ogg_vorbis.ogg"),
        QStringLiteral("library/opus.opus"),
        QStringLiteral("library/m4a_aac.m4a"),
        QStringLiteral("library/m4a_alac.m4a"),
        QStringLiteral("library/wav_id3.wav"),
        QStringLiteral("library/wavpack_ape.wv"),
        QStringLiteral("library/中文 文件名.flac"),
    };

    // Serial baseline results
    QList<QString> fullPaths;
    QList<TagReadResult> serialResults;
    for (const auto &rel : sampleFiles) {
        const QString p = fixturePath(rel);
        const auto res = TagReader::read(p);
        QVERIFY(res.ok());
        fullPaths.append(p);
        serialResults.append(res.value());
    }

    // Repeat 20 times concurrently
    QList<QString> taskList;
    taskList.reserve(fullPaths.size() * 20);
    for (int rep = 0; rep < 20; ++rep) {
        for (const auto &p : fullPaths) {
            taskList.append(p);
        }
    }

    const QList<linernotes::core::Result<TagReadResult>> concurrentResults
        = QtConcurrent::blockingMapped(
            taskList, [](const QString &p) { return TagReader::read(p); });

    QCOMPARE(concurrentResults.size(), taskList.size());
    for (qsizetype i = 0; i < concurrentResults.size(); ++i) {
        QVERIFY(concurrentResults.at(i).ok());
        const qsizetype baselineIdx = i % sampleFiles.size();
        const auto &actual = concurrentResults.at(i).value();
        const auto &expected = serialResults.at(baselineIdx);

        QCOMPARE(actual.audio.container, expected.audio.container);
        QCOMPARE(actual.audio.codec, expected.audio.codec);
        QCOMPARE(actual.audio.sampleRate, expected.audio.sampleRate);
        QCOMPARE(actual.audio.channels, expected.audio.channels);
        QCOMPARE(actual.audio.bitDepth, expected.audio.bitDepth);
        QCOMPARE(actual.hasEmbeddedCover, expected.hasEmbeddedCover);
        QCOMPARE(actual.tags.size(), expected.tags.size());
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstTagReader)

#include "tst_TagReader.moc"
