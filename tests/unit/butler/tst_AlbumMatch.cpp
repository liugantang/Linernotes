// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

#include <butler/AlbumMatch.h>
#include <butler/MusicBrainz.h>
#include <common/TestSupport.h>

#include <cmath>
#include <optional>
#include <utility>

namespace {

using linernotes::butler::decide;
using linernotes::butler::durationScore;
using linernotes::butler::kMinAlbumScore;
using linernotes::butler::kMinPairScore;
using linernotes::butler::LocalAlbum;
using linernotes::butler::LocalTrack;
using linernotes::butler::MbRelease;
using linernotes::butler::parseRelease;
using linernotes::butler::scoreRelease;
using linernotes::butler::titleSimilarity;

QByteArray readFixture(const QString &relativePath)
{
    const QString fullPath = linernotes::test::fixturePath(relativePath);
    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    return file.readAll();
}

class TstAlbumMatch : public QObject {
    Q_OBJECT

private slots:
    void testTitleSimilarity();
    void testDurationScore();
    void testFlowerflowerFullMatch();
    void testFlowerflowerPartialAlbum();
    void testFlowerflowerUnrelatedTracks();
    void testDecideEquivalentAndAmbiguous();
    void testKalafina3CdMatch();
    void testEmptyInputs();
};

void TstAlbumMatch::testTitleSimilarity()
{
    // 相同 → 1
    QCOMPARE(titleSimilarity(QStringLiteral("Song"), QStringLiteral("Song")), 1.0);

    // 大小写差异 → 1
    QCOMPARE(titleSimilarity(QStringLiteral("GALNERYUS"), QStringLiteral("galneryus")), 1.0);

    // 全半角差异 → 1
    QCOMPARE(titleSimilarity(QStringLiteral("Ｈｅｌｌｏ"), QStringLiteral("Hello")), 1.0);

    // 去掉圆括号内容 → 1
    QCOMPARE(
        titleSimilarity(QStringLiteral("宝物 (Live at Shibuya WWW)"), QStringLiteral("宝物")), 1.0);
    QCOMPARE(titleSimilarity(QStringLiteral("宝物（Live at Shibuya WWW）"), QStringLiteral("宝物")),
        1.0);

    // 去掉方括号与六角括号内容 → 1
    QCOMPARE(titleSimilarity(QStringLiteral("Song [Remaster]"), QStringLiteral("Song")), 1.0);
    QCOMPARE(titleSimilarity(QStringLiteral("Song［2020 Remaster］"), QStringLiteral("Song")), 1.0);
    QCOMPARE(titleSimilarity(QStringLiteral("Song【Official】"), QStringLiteral("Song")), 1.0);

    // 「」『』 内通常是正文，不去掉（标点在 exactKey 中已去掉）
    QCOMPARE(
        titleSimilarity(QStringLiteral("「おうちに帰りたい」"), QStringLiteral("おうちに帰りたい")),
        1.0);

    // 完全不同 → < 0.3
    QVERIFY(titleSimilarity(QStringLiteral("Hello"), QStringLiteral("Goodbye")) < 0.3);

    // 两者都为空返回 0
    QCOMPARE(titleSimilarity(QString(), QString()), 0.0);
    QCOMPARE(titleSimilarity(QStringLiteral("   "), QStringLiteral("")), 0.0);
}

void TstAlbumMatch::testDurationScore()
{
    // 差 1 s (≤ 2 s) → 1
    QCOMPARE(durationScore(200000, 201000), 1.0);
    QCOMPARE(durationScore(200000, 199000), 1.0);

    // 差 2 s → 1
    QCOMPARE(durationScore(200000, 202000), 1.0);

    // 差 8.5 s → 约 0.5
    const double mid = durationScore(200000, 208500);
    QVERIFY(std::abs(mid - 0.5) < 1e-6);

    // 差 15 s (≥ 15 s) → 0
    QCOMPARE(durationScore(200000, 215000), 0.0);

    // 差 20 s → 0
    QCOMPARE(durationScore(200000, 220000), 0.0);

    // 任一为 0 → 0.5
    QCOMPARE(durationScore(0, 200000), 0.5);
    QCOMPARE(durationScore(200000, 0), 0.5);
    QCOMPARE(durationScore(0, 0), 0.5);
}

void TstAlbumMatch::testFlowerflowerFullMatch()
{
    const QByteArray json
        = readFixture(QStringLiteral("musicbrainz/release_flowerflower_takaramono.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseRelease(json);
    QVERIFY(res.ok());
    const auto &release = res.value();
    QCOMPARE(release.tracks.size(), 5);

    // 本地 5 首标题与时长（±1 s）都一致、无编号
    LocalAlbum local {
        .title = QStringLiteral("宝物"),
        .tracks = {
            LocalTrack {
                .trackId = 1,
                .title = QStringLiteral("宝物"),
                .durationMs = 293000,
                .discNumber = std::nullopt,
                .trackNumber = std::nullopt,
            },
            LocalTrack {
                .trackId = 2,
                .title = QStringLiteral("炎"),
                .durationMs = 308000,
                .discNumber = std::nullopt,
                .trackNumber = std::nullopt,
            },
            LocalTrack {
                .trackId = 3,
                .title = QStringLiteral("とうめいなうた (Live at Shibuya WWW)"),
                .durationMs = 329000,
                .discNumber = std::nullopt,
                .trackNumber = std::nullopt,
            },
            LocalTrack {
                .trackId = 4,
                .title = QStringLiteral("宝物 (Live at Shibuya WWW)"),
                .durationMs = 297000,
                .discNumber = std::nullopt,
                .trackNumber = std::nullopt,
            },
            LocalTrack {
                .trackId = 5,
                .title = QStringLiteral("スタートライン (Live at Shibuya WWW)"),
                .durationMs = 341000,
                .discNumber = std::nullopt,
                .trackNumber = std::nullopt,
            },
        },
    };

    const auto match = scoreRelease(local, release);
    QVERIFY(match.score > 0.95);
    QCOMPARE(match.mapping.size(), 5);
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(match.mapping.at(i).trackId, static_cast<qint64>(i + 1));
        QCOMPARE(match.mapping.at(i).mbIndex, i);
        QVERIFY(match.mapping.at(i).score >= kMinPairScore);
    }
}

void TstAlbumMatch::testFlowerflowerPartialAlbum()
{
    const QByteArray json
        = readFixture(QStringLiteral("musicbrainz/release_flowerflower_takaramono.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseRelease(json);
    QVERIFY(res.ok());
    const auto &release = res.value();

    // 本地只有前 2 首
    LocalAlbum local {
        .title = QStringLiteral("宝物"),
        .tracks = {
            LocalTrack {
                .trackId = 1,
                .title = QStringLiteral("宝物"),
                .durationMs = 292000,
                .discNumber = std::nullopt,
                .trackNumber = std::nullopt,
            },
            LocalTrack {
                .trackId = 2,
                .title = QStringLiteral("炎"),
                .durationMs = 309000,
                .discNumber = std::nullopt,
                .trackNumber = std::nullopt,
            },
        },
    };

    const auto match = scoreRelease(local, release);
    // partial album: 仍通过阈值
    QVERIFY(match.score >= kMinAlbumScore);
    QCOMPARE(match.mapping.size(), 2);
    QCOMPARE(match.mapping.at(0).trackId, 1);
    QCOMPARE(match.mapping.at(0).mbIndex, 0);
    QCOMPARE(match.mapping.at(1).trackId, 2);
    QCOMPARE(match.mapping.at(1).mbIndex, 1);
}

void TstAlbumMatch::testFlowerflowerUnrelatedTracks()
{
    const QByteArray json
        = readFixture(QStringLiteral("musicbrainz/release_flowerflower_takaramono.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseRelease(json);
    QVERIFY(res.ok());
    const auto &release = res.value();

    // 本地 5 首标题全不相关、时长也差很多
    LocalAlbum local {
        .title = QStringLiteral("Some Album"),
        .tracks = {
            LocalTrack {
                .trackId = 1,
                .title = QStringLiteral("Totally Unrelated Track 1"),
                .durationMs = 50000,
            },
            LocalTrack {
                .trackId = 2,
                .title = QStringLiteral("Totally Unrelated Track 2"),
                .durationMs = 60000,
            },
            LocalTrack {
                .trackId = 3,
                .title = QStringLiteral("Totally Unrelated Track 3"),
                .durationMs = 70000,
            },
            LocalTrack {
                .trackId = 4,
                .title = QStringLiteral("Totally Unrelated Track 4"),
                .durationMs = 80000,
            },
            LocalTrack {
                .trackId = 5,
                .title = QStringLiteral("Totally Unrelated Track 5"),
                .durationMs = 90000,
            },
        },
    };

    const auto match = scoreRelease(local, release);
    QVERIFY(match.score < kMinAlbumScore);

    const auto decision = decide(local, { release });
    QVERIFY(!decision.best.has_value());
    QVERIFY(!decision.ambiguous);
}

void TstAlbumMatch::testDecideEquivalentAndAmbiguous()
{
    const QByteArray json
        = readFixture(QStringLiteral("musicbrainz/release_flowerflower_takaramono.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseRelease(json);
    QVERIFY(res.ok());
    const auto &release1 = res.value();

    LocalAlbum local {
        .title = QStringLiteral("宝物"),
        .tracks = {
            LocalTrack {
                .trackId = 1,
                .title = QStringLiteral("宝物"),
                .durationMs = 292000,
            },
            LocalTrack {
                .trackId = 2,
                .title = QStringLiteral("炎"),
                .durationMs = 309000,
            },
            LocalTrack {
                .trackId = 3,
                .title = QStringLiteral("とうめいなうた (Live at Shibuya WWW)"),
                .durationMs = 328000,
            },
            LocalTrack {
                .trackId = 4,
                .title = QStringLiteral("宝物 (Live at Shibuya WWW)"),
                .durationMs = 298000,
            },
            LocalTrack {
                .trackId = 5,
                .title = QStringLiteral("スタートライン (Live at Shibuya WWW)"),
                .durationMs = 340000,
            },
        },
    };

    // 1. 同一个 release 复制一份改 id（等价）→ 不 ambiguous
    auto release2 = release1;
    release2.id = QStringLiteral("22222222-6831-4169-830b-baf5b827878e");

    const auto decEquivalent = decide(local, { release1, release2 });
    QVERIFY(decEquivalent.best.has_value());
    QVERIFY(!decEquivalent.ambiguous);

    // 2. 年份不同（分数相同但年份不同）→ ambiguous
    auto releaseDiffYear = release1;
    releaseDiffYear.id = QStringLiteral("33333333-6831-4169-830b-baf5b827878e");
    releaseDiffYear.date = QStringLiteral("2017-01-01");
    releaseDiffYear.originalDate = QStringLiteral("2017-01-01");

    const auto decDiffYear = decide(local, { release1, releaseDiffYear });
    QVERIFY(decDiffYear.best.has_value());
    QVERIFY(decDiffYear.ambiguous);

    // 3. 只互换第 1、2 首的 position（标题、时长不动）→ 映射到的 position 不同 → ambiguous
    auto releaseDiffPos = release1;
    releaseDiffPos.id = QStringLiteral("44444444-6831-4169-830b-baf5b827878e");
    QVERIFY(releaseDiffPos.tracks.size() >= 2);
    auto &track0 = releaseDiffPos.tracks.first();
    auto &track1 = *std::next(releaseDiffPos.tracks.begin());
    std::swap(track0.position, track1.position);

    const auto decDiffPos = decide(local, { release1, releaseDiffPos });
    QVERIFY(decDiffPos.best.has_value());
    QVERIFY(decDiffPos.ambiguous);
}

void TstAlbumMatch::testKalafina3CdMatch()
{
    const QByteArray json
        = readFixture(QStringLiteral("musicbrainz/release_kalafina_best_3cd.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseRelease(json);
    QVERIFY(res.ok());
    const auto &release = res.value();
    QCOMPARE(release.discCount, 3);
    QCOMPARE(release.tracks.size(), 36);

    // 本地 3 首分别来自第 1、2、3 张碟（标题、时长一致，本地 disc/track 编号与 MB 一致）
    LocalAlbum local {
        .title = QStringLiteral("Kalafina All Time Best 2008–2018"),
        .tracks = {
            LocalTrack {
                .trackId = 101,
                .title = QStringLiteral("oblivious"),
                .durationMs = 264400,
                .discNumber = 1,
                .trackNumber = 1,
            },
            LocalTrack {
                .trackId = 102,
                .title = QStringLiteral("I have a dream"),
                .durationMs = 358053,
                .discNumber = 2,
                .trackNumber = 1,
            },
            LocalTrack {
                .trackId = 103,
                .title = QStringLiteral("dolce"),
                .durationMs = 171586,
                .discNumber = 3,
                .trackNumber = 1,
            },
        },
    };

    const auto match = scoreRelease(local, release);
    QCOMPARE(match.mapping.size(), 3);
    QCOMPARE(match.mapping.at(0).trackId, 101);
    QCOMPARE(match.mapping.at(0).mbIndex, 0); // CD1 track 1
    QCOMPARE(match.mapping.at(1).trackId, 102);
    QCOMPARE(match.mapping.at(1).mbIndex, 12); // CD2 track 1
    QCOMPARE(match.mapping.at(2).trackId, 103);
    QCOMPARE(match.mapping.at(2).mbIndex, 24); // CD3 track 1
}

void TstAlbumMatch::testEmptyInputs()
{
    const LocalAlbum emptyLocal { };
    const MbRelease emptyRelease { };

    const auto match1 = scoreRelease(emptyLocal, emptyRelease);
    QCOMPARE(match1.score, 0.0);
    QVERIFY(match1.mapping.isEmpty());

    const auto dec = decide(emptyLocal, { });
    QVERIFY(!dec.best.has_value());
    QVERIFY(!dec.ambiguous);
}

} // namespace

QTEST_GUILESS_MAIN(TstAlbumMatch)

#include "tst_AlbumMatch.moc"
