// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include <audio/AudioDecoder.h>
#include <common/TestSupport.h>
#include <core/Result.h>
#include <library/Errors.h>
#include <library/LibraryEnums.h>
#include <library/TagReader.h>
#include <library/TagWriter.h>

#include <optional>

namespace {

using linernotes::library::TagField;
using linernotes::library::TagReader;
using linernotes::library::TagSnapshot;
using linernotes::library::TagWriter;
using linernotes::test::fixturePath;
namespace errc = linernotes::library::errc;

QString copyFixtureToTemp(const QString &relPath, const QTemporaryDir &tempDir)
{
    const QString src = fixturePath(relPath);
    QString dest = tempDir.filePath(QFileInfo(relPath).fileName());
    if (QFile::exists(dest)) {
        QFile::remove(dest);
    }
    if (!QFile::copy(src, dest)) {
        return { };
    }
    return dest;
}

class TstTagWriter : public QObject {
    Q_OBJECT

private slots:
    void roundtripWriteAndRestore_data();
    void roundtripWriteAndRestore();
    void audioUnchangedAfterRoundtrip_data();
    void audioUnchangedAfterRoundtrip();
    void embeddedPictureUnchanged_data();
    void embeddedPictureUnchanged();
    void keepsId3v23GbkBytes();
    void modifyOnlyTrackTotal();
    void unsupportedFormatsFail();
    void jsonSerialization();
};

void TstTagWriter::roundtripWriteAndRestore_data()
{
    QTest::addColumn<QString>("fixtureRelPath");

    QTest::newRow("flac_vorbis") << QStringLiteral("library/flac_vorbis.flac");
    QTest::newRow("mp3_id3v24_utf8") << QStringLiteral("library/mp3_id3v24_utf8.mp3");
    QTest::newRow("mp3_id3v23_gbk") << QStringLiteral("library/mp3_id3v23_gbk.mp3");
    QTest::newRow("mp3_v1_and_v2") << QStringLiteral("library/mp3_v1_and_v2.mp3");
    QTest::newRow("m4a_alac") << QStringLiteral("library/m4a_alac.m4a");
    QTest::newRow("cover_1600_embed_flac") << QStringLiteral("library/cover_1600_embed.flac");
    QTest::newRow("cover_1600_embed_mp3") << QStringLiteral("library/cover_1600_embed.mp3");
    QTest::newRow("melody_8s_ogg") << QStringLiteral("audio/melody_8s.ogg");
}

void TstTagWriter::roundtripWriteAndRestore()
{
    QFETCH(QString, fixtureRelPath);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString tempPath = copyFixtureToTemp(fixtureRelPath, tempDir);
    QVERIFY(!tempPath.isEmpty());

    // 1. Take original snapshot
    const auto snapRes = TagWriter::snapshot(tempPath);
    QVERIFY(snapRes.ok());
    const TagSnapshot &origSnapshot = snapRes.value();

    // 2. Write fields
    const QHash<TagField, QString> values = {
        { TagField::Title, QStringLiteral("写回测试") },
        { TagField::Album, QStringLiteral("新专辑") },
        { TagField::Year, QStringLiteral("2016") },
        { TagField::TrackNumber, QStringLiteral("3") },
        { TagField::TrackTotal, QStringLiteral("12") },
    };
    const auto writeRes = TagWriter::writeFields(tempPath, values);
    QVERIFY(writeRes.ok());

    // 3. Read back with TagReader
    const auto readRes = TagReader::read(tempPath);
    QVERIFY(readRes.ok());
    const auto &readTags = readRes.value().tags;

    auto getTagValue = [&](const QString &key) -> QString {
        for (const auto &t : readTags) {
            if (t.key == key) {
                return t.value;
            }
        }
        return { };
    };

    QCOMPARE(getTagValue(QStringLiteral("TITLE")), QStringLiteral("写回测试"));
    QCOMPARE(getTagValue(QStringLiteral("ALBUM")), QStringLiteral("新专辑"));
    QCOMPARE(getTagValue(QStringLiteral("DATE")), QStringLiteral("2016"));

    if (tempPath.endsWith(QLatin1StringView(".flac"), Qt::CaseInsensitive)
        || tempPath.endsWith(QLatin1StringView(".ogg"), Qt::CaseInsensitive)) {
        QCOMPARE(getTagValue(QStringLiteral("TRACKNUMBER")), QStringLiteral("3"));
        QCOMPARE(getTagValue(QStringLiteral("TRACKTOTAL")), QStringLiteral("12"));
    } else {
        QCOMPARE(getTagValue(QStringLiteral("TRACKNUMBER")), QStringLiteral("3/12"));
    }

    // 4. Restore original snapshot
    const auto restoreRes = TagWriter::restore(tempPath, origSnapshot);
    QVERIFY(restoreRes.ok());

    // 5. Verify snapshot after restore equals original snapshot
    const auto postSnapRes = TagWriter::snapshot(tempPath);
    QVERIFY(postSnapRes.ok());
    QCOMPARE(postSnapRes.value(), origSnapshot);
}

void TstTagWriter::audioUnchangedAfterRoundtrip_data()
{
    QTest::addColumn<QString>("fixtureRelPath");

    QTest::newRow("melody_8s_ogg") << QStringLiteral("audio/melody_8s.ogg");
    QTest::newRow("flac_vorbis") << QStringLiteral("library/flac_vorbis.flac");
}

void TstTagWriter::audioUnchangedAfterRoundtrip()
{
    QFETCH(QString, fixtureRelPath);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString tempPath = copyFixtureToTemp(fixtureRelPath, tempDir);
    QVERIFY(!tempPath.isEmpty());

    const linernotes::audio::DecodeOptions opts { .maxDurationMs = 5000 };
    const auto decBefore = linernotes::audio::AudioDecoder::decode(tempPath, opts);
    QVERIFY(decBefore.ok());

    const auto snapRes = TagWriter::snapshot(tempPath);
    QVERIFY(snapRes.ok());

    const QHash<TagField, QString> values = {
        { TagField::Title, QStringLiteral("修改标题音频测试") },
        { TagField::Artist, QStringLiteral("新艺人") },
        { TagField::Album, QStringLiteral("新专辑") },
    };
    QVERIFY(TagWriter::writeFields(tempPath, values).ok());
    QVERIFY(TagWriter::restore(tempPath, snapRes.value()).ok());

    const auto decAfter = linernotes::audio::AudioDecoder::decode(tempPath, opts);
    QVERIFY(decAfter.ok());
    QCOMPARE(decAfter.value().samples, decBefore.value().samples);
    QCOMPARE(decAfter.value().sampleRate, decBefore.value().sampleRate);
    QCOMPARE(decAfter.value().channels, decBefore.value().channels);
}

void TstTagWriter::embeddedPictureUnchanged_data()
{
    QTest::addColumn<QString>("fixtureRelPath");

    QTest::newRow("cover_1600_embed_flac") << QStringLiteral("library/cover_1600_embed.flac");
    QTest::newRow("cover_1600_embed_mp3") << QStringLiteral("library/cover_1600_embed.mp3");
}

void TstTagWriter::embeddedPictureUnchanged()
{
    QFETCH(QString, fixtureRelPath);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString tempPath = copyFixtureToTemp(fixtureRelPath, tempDir);
    QVERIFY(!tempPath.isEmpty());

    const auto readBefore = TagReader::read(tempPath);
    QVERIFY(readBefore.ok());
    QVERIFY(readBefore.value().hasEmbeddedCover);
    QVERIFY(readBefore.value().frontCover.has_value());
    if (!readBefore.value().frontCover.has_value()) {
        return;
    }
    const auto origCover = *readBefore.value().frontCover;

    const auto snapRes = TagWriter::snapshot(tempPath);
    QVERIFY(snapRes.ok());

    const QHash<TagField, QString> values = {
        { TagField::Title, QStringLiteral("图片测试标题") },
        { TagField::Album, QStringLiteral("图片测试专辑") },
    };
    QVERIFY(TagWriter::writeFields(tempPath, values).ok());
    QVERIFY(TagWriter::restore(tempPath, snapRes.value()).ok());

    const auto readAfter = TagReader::read(tempPath);
    QVERIFY(readAfter.ok());
    QVERIFY(readAfter.value().hasEmbeddedCover);
    QVERIFY(readAfter.value().frontCover.has_value());
    if (!readAfter.value().frontCover.has_value()) {
        return;
    }
    const auto afterCover = *readAfter.value().frontCover;
    QCOMPARE(afterCover.data, origCover.data);
    QCOMPARE(afterCover.mimeType, origCover.mimeType);
}

void TstTagWriter::keepsId3v23GbkBytes()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString tempPath
        = copyFixtureToTemp(QStringLiteral("library/mp3_id3v23_gbk.mp3"), tempDir);
    QVERIFY(!tempPath.isEmpty());

    const auto snapRes = TagWriter::snapshot(tempPath);
    QVERIFY(snapRes.ok());
    const TagSnapshot &origSnap = snapRes.value();
    QVERIFY(!origSnap.blocks.isEmpty());
    QCOMPARE(origSnap.blocks.front().type, QStringLiteral("id3v2"));
    QCOMPARE(origSnap.blocks.front().id3v2Version, 3);
    const QString origTitle
        = origSnap.blocks.front().properties.value(QStringLiteral("TITLE")).front();

    const QHash<TagField, QString> values = {
        { TagField::Title, QStringLiteral("临时修改标题") },
    };
    QVERIFY(TagWriter::writeFields(tempPath, values).ok());
    QVERIFY(TagWriter::restore(tempPath, origSnap).ok());

    const auto postSnapRes = TagWriter::snapshot(tempPath);
    QVERIFY(postSnapRes.ok());
    const TagSnapshot &postSnap = postSnapRes.value();
    QCOMPARE(postSnap.blocks.front().id3v2Version, 3);
    QCOMPARE(postSnap.blocks.front().properties.value(QStringLiteral("TITLE")).front(), origTitle);
    QCOMPARE(postSnap, origSnap);
}

void TstTagWriter::modifyOnlyTrackTotal()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString tempPath
        = copyFixtureToTemp(QStringLiteral("library/mp3_id3v24_utf8.mp3"), tempDir);
    QVERIFY(!tempPath.isEmpty());

    // First write TrackNumber = "5"
    const QHash<TagField, QString> initValues = {
        { TagField::TrackNumber, QStringLiteral("5") },
    };
    QVERIFY(TagWriter::writeFields(tempPath, initValues).ok());

    // Modify ONLY TrackTotal = "10"
    const QHash<TagField, QString> totalOnly = {
        { TagField::TrackTotal, QStringLiteral("10") },
    };
    QVERIFY(TagWriter::writeFields(tempPath, totalOnly).ok());

    // TagReader should read back "5/10"
    const auto readRes = TagReader::read(tempPath);
    QVERIFY(readRes.ok());
    QString trackNum;
    for (const auto &t : readRes.value().tags) {
        if (t.key == QStringLiteral("TRACKNUMBER")) {
            trackNum = t.value;
            break;
        }
    }
    QCOMPARE(trackNum, QStringLiteral("5/10"));
}

void TstTagWriter::unsupportedFormatsFail()
{
    // 1. WAV file (supported for reading by TagLib, but unsupported for TagWriter)
    const QString wavPath = fixturePath(QStringLiteral("library/wav_id3.wav"));
    QVERIFY(!TagWriter::isSupported(wavPath));

    const QHash<TagField, QString> values = {
        { TagField::Title, QStringLiteral("WAV Title") },
    };
    const auto writeWavRes = TagWriter::writeFields(wavPath, values);
    QVERIFY(!writeWavRes.ok());
    QCOMPARE(writeWavRes.error().code, errc::kTagWriteUnsupported);

    const auto snapWavRes = TagWriter::snapshot(wavPath);
    QVERIFY(!snapWavRes.ok());
    QCOMPARE(snapWavRes.error().code, errc::kTagWriteUnsupported);

    // 2. Text file
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString txtPath = tempDir.filePath(QStringLiteral("sample.txt"));
    QFile txtFile(txtPath);
    QVERIFY(txtFile.open(QIODevice::WriteOnly));
    txtFile.write("Hello Text File");
    txtFile.close();

    QVERIFY(!TagWriter::isSupported(txtPath));
    const auto writeTxtRes = TagWriter::writeFields(txtPath, values);
    QVERIFY(!writeTxtRes.ok());
    QCOMPARE(writeTxtRes.error().code, errc::kTagWriteUnsupported);
}

void TstTagWriter::jsonSerialization()
{
    TagSnapshot orig;
    TagSnapshot::Block b1;
    b1.type = QStringLiteral("id3v2");
    b1.id3v2Version = 3;
    b1.properties.insert(QStringLiteral("TITLE"), { QStringLiteral("测试标题") });
    b1.properties.insert(
        QStringLiteral("ARTIST"), { QStringLiteral("歌手1"), QStringLiteral("歌手2") });
    b1.unsupported = { QStringLiteral("TXXX/UNSUPPORTED") };
    orig.blocks.append(b1);

    TagSnapshot::Block b2;
    b2.type = QStringLiteral("id3v1");
    b2.id3v2Version = 0;
    b2.properties.insert(QStringLiteral("TITLE"), { QStringLiteral("V1 Title") });
    orig.blocks.append(b2);

    const QJsonObject json = orig.toJson();
    const auto parsed = TagSnapshot::fromJson(json);
    QVERIFY(parsed.has_value());
    if (!parsed.has_value()) {
        return;
    }
    QCOMPARE(*parsed, orig);

    // Invalid JSON returns nullopt
    const auto invalidParsed = TagSnapshot::fromJson(QJsonObject());
    QCOMPARE(invalidParsed, std::nullopt);
}

} // namespace

QTEST_GUILESS_MAIN(TstTagWriter)

#include "tst_TagWriter.moc"
