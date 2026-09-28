// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QByteArray>
#include <QList>
#include <QString>
#include <QTest>

#include <butler/Mojibake.h>
#include <butler/MojibakeAnalysis.h>
#include <common/TestSupport.h>
#include <library/CorrectionStore.h>
#include <library/LibraryEnums.h>
#include <unicode/ucnv.h>
#include <unicode/utypes.h>

#include <memory>
#include <optional>
#include <vector>

namespace {

using linernotes::butler::analyzeGroup;
using linernotes::butler::FilenameGuess;
using linernotes::butler::guessAlbumFromDirectory;
using linernotes::butler::guessFromPath;
using linernotes::butler::MojibakeField;
using linernotes::butler::MojibakeGroup;
using linernotes::butler::MojibakeTrack;
using linernotes::butler::SourceEncoding;
using linernotes::library::CorrectionSource;
using linernotes::library::TagField;

struct UConverterDeleter {
    void operator()(UConverter *cnv) const
    {
        if (cnv != nullptr) {
            ucnv_close(cnv);
        }
    }
};

using UConverterPtr = std::unique_ptr<UConverter, UConverterDeleter>;

std::optional<QByteArray> encodeWithIcu(const QString &text, const char *converterName)
{
    UErrorCode status = U_ZERO_ERROR;
    const UConverterPtr cnv(ucnv_open(converterName, &status));
    if (U_FAILURE(status) != 0 || cnv == nullptr) {
        return std::nullopt;
    }

    ucnv_setFromUCallBack(cnv.get(), UCNV_FROM_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &status);
    if (U_FAILURE(status) != 0) {
        return std::nullopt;
    }

    std::vector<UChar> u16(static_cast<size_t>(text.size()));
    for (qsizetype i = 0; i < text.size(); ++i) {
        u16.at(static_cast<size_t>(i)) = static_cast<UChar>(text.at(i).unicode());
    }

    const auto destCap = static_cast<int32_t>((text.size() * 4) + 16);
    QByteArray result;
    result.resize(destCap);

    const int32_t len = ucnv_fromUChars(
        cnv.get(), result.data(), destCap, u16.data(), static_cast<int32_t>(u16.size()), &status);
    if (U_FAILURE(status) != 0 || len < 0) {
        return std::nullopt;
    }

    result.resize(len);
    return result;
}

class TstMojibakeAnalysis : public QObject {
    Q_OBJECT

private slots:
    void guessFromPathCase_data();
    void guessFromPathCase();
    void guessAlbumFromDirectoryTest();
    void analyzeProducesRuleProposals();
    void irreparableUsesFilename();
};

void TstMojibakeAnalysis::guessFromPathCase_data()
{
    QTest::addColumn<QString>("filePath");
    QTest::addColumn<QString>("expectedTitle");
    QTest::addColumn<QString>("expectedArtist");
    QTest::addColumn<bool>("hasTrackNumber");
    QTest::addColumn<int>("expectedTrackNumber");

    QTest::newRow("NN - Artist - Title")
        << QStringLiteral("/music/03 - 林晓风 - 晚风里的歌.mp3") << QStringLiteral("晚风里的歌")
        << QStringLiteral("林晓风") << true << 3;

    QTest::newRow("NN. Title") << QStringLiteral("/music/01. 晚风里的歌.flac")
                               << QStringLiteral("晚风里的歌") << QString() << true << 1;

    QTest::newRow("NN Title") << QStringLiteral("/music/02 晚风里的歌.mp3")
                              << QStringLiteral("晚风里的歌") << QString() << true << 2;

    QTest::newRow("NN-Title") << QStringLiteral("/music/04-晚风里的歌.ogg")
                              << QStringLiteral("晚风里的歌") << QString() << true << 4;

    QTest::newRow("Artist - Title")
        << QStringLiteral("/music/林晓风 - 晚风里的歌.mp3") << QStringLiteral("晚风里的歌")
        << QStringLiteral("林晓风") << false << 0;

    QTest::newRow("Title") << QStringLiteral("/music/晚风里的歌.wav")
                           << QStringLiteral("晚风里的歌") << QString() << false << 0;

    QTest::newRow("Unparseable") << QStringLiteral("/music/.mp3") << QString() << QString() << false
                                 << 0;
}

void TstMojibakeAnalysis::guessFromPathCase()
{
    QFETCH(QString, filePath);
    QFETCH(QString, expectedTitle);
    QFETCH(QString, expectedArtist);
    QFETCH(bool, hasTrackNumber);
    QFETCH(int, expectedTrackNumber);

    const FilenameGuess guess = guessFromPath(filePath);
    QCOMPARE(guess.title, expectedTitle);
    QCOMPARE(guess.artist, expectedArtist);
    QCOMPARE(guess.trackNumber.has_value(), hasTrackNumber);
    if (hasTrackNumber && guess.trackNumber.has_value()) {
        QCOMPARE(*guess.trackNumber, expectedTrackNumber);
    }
}

void TstMojibakeAnalysis::guessAlbumFromDirectoryTest()
{
    // 3 种年份前缀
    QCOMPARE(
        guessAlbumFromDirectory(QStringLiteral("/music/2023 - 专辑名")), QStringLiteral("专辑名"));
    QCOMPARE(
        guessAlbumFromDirectory(QStringLiteral("/music/[2023] 专辑名")), QStringLiteral("专辑名"));
    QCOMPARE(
        guessAlbumFromDirectory(QStringLiteral("/music/(2023) 专辑名")), QStringLiteral("专辑名"));

    // Artist - Album
    QCOMPARE(guessAlbumFromDirectory(QStringLiteral("/music/周杰伦 - 范特西")),
        QStringLiteral("范特西"));

    // 普通名
    QCOMPARE(
        guessAlbumFromDirectory(QStringLiteral("/music/普通专辑名")), QStringLiteral("普通专辑名"));
}

void TstMojibakeAnalysis::analyzeProducesRuleProposals()
{
    const QString origTitle = QStringLiteral("晚风里的歌");
    const QString origArtist = QStringLiteral("林晓风");
    const QString origAlbum = QStringLiteral("山谷的回响");

    const auto rawTitleOpt = encodeWithIcu(origTitle, "windows-936");
    const auto rawArtistOpt = encodeWithIcu(origArtist, "windows-936");
    const auto rawAlbumOpt = encodeWithIcu(origAlbum, "windows-936");

    QVERIFY(rawTitleOpt.has_value() && rawArtistOpt.has_value() && rawAlbumOpt.has_value());
    if (!rawTitleOpt.has_value() || !rawArtistOpt.has_value() || !rawAlbumOpt.has_value()) {
        return;
    }

    MojibakeTrack track1;
    track1.trackId = 101;
    track1.filePath = QStringLiteral("/music/01.mp3");
    track1.suspects = {
        MojibakeField {
            .field = TagField::Title,
            .value = QString::fromLatin1(*rawTitleOpt),
            .rawBytes = rawTitleOpt,
        },
    };

    MojibakeTrack track2;
    track2.trackId = 102;
    track2.filePath = QStringLiteral("/music/02.mp3");
    track2.suspects = {
        MojibakeField {
            .field = TagField::Artist,
            .value = QString::fromLatin1(*rawArtistOpt),
            .rawBytes = rawArtistOpt,
        },
    };

    MojibakeTrack track3;
    track3.trackId = 103;
    track3.filePath = QStringLiteral("/music/03.mp3");
    track3.suspects = {
        MojibakeField {
            .field = TagField::Album,
            .value = QString::fromLatin1(*rawAlbumOpt),
            .rawBytes = rawAlbumOpt,
        },
    };

    MojibakeGroup group;
    group.key = QStringLiteral("{\"album\":\"\",\"dir\":\"/music\"}");
    group.directory = QStringLiteral("/music");
    group.tracks = { track1, track2, track3 };

    const auto analysis = analyzeGroup(group);

    QVERIFY(analysis.encoding.has_value());
    if (analysis.encoding.has_value()) {
        QCOMPARE(*analysis.encoding, SourceEncoding::Gbk);
    }
    QVERIFY(analysis.ambiguous.isEmpty());
    QCOMPARE(analysis.proposals.size(), 3);

    QCOMPARE(analysis.proposals.at(0).field, TagField::Title);
    QCOMPARE(analysis.proposals.at(0).newValue, origTitle);
    QCOMPARE(analysis.proposals.at(0).source, CorrectionSource::Rule);
    QCOMPARE(analysis.proposals.at(0).reason,
        QStringLiteral("Re-decoded as GBK (decided across the album)"));
    QVERIFY(analysis.proposals.at(0).confidence >= 0.6);

    QCOMPARE(analysis.proposals.at(1).field, TagField::Artist);
    QCOMPARE(analysis.proposals.at(1).newValue, origArtist);
    QCOMPARE(analysis.proposals.at(1).source, CorrectionSource::Rule);
    QCOMPARE(analysis.proposals.at(1).reason,
        QStringLiteral("Re-decoded as GBK (decided across the album)"));

    QCOMPARE(analysis.proposals.at(2).field, TagField::Album);
    QCOMPARE(analysis.proposals.at(2).newValue, origAlbum);
    QCOMPARE(analysis.proposals.at(2).source, CorrectionSource::Rule);
    QCOMPARE(analysis.proposals.at(2).reason,
        QStringLiteral("Re-decoded as GBK (decided across the album)"));
}

void TstMojibakeAnalysis::irreparableUsesFilename()
{
    MojibakeTrack track;
    track.trackId = 201;
    track.filePath = QStringLiteral("/music/03 - 某艺人 - 某标题.mp3");
    track.suspects = {
        MojibakeField {
            .field = TagField::Title,
            .value = QStringLiteral("????"),
            .rawBytes = std::nullopt,
        },
    };

    MojibakeGroup group;
    group.key = QStringLiteral("{\"album\":\"\",\"dir\":\"/music\"}");
    group.directory = QStringLiteral("/music");
    group.tracks = { track };

    const auto analysis = analyzeGroup(group);

    QCOMPARE(analysis.irreparable.size(), 1);
    QCOMPARE(analysis.irreparable.at(0).first, 201);
    QCOMPARE(analysis.irreparable.at(0).second, TagField::Title);

    QCOMPARE(analysis.proposals.size(), 1);
    QCOMPARE(analysis.proposals.at(0).field, TagField::Title);
    QCOMPARE(analysis.proposals.at(0).newValue, QStringLiteral("某标题"));
    QCOMPARE(analysis.proposals.at(0).confidence, 0.5);
    QCOMPARE(analysis.proposals.at(0).source, CorrectionSource::Rule);
    QCOMPARE(analysis.proposals.at(0).reason, QStringLiteral("Guessed from file name"));
}

} // namespace

QTEST_GUILESS_MAIN(TstMojibakeAnalysis)

#include "tst_MojibakeAnalysis.moc"
