// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QTemporaryDir>
#include <QVariant>

#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/Migrator.h>
#include <library/SearchIndex.h>
#include <library/SearchText.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {

using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::Migrator;
using linernotes::library::SearchIndex;
using linernotes::library::Transaction;

struct BenchmarkStats {
    double minMs = 0.0;
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double maxMs = 0.0;
    double avgMs = 0.0;
    int hitCount = 0;
};

template <typename Func>
BenchmarkStats measureQuery(int warmupRuns, int measuredRuns, const Func &fn)
{
    for (int i = 0; i < warmupRuns; ++i) {
        fn();
    }
    std::vector<double> latencies;
    latencies.reserve(static_cast<size_t>(measuredRuns));
    int lastHitCount = 0;
    for (int i = 0; i < measuredRuns; ++i) {
        const auto t0 = std::chrono::high_resolution_clock::now();
        lastHitCount = fn();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        latencies.push_back(ms);
    }
    std::ranges::sort(latencies);

    BenchmarkStats stats;
    stats.minMs = latencies.front();
    stats.maxMs = latencies.back();
    stats.p50Ms = latencies.at(latencies.size() / 2);
    auto idx95 = static_cast<size_t>(std::ceil(static_cast<double>(latencies.size()) * 0.95));
    if (idx95 > 0) {
        idx95 -= 1;
    }
    if (idx95 >= latencies.size()) {
        idx95 = latencies.size() - 1;
    }
    stats.p95Ms = latencies.at(idx95);
    const double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    stats.avgMs = sum / static_cast<double>(latencies.size());
    stats.hitCount = lastHitCount;
    return stats;
}

// ---------------------------------------------------------------------------
// Wordlists for deterministic data generation
// ---------------------------------------------------------------------------

const auto kChineseChars = std::to_array<QStringView>({
    u"一",
    u"是",
    u"在",
    u"不",
    u"了",
    u"有",
    u"和",
    u"人",
    u"这",
    u"中",
    u"大",
    u"为",
    u"上",
    u"个",
    u"国",
    u"我",
    u"以",
    u"要",
    u"他",
    u"时",
    u"来",
    u"用",
    u"生",
    u"到",
    u"作",
    u"地",
    u"于",
    u"出",
    u"就",
    u"分",
    u"对",
    u"成",
    u"会",
    u"可",
    u"主",
    u"发",
    u"年",
    u"动",
    u"同",
    u"工",
    u"也",
    u"能",
    u"下",
    u"过",
    u"子",
    u"说",
    u"产",
    u"种",
    u"面",
    u"而",
    u"方",
    u"后",
    u"多",
    u"定",
    u"行",
    u"学",
    u"法",
    u"所",
    u"民",
    u"得",
    u"经",
    u"十",
    u"三",
    u"之",
    u"进",
    u"着",
    u"等",
    u"部",
    u"度",
    u"家",
    u"电",
    u"力",
    u"里",
    u"如",
    u"水",
    u"化",
    u"高",
    u"自",
    u"二",
    u"理",
    u"起",
    u"小",
    u"物",
    u"现",
    u"实",
    u"加",
    u"量",
    u"都",
    u"两",
    u"体",
    u"制",
    u"机",
    u"当",
    u"使",
    u"点",
    u"从",
    u"业",
    u"本",
    u"去",
    u"把",
    u"性",
    u"好",
    u"应",
    u"开",
    u"它",
    u"合",
    u"还",
    u"因",
    u"由",
    u"其",
    u"些",
    u"然",
    u"前",
    u"外",
    u"天",
    u"政",
    u"四",
    u"日",
    u"那",
    u"社",
    u"义",
    u"事",
    u"平",
    u"形",
    u"相",
    u"全",
    u"表",
    u"间",
    u"样",
    u"与",
    u"关",
    u"各",
    u"重",
    u"新",
    u"线",
    u"内",
    u"数",
    u"正",
    u"心",
    u"反",
    u"你",
    u"明",
    u"看",
    u"原",
    u"又",
    u"么",
    u"利",
    u"比",
    u"或",
    u"但",
    u"质",
    u"气",
    u"第",
    u"向",
    u"道",
    u"命",
    u"此",
    u"变",
    u"条",
    u"只",
    u"没",
    u"结",
    u"解",
    u"问",
    u"意",
    u"建",
    u"月",
    u"公",
    u"无",
    u"系",
    u"军",
    u"很",
    u"情",
    u"者",
    u"最",
    u"立",
    u"代",
    u"想",
    u"已",
    u"通",
    u"并",
    u"程",
    u"展",
    u"五",
    u"果",
    u"料",
    u"象",
    u"员",
    u"革",
    u"位",
    u"入",
    u"常",
    u"文",
    u"总",
    u"次",
    u"品",
    u"式",
    u"活",
    u"设",
    u"及",
    u"管",
    u"特",
    u"件",
    u"长",
    u"求",
    u"老",
    u"头",
    u"基",
    u"资",
    u"风",
    u"光",
    u"梦",
    u"夜",
    u"爱",
    u"雨",
    u"海",
    u"星",
    u"空",
    u"春",
    u"夏",
    u"秋",
    u"冬",
    u"云",
    u"雪",
    u"花",
    u"山",
    u"川",
    u"阳",
    u"蓝",
    u"红",
    u"白",
    u"黑",
    u"绿",
    u"金",
    u"银",
    u"旅",
    u"歌",
    u"声",
    u"音",
    u"乐",
    u"曲",
    u"魂",
    u"影",
    u"语",
    u"忆",
    u"念",
    u"恋",
    u"诗",
    u"画",
    u"晨",
    u"夕",
    u"路",
    u"岸",
    u"岛",
    u"桥",
    u"城",
    u"街",
    u"舟",
    u"帆",
    u"树",
    u"林",
    u"木",
    u"草",
    u"叶",
    u"鸟",
    u"蝶",
    u"铃",
    u"烟",
    u"火",
    u"尘",
    u"幻",
    u"镜",
    u"芒",
    u"彩",
    u"虹",
    u"痕",
    u"迹",
    u"倒",
    u"流",
    u"美",
    u"丽",
    u"世",
    u"界",
    u"晓",
});

const auto kJapaneseTokens = std::to_array<QStringView>({
    u"サクラ",
    u"あめ",
    u"ひかり",
    u"風",
    u"星",
    u"夢",
    u"夜",
    u"空",
    u"海",
    u"花",
    u"月",
    u"春",
    u"夏",
    u"秋",
    u"冬",
    u"雪",
    u"虹",
    u"旅",
    u"歌",
    u"声",
    u"音",
    u"道",
    u"心",
    u"恋",
    u"青",
    u"白",
    u"黒",
    u"影",
    u"街",
    u"川",
    u"山",
    u"ハル",
    u"ナツ",
    u"アキ",
    u"フユ",
    u"ホシ",
    u"ユメ",
    u"ツキ",
    u"カゼ",
    u"ソラ",
    u"ウミ",
    u"ハナ",
    u"ミチ",
    u"ココロ",
    u"ヒカリ",
    u"キセキ",
    u"トビラ",
    u"キオク",
    u"ミライ",
    u"セカイ",
    u"オト",
});

const auto kEnglishWords = std::to_array<QStringView>({
    u"Love",
    u"Night",
    u"Dream",
    u"Blue",
    u"Sky",
    u"Summer",
    u"Wind",
    u"Sun",
    u"Moon",
    u"Star",
    u"Light",
    u"Dark",
    u"Heart",
    u"Soul",
    u"Fire",
    u"Water",
    u"Rain",
    u"River",
    u"Ocean",
    u"Sea",
    u"Time",
    u"World",
    u"Life",
    u"Day",
    u"Shadow",
    u"Memory",
    u"Forever",
    u"Magic",
    u"Wonder",
    u"Dance",
    u"Song",
    u"Music",
    u"Melody",
    u"Rhythm",
    u"Beat",
    u"Sound",
    u"Voice",
    u"Silence",
    u"Golden",
    u"Silver",
    u"Crystal",
    u"Diamond",
    u"Flying",
    u"Running",
    u"Falling",
    u"Rising",
    u"Walking",
    u"Leaving",
    u"Coming",
    u"Waiting",
    u"Feeling",
    u"Touch",
    u"Smile",
    u"Tear",
    u"Kiss",
    u"Eyes",
    u"Road",
    u"Street",
    u"City",
    u"Town",
    u"Home",
    u"House",
    u"Garden",
    u"Forest",
    u"Mountain",
    u"Valley",
    u"Desert",
    u"Island",
    u"Shore",
    u"Wave",
    u"Breeze",
    u"Storm",
    u"Thunder",
    u"Lightning",
    u"Cloud",
    u"Morning",
    u"Evening",
    u"Midnight",
    u"Dawn",
    u"Dusk",
    u"Twilight",
    u"Winter",
    u"Spring",
    u"Autumn",
    u"Fall",
    u"Young",
    u"Old",
    u"New",
    u"Free",
    u"Wild",
    u"Sweet",
    u"Bitter",
    u"Happy",
    u"Sad",
    u"Lonely",
    u"Quiet",
    u"Loud",
    u"Bright",
    u"Deep",
    u"High",
    u"Low",
    u"Far",
    u"Near",
    u"True",
    u"Secret",
    u"Silent",
    u"Endless",
    u"Broken",
    u"Crazy",
    u"Gentle",
    u"Tender",
    u"Beautiful",
    u"Perfect",
    u"Little",
    u"Big",
    u"Great",
    u"Lost",
    u"Found",
    u"Without",
    u"Together",
    u"Always",
    u"Never",
    u"Again",
    u"Away",
    u"Inside",
    u"Outside",
    u"Here",
    u"There",
    u"Now",
    u"Before",
    u"After",
    u"Yesterday",
    u"Today",
    u"Tomorrow",
    u"Heaven",
    u"Paradise",
    u"Angel",
    u"Miracle",
    u"Hero",
    u"Legend",
    u"Story",
    u"Tale",
    u"Journey",
    u"Voyage",
    u"Destiny",
    u"Fate",
    u"Hope",
    u"Wish",
    u"Desire",
    u"Passion",
    u"Energy",
    u"Power",
    u"Glory",
    u"Peace",
    u"Freedom",
    u"Space",
    u"Universe",
    u"Galaxy",
    u"Planet",
    u"Future",
    u"Past",
    u"Eternal",
    u"Infinite",
    u"Electric",
});

const auto kGenres = std::to_array<QStringView>({
    u"Rock",
    u"Pop",
    u"Jazz",
    u"Classical",
    u"Electronic",
    u"Folk",
    u"Metal",
    u"Hip Hop",
    u"R&B",
    u"Soundtrack",
    u"Ambient",
    u"Blues",
    u"Soul",
    u"Country",
    u"Alternative",
});

QString generateRandomName(std::mt19937_64 &rng)
{
    const uint64_t mode = rng() % 100;
    if (mode < 40) {
        // 40% Chinese (2 - 6 chars)
        const size_t len = 2 + (rng() % 5);
        QString s;
        for (size_t i = 0; i < len; ++i) {
            s.append(kChineseChars.at(rng() % kChineseChars.size()));
        }
        return s;
    }
    if (mode < 60) {
        // 20% Japanese (2 - 4 tokens)
        const size_t len = 2 + (rng() % 3);
        QString s;
        for (size_t i = 0; i < len; ++i) {
            s.append(kJapaneseTokens.at(rng() % kJapaneseTokens.size()));
        }
        return s;
    }
    // 40% English (1 - 4 words)
    const size_t len = 1 + (rng() % 4);
    QStringList words;
    for (size_t i = 0; i < len; ++i) {
        words.append(kEnglishWords.at(rng() % kEnglishWords.size()).toString());
    }
    return words.join(QLatin1Char(' '));
}

struct AlbumInfo {
    QString title;
    int artistId = 0;
    int year = 2000;
};

struct DataGenTimings {
    double filesTracksSec = 0.0;
    double rawTagsSec = 0.0;
    double tagsReadAtSec = 0.0;
    double entityLinkerSec = 0.0;
    double ftsFlushSec = 0.0;
    double commitSec = 0.0;
    double totalSec = 0.0;
};

QString pickTrackTitle(int trackId, std::mt19937_64 &rng)
{
    switch (trackId) {
    case 1:
        return QStringLiteral("阳光回忆");
    case 2:
        return QStringLiteral("时光倒流");
    case 3:
        return QStringLiteral("美丽世界");
    case 4:
        return QStringLiteral("Love In The Night");
    case 5:
        return QStringLiteral("Dreamer In Blue Sky");
    case 6:
        return QStringLiteral("Summer Wind");
    case 7:
        return QStringLiteral("サクラの雨");
    case 8:
        return QStringLiteral("夜与梦之歌");
    default:
        return generateRandomName(rng);
    }
}

void insertFilesAndTracksBatch(QSqlQuery &insertFileStmt, QSqlQuery &insertTrackStmt,
    int startTrack, int endTrack, const std::vector<AlbumInfo> &albums,
    const std::vector<QString> &artists, std::mt19937_64 &rng)
{
    const auto kAlbumCount = albums.size();
    for (int t = startTrack; t <= endTrack; ++t) {
        const auto albumIdx = static_cast<size_t>((t - 1) % static_cast<int>(kAlbumCount));
        const auto &alb = albums.at(albumIdx);
        const auto &albumArtist = artists.at(static_cast<size_t>(alb.artistId));
        const int trackNum = ((t - 1) % 12) + 1;
        const QString title = generateRandomName(rng);
        const QString path = QStringLiteral("/music/%1/%2/%3_%4 - %5.flac")
                                 .arg(albumArtist, alb.title, QString::number(t),
                                     QString::number(trackNum).rightJustified(2, u'0'), title);

        insertFileStmt.bindValue(0, t);
        insertFileStmt.bindValue(1, 1);
        insertFileStmt.bindValue(2, path);
        insertFileStmt.bindValue(3, 1048576);
        insertFileStmt.bindValue(4, 1000);
        insertFileStmt.bindValue(5, 1000);
        insertFileStmt.bindValue(6, 1000);
        insertFileStmt.exec();

        insertTrackStmt.bindValue(0, t);
        insertTrackStmt.bindValue(1, t);
        insertTrackStmt.bindValue(2, 1000);
        insertTrackStmt.exec();
    }
}

void insertRawTagsBatch(QSqlQuery &insertRawTagStmt, int startTrack, int endTrack,
    const std::vector<AlbumInfo> &albums, const std::vector<QString> &artists, std::mt19937_64 &rng)
{
    const auto kAlbumCount = albums.size();
    const auto kArtistCount = artists.size();

    auto insertTag = [&](int trackId, const QString &key, const QString &val, int ordinal = 0) {
        insertRawTagStmt.bindValue(0, trackId);
        insertRawTagStmt.bindValue(1, QStringLiteral("id3v2"));
        insertRawTagStmt.bindValue(2, 0);
        insertRawTagStmt.bindValue(3, key);
        insertRawTagStmt.bindValue(4, ordinal);
        insertRawTagStmt.bindValue(5, val);
        insertRawTagStmt.exec();
    };

    for (int t = startTrack; t <= endTrack; ++t) {
        const auto albumIdx = static_cast<size_t>((t - 1) % static_cast<int>(kAlbumCount));
        const auto &alb = albums.at(albumIdx);
        const auto &albumArtist = artists.at(static_cast<size_t>(alb.artistId));
        QString trackArtist = albumArtist;
        if (t % 8 == 0) {
            const auto featIdx
                = static_cast<size_t>((alb.artistId + 17) % static_cast<int>(kArtistCount));
            trackArtist = albumArtist + QStringLiteral(" / ") + artists.at(featIdx);
        }
        const QString title = pickTrackTitle(t, rng);
        const int trackNum = ((t - 1) % 12) + 1;
        const auto genreIdx = static_cast<size_t>(
            (alb.artistId + static_cast<int>(albumIdx)) % static_cast<int>(kGenres.size()));
        const QString genre = kGenres.at(genreIdx).toString();

        insertTag(t, QStringLiteral("TITLE"), title);
        insertTag(t, QStringLiteral("ARTIST"), trackArtist);
        insertTag(t, QStringLiteral("ALBUM"), alb.title);
        insertTag(t, QStringLiteral("ALBUMARTIST"), albumArtist);
        insertTag(t, QStringLiteral("GENRE"), genre);
        insertTag(t, QStringLiteral("DATE"), QString::number(alb.year));
        insertTag(t, QStringLiteral("TRACKNUMBER"), QString::number(trackNum));
        insertTag(t, QStringLiteral("TRACKTOTAL"), QStringLiteral("12"));
        insertTag(t, QStringLiteral("DISCNUMBER"), QStringLiteral("1"));
        insertTag(t, QStringLiteral("DISCTOTAL"), QStringLiteral("1"));
        if (t % 4 == 0) {
            const auto compIdx
                = static_cast<size_t>((alb.artistId + 23) % static_cast<int>(kArtistCount));
            insertTag(t, QStringLiteral("COMPOSER"), artists.at(compIdx));
        }
    }
}

DataGenTimings generateLibraryData(const QSqlDatabase &conn, int totalTracks, int batchSize = 500)
{
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937_64 rng(20260926);

    // Pre-generate 5,000 artists
    constexpr int kArtistCount = 5000;
    std::vector<QString> artists;
    artists.reserve(kArtistCount);
    for (int i = 0; i < kArtistCount; ++i) {
        artists.push_back(generateRandomName(rng));
    }

    // Ensure specific test query targets exist in artist pool
    artists.at(0) = QStringLiteral("林晓风");
    artists.at(1) = QStringLiteral("周杰伦");
    artists.at(2) = QStringLiteral("宇多田ヒカル");
    artists.at(3) = QStringLiteral("Taylor Swift");
    artists.at(4) = QStringLiteral("The Beatles");

    // Pre-generate 10,000 albums
    constexpr int kAlbumCount = 10000;
    std::vector<AlbumInfo> albums;
    albums.reserve(kAlbumCount);
    for (int i = 0; i < kAlbumCount; ++i) {
        AlbumInfo alb;
        alb.title = generateRandomName(rng);
        alb.artistId = static_cast<int>(rng() % kArtistCount);
        alb.year = 1970 + static_cast<int>(rng() % 56);
        albums.push_back(alb);
    }
    // Specific predictable album titles
    albums.at(0).title = QStringLiteral("时光倒流");
    albums.at(1).title = QStringLiteral("美丽世界");
    albums.at(2).title = QStringLiteral("Blue Sky");
    albums.at(3).title = QStringLiteral("Summer Wind");
    albums.at(4).title = QStringLiteral("サクラの涙");
    albums.at(5).title = QStringLiteral("阳光与海");
    albums.at(6).title = QStringLiteral("回忆如诗");

    // Insert library root
    {
        QSqlQuery q(conn);
        q.prepare(QStringLiteral(
            "INSERT INTO library_roots (id, path, added_at) VALUES (1, '/music', 1000);"));
        q.exec();
    }

    QSqlQuery insertFileStmt(conn);
    insertFileStmt.prepare(QStringLiteral(
        "INSERT INTO files (id, root_id, path, size, mtime, first_seen_at, scanned_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);"));

    QSqlQuery insertTrackStmt(conn);
    insertTrackStmt.prepare(QStringLiteral("INSERT INTO tracks (id, file_id, tags_read_at, "
                                           "created_at) "
                                           "VALUES (?, ?, 0, ?);"));

    QSqlQuery insertRawTagStmt(conn);
    insertRawTagStmt.prepare(
        QStringLiteral("INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
                       "VALUES (?, ?, ?, ?, ?, ?);"));

    QSqlQuery updateTagsReadAtStmt(conn);
    updateTagsReadAtStmt.prepare(
        QStringLiteral("UPDATE tracks SET tags_read_at = ? WHERE id = ?;"));

    EntityLinker entityLinker(conn);
    SearchIndex searchIndex(conn);

    int64_t nsFilesTracks = 0;
    int64_t nsRawTags = 0;
    int64_t nsTagsReadAt = 0;
    int64_t nsEntityLinker = 0;
    int64_t nsFtsFlush = 0;
    int64_t nsCommit = 0;

    const auto totalStart = std::chrono::high_resolution_clock::now();
    QElapsedTimer timer;

    const int totalBatches = (totalTracks + batchSize - 1) / batchSize;
    for (int b = 0; b < totalBatches; ++b) {
        const int startTrack = (b * batchSize) + 1;
        const int endTrack = std::min(startTrack + batchSize - 1, totalTracks);

        Transaction tx(conn, Transaction::Mode::Immediate);

        // Phase 1: Files & Tracks Insertion
        timer.start();
        insertFilesAndTracksBatch(
            insertFileStmt, insertTrackStmt, startTrack, endTrack, albums, artists, rng);
        nsFilesTracks += timer.nsecsElapsed();

        // Phase 2: raw_tags Insertion (~10 tags per track)
        timer.start();
        insertRawTagsBatch(insertRawTagStmt, startTrack, endTrack, albums, artists, rng);
        nsRawTags += timer.nsecsElapsed();

        // Phase 3: tags_read_at update (Trigger effective_metadata)
        timer.start();
        for (int t = startTrack; t <= endTrack; ++t) {
            updateTagsReadAtStmt.bindValue(0, 2000);
            updateTagsReadAtStmt.bindValue(1, t);
            updateTagsReadAtStmt.exec();
        }
        nsTagsReadAt += timer.nsecsElapsed();

        // Phase 4: EntityLinker (linkTrack)
        timer.start();
        for (int t = startTrack; t <= endTrack; ++t) {
            const auto linkRes = entityLinker.linkTrack(t);
            if (!linkRes.ok()) {
                std::cerr << "linkTrack failed: " << linkRes.error().toString().toStdString()
                          << '\n';
            }
        }
        nsEntityLinker += timer.nsecsElapsed();

        // Phase 5: SearchIndex (flushDirty)
        timer.start();
        const auto flushRes = searchIndex.flushDirty();
        if (!flushRes.ok()) {
            std::cerr << "flushDirty failed: " << flushRes.error().toString().toStdString() << '\n';
        }
        nsFtsFlush += timer.nsecsElapsed();

        // Phase 6: Commit
        timer.start();
        const auto commitRes = tx.commit();
        if (!commitRes.ok()) {
            std::cerr << "commit failed: " << commitRes.error().toString().toStdString() << '\n';
        }
        nsCommit += timer.nsecsElapsed();

        if ((b + 1) % 40 == 0 || b == totalBatches - 1) {
            std::cout << "  Generating data: " << endTrack << " / " << totalTracks
                      << " tracks...\n";
        }
    }

    const auto totalEnd = std::chrono::high_resolution_clock::now();
    const double totalSec = std::chrono::duration<double>(totalEnd - totalStart).count();

    DataGenTimings timings;
    timings.filesTracksSec = static_cast<double>(nsFilesTracks) / 1e9;
    timings.rawTagsSec = static_cast<double>(nsRawTags) / 1e9;
    timings.tagsReadAtSec = static_cast<double>(nsTagsReadAt) / 1e9;
    timings.entityLinkerSec = static_cast<double>(nsEntityLinker) / 1e9;
    timings.ftsFlushSec = static_cast<double>(nsFtsFlush) / 1e9;
    timings.commitSec = static_cast<double>(nsCommit) / 1e9;
    timings.totalSec = totalSec;
    return timings;
}

// ---------------------------------------------------------------------------
// Section 3: Investigation of Legacy Phenomenon
// ---------------------------------------------------------------------------

struct InvestigationResult {
    double appBatchPerTrackMs = 0.0;
    double cteTotalSec = 0.0;
    double cteUserSec = 0.0;
    double cteSysSec = 0.0;
    double ctePerTrackMs = 0.0;
    double cteNoTriggerSec = 0.0;
    double cteNoTriggerUserSec = 0.0;
    double cteNoTriggerSysSec = 0.0;
};

InvestigationResult runInvestigation(int count = 50000)
{
    InvestigationResult res;

    QTemporaryDir tempDir;
    Database db(tempDir.filePath(QStringLiteral("investigate.db")));
    Migrator migrator;
    const auto openRes1 = db.open(migrator);
    if (!openRes1.ok()) {
        std::cerr << "investigation db open failed: " << openRes1.error().toString().toStdString()
                  << '\n';
    }
    const auto connRes = db.connection();
    const auto &conn = connRes.value();

    // 1. Measure app batch track insertion (500 tracks/tx)
    QElapsedTimer tBatch;
    tBatch.start();
    {
        QSqlQuery insertFile(conn);
        insertFile.prepare(QStringLiteral(
            "INSERT INTO files (id, root_id, path, size, mtime, first_seen_at, scanned_at) "
            "VALUES (?, 1, '/m/' || ? || '.mp3', 1024, 1000, 1000, 1000);"));
        QSqlQuery insertTrack(conn);
        insertTrack.prepare(QStringLiteral("INSERT INTO tracks (id, file_id, tags_read_at, "
                                           "created_at) VALUES (?, ?, 1000, 1000);"));

        const int batches = count / 500;
        for (int b = 0; b < batches; ++b) {
            Transaction tx(conn, Transaction::Mode::Immediate);
            for (int i = (b * 500) + 1; i <= (b + 1) * 500; ++i) {
                insertFile.bindValue(0, i);
                insertFile.bindValue(1, i);
                insertFile.exec();
                insertTrack.bindValue(0, i);
                insertTrack.bindValue(1, i);
                insertTrack.exec();
            }
            const auto cRes = tx.commit();
            if (!cRes.ok()) {
                std::cerr << "investigation tx commit failed\n";
            }
        }
    }
    res.appBatchPerTrackMs = (static_cast<double>(tBatch.elapsed())) / count;

    // 2. Measure single CTE insert with triggers on
    QTemporaryDir tempDirCte;
    Database dbCte(tempDirCte.filePath(QStringLiteral("cte.db")));
    const auto openRes2 = dbCte.open(migrator);
    if (!openRes2.ok()) {
        std::cerr << "investigation cte db open failed\n";
    }
    const auto connCteRes = dbCte.connection();
    const auto &connCte = connCteRes.value();

    {
        QSqlQuery q(connCte);
        q.exec(QStringLiteral(
            "INSERT INTO library_roots (id, path, added_at) VALUES (1, '/m', 1000);"));
        q.exec(QStringLiteral(
            "WITH RECURSIVE cnt(x) AS ("
            "    SELECT 1 UNION ALL SELECT x+1 FROM cnt WHERE x < 50000"
            ") "
            "INSERT INTO files (id, root_id, path, size, mtime, first_seen_at, scanned_at) "
            "SELECT x, 1, '/m/' || x || '.mp3', 1024, 1000, 1000, 1000 FROM cnt;"));

        struct rusage r0 { };
        struct rusage r1 { };
        getrusage(RUSAGE_SELF, &r0);
        const auto t0 = std::chrono::high_resolution_clock::now();

        q.exec(QStringLiteral("WITH RECURSIVE cnt(x) AS ("
                              "    SELECT 1 UNION ALL SELECT x+1 FROM cnt WHERE x < 50000"
                              ") "
                              "INSERT INTO tracks (id, file_id, tags_read_at, created_at) "
                              "SELECT x, x, 1000, 1000 FROM cnt;"));

        const auto t1 = std::chrono::high_resolution_clock::now();
        getrusage(RUSAGE_SELF, &r1);

        res.cteTotalSec = std::chrono::duration<double>(t1 - t0).count();
        res.cteUserSec = static_cast<double>(r1.ru_utime.tv_sec - r0.ru_utime.tv_sec)
            + (static_cast<double>(r1.ru_utime.tv_usec - r0.ru_utime.tv_usec) / 1e6);
        res.cteSysSec = static_cast<double>(r1.ru_stime.tv_sec - r0.ru_stime.tv_sec)
            + (static_cast<double>(r1.ru_stime.tv_usec - r0.ru_stime.tv_usec) / 1e6);
        res.ctePerTrackMs = (res.cteTotalSec * 1000.0) / count;
    }

    // 3. Measure single CTE with triggers dropped
    QTemporaryDir tempDirNoTrig;
    Database dbNoTrig(tempDirNoTrig.filePath(QStringLiteral("notrig.db")));
    const auto openRes3 = dbNoTrig.open(migrator);
    if (!openRes3.ok()) {
        std::cerr << "investigation no-trig db open failed\n";
    }
    const auto connNoTrigRes = dbNoTrig.connection();
    const auto &connNoTrig = connNoTrigRes.value();

    {
        QSqlQuery q(connNoTrig);
        q.exec(QStringLiteral("DROP TRIGGER effective_metadata_after_track_insert;"));
        q.exec(QStringLiteral("DROP TRIGGER search_dirty_after_effective_metadata_insert;"));
        q.exec(QStringLiteral(
            "INSERT INTO library_roots (id, path, added_at) VALUES (1, '/m', 1000);"));
        q.exec(QStringLiteral(
            "WITH RECURSIVE cnt(x) AS ("
            "    SELECT 1 UNION ALL SELECT x+1 FROM cnt WHERE x < 50000"
            ") "
            "INSERT INTO files (id, root_id, path, size, mtime, first_seen_at, scanned_at) "
            "SELECT x, 1, '/m/' || x || '.mp3', 1024, 1000, 1000, 1000 FROM cnt;"));

        struct rusage r0 { };
        struct rusage r1 { };
        getrusage(RUSAGE_SELF, &r0);
        const auto t0 = std::chrono::high_resolution_clock::now();

        q.exec(QStringLiteral("WITH RECURSIVE cnt(x) AS ("
                              "    SELECT 1 UNION ALL SELECT x+1 FROM cnt WHERE x < 50000"
                              ") "
                              "INSERT INTO tracks (id, file_id, tags_read_at, created_at) "
                              "SELECT x, x, 1000, 1000 FROM cnt;"));

        const auto t1 = std::chrono::high_resolution_clock::now();
        getrusage(RUSAGE_SELF, &r1);

        res.cteNoTriggerSec = std::chrono::duration<double>(t1 - t0).count();
        res.cteNoTriggerUserSec = static_cast<double>(r1.ru_utime.tv_sec - r0.ru_utime.tv_sec)
            + (static_cast<double>(r1.ru_utime.tv_usec - r0.ru_utime.tv_usec) / 1e6);
        res.cteNoTriggerSysSec = static_cast<double>(r1.ru_stime.tv_sec - r0.ru_stime.tv_sec)
            + (static_cast<double>(r1.ru_stime.tv_usec - r0.ru_stime.tv_usec) / 1e6);
    }

    return res;
}

bool runFtsBenchmarks(const QSqlDatabase &conn)
{
    std::cout << "\n[2/5] Benchmarking FTS Search Queries (limit 200, 5 warmups + 20 runs)...\n";
    std::cout << std::left << std::setw(32) << "Query Description" << std::setw(24) << "Query Input"
              << std::right << std::setw(10) << "Hits" << std::setw(10) << "p50 (ms)"
              << std::setw(10) << "p95 (ms)" << std::setw(10) << "max (ms)" << std::setw(10)
              << "Status\n";
    std::cout << std::string(106, '-') << '\n';

    SearchIndex searchIndex(conn);

    struct FtsQueryTest {
        const char *desc = nullptr;
        QString input;
    };

    const std::vector<FtsQueryTest> ftsTests = {
        { .desc = "English word", .input = QStringLiteral("love") },
        { .desc = "English prefix (3 chars)", .input = QStringLiteral("dre") },
        { .desc = "Two Chinese characters", .input = QStringLiteral("阳光") },
        { .desc = "Four Chinese characters", .input = QStringLiteral("时光倒流") },
        { .desc = "Pinyin initials (3 chars)", .input = QStringLiteral("ygs") },
        { .desc = "Pinyin continuous prefix", .input = QStringLiteral("yangguang") },
        { .desc = "Japanese Kana", .input = QStringLiteral("サクラ") },
        { .desc = "Two words AND", .input = QStringLiteral("blue sky") },
        { .desc = "Low-match rare query", .input = QStringLiteral("xyzqwk999") },
        { .desc = "High-frequency character", .input = QStringLiteral("人") },
    };

    bool allPassed = true;
    for (const auto &test : ftsTests) {
        const auto stats = measureQuery(5, 20, [&]() -> int {
            const auto res = searchIndex.search(test.input, 200);
            return res.ok() ? static_cast<int>(res.value().size()) : 0;
        });

        const bool pass = (stats.p95Ms < 100.0);
        if (!pass) {
            allPassed = false;
        }

        std::cout << std::left << std::setw(32) << test.desc << std::setw(24)
                  << test.input.toStdString() << std::right << std::setw(10) << stats.hitCount
                  << std::setw(10) << std::fixed << std::setprecision(2) << stats.p50Ms
                  << std::setw(10) << std::fixed << std::setprecision(2) << stats.p95Ms
                  << std::setw(10) << std::fixed << std::setprecision(2) << stats.maxMs
                  << std::setw(10) << (pass ? "PASS" : "FAIL") << '\n';
    }
    return allPassed;
}

bool runBrowseBenchmarks(const QSqlDatabase &conn)
{
    std::cout << "\n[3/5] Benchmarking Browse Queries (5 warmups + 20 runs)...\n";
    std::cout << std::left << std::setw(42) << "Query Description" << std::right << std::setw(10)
              << "Hits" << std::setw(10) << "p50 (ms)" << std::setw(10) << "p95 (ms)"
              << std::setw(10) << "max (ms)" << std::setw(10) << "Status\n";
    std::cout << std::string(92, '-') << '\n';

    bool allPassed = true;
    auto benchmarkBrowse
        = [&](const char *desc, const QString &sql, const std::vector<QVariant> &binds = { }) {
              const auto stats = measureQuery(5, 20, [&]() -> int {
                  QSqlQuery q(conn);
                  q.prepare(sql);
                  for (size_t i = 0; i < binds.size(); ++i) {
                      q.bindValue(static_cast<int>(i), binds.at(i));
                  }
                  if (!q.exec()) {
                      return 0;
                  }
                  int rows = 0;
                  while (q.next()) {
                      rows++;
                  }
                  return rows;
              });

              const bool pass = (stats.p95Ms < 100.0);
              if (!pass) {
                  allPassed = false;
              }

              std::cout << std::left << std::setw(42) << desc << std::right << std::setw(10)
                        << stats.hitCount << std::setw(10) << std::fixed << std::setprecision(2)
                        << stats.p50Ms << std::setw(10) << std::fixed << std::setprecision(2)
                        << stats.p95Ms << std::setw(10) << std::fixed << std::setprecision(2)
                        << stats.maxMs << std::setw(10) << (pass ? "PASS" : "FAIL") << '\n';
          };

    benchmarkBrowse("Tracks sorted (LIMIT 100 OFFSET 50000)",
        QStringLiteral("SELECT track_id, title, artist, album, album_artist, genre, year, "
                       "track_number, disc_number FROM effective_metadata "
                       "ORDER BY album_artist ASC, album ASC, "
                       "disc_number ASC, track_number ASC "
                       "LIMIT 100 OFFSET 50000;"));

    benchmarkBrowse("Artist tracks (JOIN track_artists)",
        QStringLiteral("SELECT t.id, em.title, em.artist, em.album, em.year, em.track_number "
                       "FROM track_artists ta "
                       "JOIN tracks t ON ta.track_id = t.id "
                       "JOIN effective_metadata em ON t.id = em.track_id "
                       "WHERE ta.artist_id = ? "
                       "ORDER BY em.year ASC, em.album ASC, em.track_number ASC;"),
        { 1 });

    benchmarkBrowse("Albums sorted (LIMIT 100 OFFSET 5000)",
        QStringLiteral("SELECT id, grouping_key, title, album_artist, year FROM albums "
                       "ORDER BY title ASC LIMIT 100 OFFSET 5000;"));

    benchmarkBrowse("Genre track count (Rock)",
        QStringLiteral("SELECT COUNT(*) FROM effective_metadata WHERE genre = ?;"),
        { QStringLiteral("Rock") });

    return allPassed;
}

void runColdStartBenchmarks(const QSqlDatabase &conn, const QString &dbPath)
{
    std::cout << "\n[4/5] Benchmarking Single Track Refresh & Cold Start...\n";
    std::cout << std::left << std::setw(42) << "Operation" << std::right << std::setw(10)
              << "p50 (ms)" << std::setw(10) << "p95 (ms)" << std::setw(10) << "max (ms)\n";
    std::cout << std::string(72, '-') << '\n';

    SearchIndex searchIndex(conn);
    {
        // Single track user_override + flushDirty()
        QSqlQuery overrideStmt(conn);
        overrideStmt.prepare(QStringLiteral(
            "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
            "VALUES (50000, 'title', ?, 3000, 3000) "
            "ON CONFLICT(track_id, field) DO UPDATE SET value = excluded.value, updated_at = "
            "3000;"));

        int runCounter = 0;
        const auto stats = measureQuery(3, 20, [&]() -> int {
            Transaction tx(conn, Transaction::Mode::Immediate);
            overrideStmt.bindValue(0, QStringLiteral("New Title %1").arg(++runCounter));
            overrideStmt.exec();
            const auto fRes = searchIndex.flushDirty();
            if (!fRes.ok()) {
                std::cerr << "Single track flushDirty failed\n";
            }
            const auto cRes = tx.commit();
            if (!cRes.ok()) {
                std::cerr << "Single track tx commit failed\n";
            }
            return 1;
        });

        std::cout << std::left << std::setw(42) << "Single track override + flushDirty()"
                  << std::right << std::setw(10) << std::fixed << std::setprecision(2)
                  << stats.p50Ms << std::setw(10) << std::fixed << std::setprecision(2)
                  << stats.p95Ms << std::setw(10) << std::fixed << std::setprecision(2)
                  << stats.maxMs << '\n';
    }

    {
        // Cold start check: reopen DB + migration check
        std::vector<double> reopenTimes;
        for (int i = 0; i < 5; ++i) {
            const auto t0 = std::chrono::high_resolution_clock::now();
            {
                Database dbReopen(dbPath);
                Migrator migratorReopen;
                const auto opRes = dbReopen.open(migratorReopen);
                if (!opRes.ok()) {
                    std::cerr << "Reopen migration check failed\n";
                }
            }
            const auto t1 = std::chrono::high_resolution_clock::now();
            reopenTimes.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        std::ranges::sort(reopenTimes);
        std::cout << std::left << std::setw(42) << "Database reopen + migration check" << std::right
                  << std::setw(10) << std::fixed << std::setprecision(2)
                  << reopenTimes.at(reopenTimes.size() / 2) << std::setw(10) << std::fixed
                  << std::setprecision(2) << reopenTimes.back() << std::setw(10) << std::fixed
                  << std::setprecision(2) << reopenTimes.back() << '\n';
    }

    {
        // SELECT COUNT(*) FROM tracks
        const auto stats = measureQuery(3, 10, [&]() -> int {
            QSqlQuery q(conn);
            q.exec(QStringLiteral("SELECT COUNT(*) FROM tracks;"));
            q.next();
            return q.value(0).toInt();
        });
        std::cout << std::left << std::setw(42) << "SELECT COUNT(*) FROM tracks" << std::right
                  << std::setw(10) << std::fixed << std::setprecision(2) << stats.p50Ms
                  << std::setw(10) << std::fixed << std::setprecision(2) << stats.p95Ms
                  << std::setw(10) << std::fixed << std::setprecision(2) << stats.maxMs << '\n';
    }
}

int runBenchmarkApp(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("linernotes-bench-library"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Linernotes Library Performance Benchmark (100k tracks)"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption countOption(
        QStringList() << QStringLiteral("n") << QStringLiteral("count") << QStringLiteral("tracks"),
        QStringLiteral("Number of tracks to generate and benchmark (default: 100000)."),
        QStringLiteral("count"), QStringLiteral("100000"));
    parser.addOption(countOption);

    QCommandLineOption checkOption(QStringLiteral("check"),
        QStringLiteral(
            "Fail (exit non-zero) if any FTS query p95 >= 100 ms or browse query p95 >= 100 ms."));
    parser.addOption(checkOption);

    QCommandLineOption keepOption(QStringLiteral("keep"),
        QStringLiteral("Keep the temporary database directory after benchmark completes."));
    parser.addOption(keepOption);

    parser.process(app);

    bool ok = false;
    const int totalTracks = parser.value(countOption).toInt(&ok);
    if (!ok || totalTracks <= 0) {
        std::cerr << "Invalid track count: " << parser.value(countOption).toStdString() << '\n';
        return 1;
    }

    const bool checkThresholds = parser.isSet(checkOption);
    const bool keepDb = parser.isSet(keepOption);

    std::cout
        << "================================================================================\n";
    std::cout << " Linernotes Library Performance Benchmark\n";
    std::cout << " Target Tracks: " << totalTracks
              << " | Check mode: " << (checkThresholds ? "ON (< 100 ms)" : "OFF") << '\n';
    std::cout
        << "================================================================================\n";

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        std::cerr << "Failed to create temporary directory for benchmark database.\n";
        return 1;
    }
    if (keepDb) {
        tempDir.setAutoRemove(false);
    }

    const QString dbPath = tempDir.filePath(QStringLiteral("bench_library.db"));
    std::cout << "Database path: " << dbPath.toStdString() << '\n';

    // Open database with real application configurations & migrations
    Database db(dbPath);
    Migrator migrator;
    const auto openRes = db.open(migrator);
    if (!openRes.ok()) {
        std::cerr << "Failed to open database: " << openRes.error().toString().toStdString()
                  << '\n';
        return 1;
    }

    const auto connRes = db.connection();
    if (!connRes.ok()) {
        std::cerr << "Failed to acquire database connection: "
                  << connRes.error().toString().toStdString() << '\n';
        return 1;
    }
    const auto &conn = connRes.value();

    // 1. Data Generation
    std::cout << "\n[1/5] Generating " << totalTracks << " tracks (deterministic seed)...\n";
    const auto genTimings = generateLibraryData(conn, totalTracks, 500);

    std::cout << "\n--- Data Generation Timings (" << totalTracks << " tracks) ---\n";
    std::cout << std::left << std::setw(42) << "Phase" << std::right << std::setw(15)
              << "Total Time (s)" << std::setw(20) << "Avg (ms/track)" << '\n';
    std::cout << std::string(77, '-') << '\n';

    auto printGenRow = [&](const char *name, double sec) {
        const double avgMs = (sec * 1000.0) / totalTracks;
        std::cout << std::left << std::setw(42) << name << std::right << std::setw(15) << std::fixed
                  << std::setprecision(3) << sec << std::setw(20) << std::fixed
                  << std::setprecision(4) << avgMs << '\n';
    };

    printGenRow("1. Files & Tracks Insert", genTimings.filesTracksSec);
    printGenRow("2. Raw Tags Insert (~10 tags/track)", genTimings.rawTagsSec);
    printGenRow("3. tags_read_at Update (Triggers)", genTimings.tagsReadAtSec);
    printGenRow("4. Entity Linking (EntityLinker)", genTimings.entityLinkerSec);
    printGenRow("5. FTS Dirty Flush (ICU + FTS5)", genTimings.ftsFlushSec);
    printGenRow("6. Transaction Commits", genTimings.commitSec);
    std::cout << std::string(77, '-') << '\n';
    std::cout << std::left << std::setw(42) << "Total Data Generation" << std::right
              << std::setw(15) << std::fixed << std::setprecision(3) << genTimings.totalSec
              << std::setw(20) << std::fixed << std::setprecision(4)
              << (genTimings.totalSec * 1000.0) / totalTracks << '\n';

    // Verify row counts
    {
        QSqlQuery q(conn);
        q.exec(QStringLiteral("SELECT COUNT(*) FROM tracks;"));
        q.next();
        const qint64 trackCount = q.value(0).toLongLong();
        q.exec(QStringLiteral("SELECT COUNT(*) FROM artists;"));
        q.next();
        const qint64 artistCount = q.value(0).toLongLong();
        q.exec(QStringLiteral("SELECT COUNT(*) FROM albums;"));
        q.next();
        const qint64 albumCount = q.value(0).toLongLong();
        q.exec(QStringLiteral("SELECT COUNT(*) FROM effective_metadata;"));
        q.next();
        const qint64 effCount = q.value(0).toLongLong();
        std::cout << "Database verified: " << trackCount << " tracks, " << artistCount
                  << " artists, " << albumCount << " albums, " << effCount
                  << " effective_metadata rows.\n";
    }

    const bool ftsPassed = runFtsBenchmarks(conn);
    const bool browsePassed = runBrowseBenchmarks(conn);
    runColdStartBenchmarks(conn, dbPath);

    // 5. Section 3 Investigation
    std::cout << "\n[5/5] Investigating Legacy CTE Phenomenon (50k rows comparison)...\n";
    const auto inv = runInvestigation(50000);
    std::cout << "  - App 500-batch insertion rate:      " << std::fixed << std::setprecision(4)
              << inv.appBatchPerTrackMs << " ms/track\n";
    std::cout << "  - Recursive CTE (Triggers ON):       " << std::fixed << std::setprecision(3)
              << inv.cteTotalSec << " s (User: " << inv.cteUserSec << " s, Sys: " << inv.cteSysSec
              << " s) -> " << std::fixed << std::setprecision(4) << inv.ctePerTrackMs
              << " ms/track\n";
    std::cout << "  - Recursive CTE (Triggers OFF):      " << std::fixed << std::setprecision(3)
              << inv.cteNoTriggerSec << " s (User: " << inv.cteNoTriggerUserSec
              << " s, Sys: " << inv.cteNoTriggerSysSec << " s)\n";

    const bool allPassed = (ftsPassed && browsePassed);
    std::cout
        << "\n================================================================================\n";
    if (allPassed) {
        std::cout << " Conclusion: PASS (All FTS & Browse queries p95 < 100 ms threshold @ 100k "
                     "tracks)\n";
    } else {
        std::cout << " Conclusion: FAIL (One or more queries exceeded 100 ms p95 threshold)\n";
    }
    std::cout
        << "================================================================================\n";

    if (keepDb) {
        std::cout << "Temporary database retained at: " << dbPath.toStdString() << '\n';
    }

    if (checkThresholds && !allPassed) {
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char *argv[]) noexcept
{
    try {
        return runBenchmarkApp(argc, argv);
    } catch (...) {
        std::cerr << "Unhandled exception in main\n";
        return 1;
    }
}
