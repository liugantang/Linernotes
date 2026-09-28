// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistMergeEval.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

#include <butler/ArtistMergeSource.h>
#include <library/ArtistAliasCorrections.h>
#include <library/CorrectionStore.h>
#include <library/EntityLinker.h>
#include <library/EnumNames.h>
#include <library/LibraryEnums.h>

#include <algorithm>
#include <iostream>
#include <utility>

namespace linernotes::eval {

namespace {

class DisjointSet {
public:
    void add(const QString &item)
    {
        if (!m_parent.contains(item)) {
            m_parent.insert(item, item);
        }
    }

    QString find(const QString &item)
    {
        if (!m_parent.contains(item)) {
            m_parent.insert(item, item);
            return item;
        }
        QString root = item;
        while (m_parent.value(root) != root) {
            root = m_parent.value(root);
        }
        QString curr = item;
        while (curr != root) {
            const QString next = m_parent.value(curr);
            m_parent.insert(curr, root);
            curr = next;
        }
        return root;
    }

    void unite(const QString &a, const QString &b)
    {
        const QString rootA = find(a);
        const QString rootB = find(b);
        if (rootA != rootB) {
            m_parent.insert(rootA, rootB);
        }
    }

private:
    QHash<QString, QString> m_parent;
};

// 传递性误合并：不同 entity 的曲库艺人被并进同一集合，但没有一条直接提议连着它们
void appendTransitiveMismerges(
    const QList<CorpusArtist> &corpus, DisjointSet &dsu, QList<MisMergeItem> &mismerges)
{
    for (int i = 0; i < corpus.size(); ++i) {
        for (int j = i + 1; j < corpus.size(); ++j) {
            const auto &a = corpus.at(i);
            const auto &b = corpus.at(j);
            if (a.entity == b.entity || dsu.find(a.name) != dsu.find(b.name)) {
                continue;
            }
            const bool direct = std::ranges::any_of(mismerges, [&](const MisMergeItem &m) {
                return (m.alias == a.name && m.canonical == b.name)
                    || (m.alias == b.name && m.canonical == a.name);
            });
            if (!direct) {
                mismerges.append(MisMergeItem {
                    .alias = a.name,
                    .canonical = b.name,
                    .source = QStringLiteral("transitive"),
                    .confidence = 0.0,
                    .reason = QStringLiteral("Joined through other proposals"),
                    .aliasEntity = a.entity,
                    .canonicalEntity = b.entity,
                });
            }
        }
    }
}

void printMismergesSection(const QList<MisMergeItem> &mismerges)
{
    std::cout << "=== Mis-merges (Count: " << mismerges.size() << ") ===\n";
    if (mismerges.isEmpty()) {
        std::cout << "  (none - 0 mis-merges)\n";
        return;
    }
    for (const auto &m : mismerges) {
        std::cout << "  * \"" << m.alias.toStdString() << "\" -> \"" << m.canonical.toStdString()
                  << "\"\n"
                  << "    Source: " << m.source.toStdString() << " | Confidence: " << m.confidence
                  << " | Entities: [" << m.aliasEntity.toStdString()
                  << " != " << m.canonicalEntity.toStdString() << "]\n"
                  << "    Reason: " << m.reason.toStdString() << "\n";
    }
}

void printMissedPairsSection(const QList<MissedPairItem> &missedPairs)
{
    std::cout << "\n=== Missed Merges (Count: " << missedPairs.size() << ") ===\n";
    if (missedPairs.isEmpty()) {
        std::cout << "  (none - all ground truth pairs merged)\n";
        return;
    }
    for (const auto &mp : missedPairs) {
        std::cout << "  * \"" << mp.nameA.toStdString() << "\" <-> \"" << mp.nameB.toStdString()
                  << "\" (entity: " << mp.entity.toStdString() << ")\n";
    }
}

void printSummaryTableSection(int corpusSize, int entityCount, int totalGtPairs, int totalProposals,
    int totalMismerges, double mismergeRate, int mergedGtPairs, double recall, qint64 elapsedMs)
{
    std::cout << "\n=== Summary Table ===\n";
    std::cout << "  Corpus Total Artists:    " << corpusSize << "\n";
    std::cout << "  Corpus Total Entities:   " << entityCount << "\n";
    std::cout << "  Ground Truth Pairs:      " << totalGtPairs << "\n";
    std::cout << "  Total Proposals (P):     " << totalProposals << "\n";
    std::cout << "  Total Mis-merges (W):    " << totalMismerges << "\n";
    std::cout << "  Mis-merge Rate (W / P):  "
              << QString::number(mismergeRate * 100.0, 'f', 2).toStdString() << "%\n";
    std::cout << "  Merged GT Pairs:         " << mergedGtPairs << " / " << totalGtPairs << "\n";
    std::cout << "  Recall:                  "
              << QString::number(recall * 100.0, 'f', 2).toStdString() << "%\n";
    std::cout << "  Elapsed Time:            " << elapsedMs << " ms\n";
}

void printSourceBreakdownSection(const QMap<QString, SourceReport> &sourceStats)
{
    std::cout << "\n--- Breakdown by Source ---\n";
    const QStringList knownSources = {
        QStringLiteral("rule"),
        QStringLiteral("musicbrainz"),
        QStringLiteral("llm"),
    };
    for (const auto &src : knownSources) {
        const auto stat = sourceStats.value(src);
        const double rate = stat.proposals > 0
            ? (static_cast<double>(stat.mismerges) / stat.proposals) * 100.0
            : 0.0;
        std::cout
            << "  " << qPrintable(src.leftJustified(14, QLatin1Char(' '))) << " Proposals: "
            << QString::number(stat.proposals).rightJustified(4, QLatin1Char(' ')).toStdString()
            << " | Mis-merges: "
            << QString::number(stat.mismerges).rightJustified(3, QLatin1Char(' ')).toStdString()
            << " | Rate: "
            << QString::number(rate, 'f', 2).rightJustified(6, QLatin1Char(' ')).toStdString()
            << "%\n";
    }
}

} // namespace

ArtistMergeEval::ArtistMergeEval(EvalConfig config, QObject *parent)
    : QObject(parent)
    , m_harness(std::move(config), this)
{
}

ArtistMergeEval::~ArtistMergeEval() = default;

bool ArtistMergeEval::init()
{
    return loadCorpus() && m_harness.init() && importCorpusToDb();
}

bool ArtistMergeEval::loadCorpus()
{
    QFile file(m_harness.config().corpusPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "Failed to open corpus file: " << qPrintable(m_harness.config().corpusPath)
                  << " (" << qPrintable(file.errorString()) << ")\n";
        return false;
    }

    QJsonParseError parseError { };
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        std::cerr << "Failed to parse corpus JSON: " << qPrintable(parseError.errorString())
                  << "\n";
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonArray artistsArray = root.value(QStringLiteral("artists")).toArray();
    if (artistsArray.isEmpty()) {
        std::cerr << "Corpus file contains no artists\n";
        return false;
    }

    m_corpus.clear();
    m_nameToEntity.clear();
    m_entityToNames.clear();

    for (const auto &val : artistsArray) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject obj = val.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
        const QString entity = obj.value(QStringLiteral("entity")).toString().trimmed();
        if (name.isEmpty() || entity.isEmpty()) {
            std::cerr << "Invalid artist entry in corpus: name or entity is empty\n";
            return false;
        }

        QStringList albums;
        const QJsonArray albumsArray = obj.value(QStringLiteral("albums")).toArray();
        for (const auto &albVal : albumsArray) {
            const QString album = albVal.toString().trimmed();
            if (!album.isEmpty()) {
                albums.append(album);
            }
        }
        if (albums.isEmpty()) {
            albums.append(QStringLiteral("Single"));
        }

        m_corpus.append(CorpusArtist {
            .name = name,
            .entity = entity,
            .albums = albums,
        });
        m_nameToEntity.insert(name, entity);
        auto entityIt = m_entityToNames.find(entity);
        if (entityIt == m_entityToNames.end()) {
            entityIt = m_entityToNames.insert(entity, { });
        }
        entityIt.value().append(name);
    }

    return true;
}

bool ArtistMergeEval::importCorpusToDb()
{
    auto connRes = m_harness.db().connection();
    if (!connRes.ok()) {
        std::cerr << "Database connection error: " << qPrintable(connRes.error().toString())
                  << "\n";
        return false;
    }
    const auto &conn = connRes.value();

    library::Transaction tx(conn);
    if (!tx.isActive()) {
        std::cerr << "Failed to begin transaction\n";
        return false;
    }

    const qint64 rootId = insertRoot(conn, QStringLiteral("/music"));
    if (rootId < 0) {
        std::cerr << "Failed to insert library root\n";
        return false;
    }

    library::EntityLinker linker(conn);
    qint64 fileCounter = 0;

    for (const auto &artist : m_corpus) {
        for (const auto &album : artist.albums) {
            fileCounter++;
            const QString filePath = QStringLiteral("/music/track_%1.mp3").arg(fileCounter);
            const qint64 fileId = insertFile(conn, rootId, filePath);
            if (fileId < 0) {
                std::cerr << "Failed to insert file: " << qPrintable(filePath) << "\n";
                return false;
            }

            const qint64 trackId = insertTrack(conn, fileId);
            if (trackId < 0) {
                std::cerr << "Failed to insert track for file: " << qPrintable(filePath) << "\n";
                return false;
            }

            const QString title = album + QStringLiteral(" 1");
            if (!insertRawTag(conn, trackId, QStringLiteral("TITLE"), title)
                || !insertRawTag(conn, trackId, QStringLiteral("ARTIST"), artist.name)
                || !insertRawTag(conn, trackId, QStringLiteral("ALBUM"), album)) {
                std::cerr << "Failed to insert raw tags for track: " << trackId << "\n";
                return false;
            }

            if (!updateTagsReadAt(conn, trackId)) {
                std::cerr << "Failed to update tags_read_at for track: " << trackId << "\n";
                return false;
            }

            const auto linkRes = linker.linkTrack(trackId);
            if (!linkRes.ok()) {
                std::cerr << "EntityLinker failed for track: " << trackId << ": "
                          << qPrintable(linkRes.error().toString()) << "\n";
                return false;
            }
        }
    }

    const auto commitRes = tx.commit();
    if (!commitRes.ok()) {
        std::cerr << "Failed to commit corpus import: " << qPrintable(commitRes.error().toString())
                  << "\n";
        return false;
    }

    return true;
}

void ArtistMergeEval::start()
{
    library::CorrectionStore store(m_harness.db(), m_harness.clock());
    const auto batchRes = store.createBatch(
        library::CorrectionKind::ArtistMerge, QStringLiteral("Artist merge eval batch"));
    if (!batchRes.ok()) {
        std::cerr << "Failed to create correction batch: "
                  << qPrintable(batchRes.error().toString()) << "\n";
        QCoreApplication::exit(2);
        return;
    }
    m_batchId = batchRes.value();

    const butler::ArtistMergeSource source(m_harness.db());
    const auto itemsRes = source.findItems(m_harness.config().useMusicBrainz);
    if (!itemsRes.ok()) {
        std::cerr << "Failed to find merge items: " << qPrintable(itemsRes.error().toString())
                  << "\n";
        QCoreApplication::exit(2);
        return;
    }
    const QStringList &items = itemsRes.value();

    QJsonObject params;
    params.insert(QStringLiteral("batchId"), m_batchId);
    if (m_harness.config().useMusicBrainz) {
        params.insert(QStringLiteral("useMusicBrainz"), true);
    }

    const bool started = m_harness.runJob(QStringLiteral("butler.artist_merge"),
        QStringLiteral("Artist merge eval"), items, params, [this]() { evaluateAndFinish(); });
    if (!started) {
        QCoreApplication::exit(2);
    }
}

void ArtistMergeEval::evaluateAndFinish()
{
    const qint64 elapsedMs = m_harness.elapsedMs();

    const library::CorrectionStore store(m_harness.db(), m_harness.clock());
    const auto correctionsRes = store.artistAliasCorrections(m_batchId);
    if (!correctionsRes.ok()) {
        std::cerr << "Failed to fetch corrections: "
                  << qPrintable(correctionsRes.error().toString()) << "\n";
        QCoreApplication::exit(2);
        return;
    }

    const auto &rows = correctionsRes.value();

    DisjointSet dsu;
    for (const auto &artist : m_corpus) {
        dsu.add(artist.name);
    }

    QList<MisMergeItem> mismerges;
    QMap<QString, SourceReport> sourceStats;
    int totalProposals = 0;
    int extraAliases = 0;

    for (const auto &row : rows) {
        const QString alias = row.alias.trimmed();
        const QString canonical = row.artistName.trimmed();
        const QString sourceStr = library::correctionSourceToString(row.source);

        // 别名不是曲库里的艺人（如 MusicBrainz 的外语别名）：只是新增别名，不合并任何艺人
        if (!m_nameToEntity.contains(alias)) {
            ++extraAliases;
            continue;
        }
        ++totalProposals;

        auto statIt = sourceStats.find(sourceStr);
        if (statIt == sourceStats.end()) {
            statIt = sourceStats.insert(sourceStr, { });
        }
        statIt.value().proposals++;
        dsu.unite(alias, canonical);

        const QString aliasEntity = m_nameToEntity.value(alias);
        const QString canonicalEntity = m_nameToEntity.value(canonical);

        if (canonicalEntity.isEmpty() || aliasEntity != canonicalEntity) {
            statIt.value().mismerges++;
            mismerges.append(MisMergeItem {
                .alias = alias,
                .canonical = canonical,
                .source = sourceStr,
                .confidence = row.confidence,
                .reason = row.reason,
                .aliasEntity = aliasEntity,
                .canonicalEntity = canonicalEntity,
            });
        }
    }

    appendTransitiveMismerges(m_corpus, dsu, mismerges);
    std::cout << "Extra aliases (not library artists, no merge): " << extraAliases << "\n";

    const int totalMismerges = static_cast<int>(mismerges.size());
    const double mismergeRate
        = totalProposals > 0 ? (static_cast<double>(totalMismerges) / totalProposals) : 0.0;

    int totalGtPairs = 0;
    int mergedGtPairs = 0;
    QList<MissedPairItem> missedPairs;

    for (auto it = m_entityToNames.cbegin(); it != m_entityToNames.cend(); ++it) {
        const auto &names = it.value();
        for (int i = 0; i < names.size(); ++i) {
            for (int j = i + 1; j < names.size(); ++j) {
                totalGtPairs++;
                const QString &nA = names.at(i);
                const QString &nB = names.at(j);
                if (dsu.find(nA) == dsu.find(nB)) {
                    mergedGtPairs++;
                } else {
                    missedPairs.append(MissedPairItem {
                        .nameA = nA,
                        .nameB = nB,
                        .entity = it.key(),
                    });
                }
            }
        }
    }

    const double recall
        = totalGtPairs > 0 ? (static_cast<double>(mergedGtPairs) / totalGtPairs) : 1.0;

    printReport(mismerges, missedPairs, sourceStats, totalProposals, totalMismerges, mismergeRate,
        totalGtPairs, mergedGtPairs, recall, elapsedMs);

    const int exitCode = (mismergeRate >= 0.01) ? 1 : 0;
    QCoreApplication::exit(exitCode);
}

void ArtistMergeEval::printReport(const QList<MisMergeItem> &mismerges,
    const QList<MissedPairItem> &missedPairs, const QMap<QString, SourceReport> &sourceStats,
    int totalProposals, int totalMismerges, double mismergeRate, int totalGtPairs,
    int mergedGtPairs, double recall, qint64 elapsedMs)
{
    std::cout << "\n========================================\n";
    std::cout << "      ARTIST MERGE EVALUATION REPORT    \n";
    std::cout << "========================================\n\n";

    printMismergesSection(mismerges);
    printMissedPairsSection(missedPairs);
    printSummaryTableSection(static_cast<int>(m_corpus.size()),
        static_cast<int>(m_entityToNames.size()), totalGtPairs, totalProposals, totalMismerges,
        mismergeRate, mergedGtPairs, recall, elapsedMs);
    printSourceBreakdownSection(sourceStats);
    m_harness.printLlmUsage();

    std::cout << "========================================\n";
    if (mismergeRate >= 0.01) {
        std::cout << "RESULT: FAILED (Mis-merge rate >= 1.0%)\n";
    } else {
        std::cout << "RESULT: PASSED (Mis-merge rate < 1.0%)\n";
    }
    std::cout << "========================================\n";
}

} // namespace linernotes::eval
