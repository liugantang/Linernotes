// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistCreditEval.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

#include <butler/ArtistCredit.h>
#include <butler/ArtistCreditSource.h>
#include <butler/ArtistCreditStore.h>
#include <library/CorrectionStore.h>
#include <library/EntityLinker.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <utility>

namespace linernotes::eval {

namespace {

enum class EntryParseStatus : std::uint8_t {
    Success,
    Skip,
    Error,
};

struct CorpusEvalStats {
    int correctCount = 0;
    int unparsedCount = 0;
    int expectedAkaCount = 0;
    int matchedAkaCount = 0;
};

void printAkaMap(const QHash<QString, QStringList> &akaMap)
{
    std::cout << "{ ";
    bool first = true;
    for (auto it = akaMap.cbegin(); it != akaMap.cend(); ++it) {
        if (!first) {
            std::cout << ", ";
        }
        std::cout << "\"" << it.key().toStdString() << "\": ["
                  << it.value().join(QStringLiteral(", ")).toStdString() << "]";
        first = false;
    }
    std::cout << " }";
}

void printSingleErrorItem(const CreditErrorItem &err)
{
    std::cout << "  * Value: \"" << err.value.toStdString() << "\"\n";
    std::cout << "    Expected Performers: ["
              << err.expectedPerformers.join(QStringLiteral(", ")).toStdString() << "]";
    if (!err.expectedAka.isEmpty()) {
        std::cout << " | Expected AKA: ";
        printAkaMap(err.expectedAka);
    }
    std::cout << "\n";

    if (err.unparsed) {
        std::cout << "    Actual:              <no result>\n";
        return;
    }

    std::cout << "    Actual Performers:   ["
              << err.actualPerformers.join(QStringLiteral(", ")).toStdString() << "]";
    if (!err.actualAka.isEmpty()) {
        std::cout << " | Actual AKA: ";
        printAkaMap(err.actualAka);
    }
    std::cout << "\n";
    if (!err.reason.isEmpty()) {
        std::cout << "    Reason:              " << err.reason.toStdString() << "\n";
    }
}

void printErrorsSection(const QList<CreditErrorItem> &errors)
{
    std::cout << "=== Errors (Count: " << errors.size() << ") ===\n";
    if (errors.isEmpty()) {
        std::cout << "  (none - all artist credits parsed correctly)\n";
        return;
    }
    for (const auto &err : errors) {
        printSingleErrorItem(err);
    }
}

QHash<QString, QStringList> parseAkaMap(const QJsonObject &akaObj)
{
    QHash<QString, QStringList> aka;
    for (auto it = akaObj.begin(); it != akaObj.end(); ++it) {
        const QString perfKey = it.key().trimmed();
        if (perfKey.isEmpty() || !it.value().isArray()) {
            continue;
        }
        QStringList akaList;
        for (const auto &a : it.value().toArray()) {
            const QString aStr = a.toString().trimmed();
            if (!aStr.isEmpty()) {
                akaList.append(aStr);
            }
        }
        if (!akaList.isEmpty()) {
            aka.insert(perfKey, akaList);
        }
    }
    return aka;
}

QStringList parsePerformers(const QJsonArray &perfArr)
{
    QStringList performers;
    for (const auto &pVal : perfArr) {
        const QString pStr = pVal.toString().trimmed();
        if (!pStr.isEmpty()) {
            performers.append(pStr);
        }
    }
    return performers;
}

EntryParseStatus parseCorpusEntry(const QJsonValue &val, CreditCorpusItem &outItem)
{
    if (!val.isObject()) {
        return EntryParseStatus::Skip;
    }
    const QJsonObject obj = val.toObject();
    const QString valueStr = obj.value(QStringLiteral("value")).toString().trimmed();
    if (valueStr.isEmpty()) {
        std::cerr << "Invalid entry in corpus: value is empty\n";
        return EntryParseStatus::Error;
    }

    const QStringList performers
        = parsePerformers(obj.value(QStringLiteral("performers")).toArray());
    if (performers.isEmpty()) {
        std::cerr << "Invalid entry in corpus: performers is empty for value: "
                  << qPrintable(valueStr) << "\n";
        return EntryParseStatus::Error;
    }

    QHash<QString, QStringList> aka;
    if (obj.contains(QStringLiteral("aka")) && obj.value(QStringLiteral("aka")).isObject()) {
        aka = parseAkaMap(obj.value(QStringLiteral("aka")).toObject());
    }

    outItem = CreditCorpusItem {
        .value = valueStr,
        .performers = performers,
        .aka = aka,
    };
    return EntryParseStatus::Success;
}

bool matchPerformerAka(const QStringList &expAkaList, const QStringList &actualAkaList)
{
    for (const auto &expAka : expAkaList) {
        const bool found = std::ranges::any_of(actualAkaList, [&](const QString &actAka) {
            return actAka.compare(expAka, Qt::CaseInsensitive) == 0;
        });
        if (!found) {
            return false;
        }
    }
    return true;
}

bool matchItemAka(const QHash<QString, QStringList> &expectedAka,
    const QList<butler::CreditPerformer> &performers)
{
    for (auto it = expectedAka.cbegin(); it != expectedAka.cend(); ++it) {
        const QString &perfName = it.key();
        const QStringList &expAkaList = it.value();

        const auto perfIt = std::ranges::find_if(
            performers, [&](const butler::CreditPerformer &p) { return p.name == perfName; });
        if (perfIt == performers.end() || !matchPerformerAka(expAkaList, perfIt->aka)) {
            return false;
        }
    }
    return true;
}

void evaluateSingleCorpusItem(const CreditCorpusItem &item,
    const QHash<QString, butler::StoredArtistCredit> &credits, CorpusEvalStats &stats,
    QList<CreditErrorItem> &errors)
{
    if (!credits.contains(item.value)) {
        ++stats.unparsedCount;
        errors.append(CreditErrorItem {
            .value = item.value,
            .expectedPerformers = item.performers,
            .expectedAka = item.aka,
            .actualPerformers = { },
            .actualAka = { },
            .reason = QString(),
            .unparsed = true,
        });
        return;
    }

    const auto &stored = credits.value(item.value);
    const auto &credit = stored.credit;

    QStringList actualNames;
    QHash<QString, QStringList> actualAka;
    for (const auto &p : credit.performers) {
        actualNames.append(p.name);
        if (!p.aka.isEmpty()) {
            actualAka.insert(p.name, p.aka);
        }
    }

    if (actualNames == item.performers) {
        ++stats.correctCount;
    } else {
        errors.append(CreditErrorItem {
            .value = item.value,
            .expectedPerformers = item.performers,
            .expectedAka = item.aka,
            .actualPerformers = actualNames,
            .actualAka = actualAka,
            .reason = credit.reason,
            .unparsed = false,
        });
    }

    if (!item.aka.isEmpty()) {
        ++stats.expectedAkaCount;
        if (matchItemAka(item.aka, credit.performers)) {
            ++stats.matchedAkaCount;
        }
    }
}

void printCorpusSummaryTable(int totalCount, const CorpusEvalStats &stats, double accuracy,
    double akaInclusionRate, qint64 elapsedMs)
{
    std::cout << "\n=== Summary Table ===\n";
    std::cout << "  Total Items:             " << totalCount << "\n";
    std::cout << "  Correct Items:           " << stats.correctCount << "\n";
    std::cout << "  Accuracy:                "
              << QString::number(accuracy * 100.0, 'f', 2).toStdString() << "%\n";
    std::cout << "  Unparsed Items:          " << stats.unparsedCount << "\n";
    std::cout << "  Items with Expected AKA: " << stats.expectedAkaCount << "\n";
    std::cout << "  AKA Matched Items:       " << stats.matchedAkaCount << " / "
              << stats.expectedAkaCount << "\n";
    std::cout << "  AKA Inclusion Rate:      "
              << QString::number(akaInclusionRate * 100.0, 'f', 2).toStdString() << "%\n";
    std::cout << "  Elapsed Time:            " << elapsedMs << " ms\n";
}

void printCorpusReport(const QList<CreditErrorItem> &errors, int totalCount,
    const CorpusEvalStats &stats, double accuracy, double akaInclusionRate, qint64 elapsedMs,
    EvalHarness &harness)
{
    std::cout << "\n========================================\n";
    std::cout << "      ARTIST CREDIT EVALUATION REPORT   \n";
    std::cout << "========================================\n\n";

    printErrorsSection(errors);
    printCorpusSummaryTable(totalCount, stats, accuracy, akaInclusionRate, elapsedMs);
    harness.printLlmUsage();

    std::cout << "========================================\n";
    if (accuracy >= 0.80) {
        std::cout << "RESULT: PASSED (Accuracy >= 80.0%)\n";
    } else {
        std::cout << "RESULT: FAILED (Accuracy < 80.0%)\n";
    }
    std::cout << "========================================\n";
}

std::optional<QStringList> queryLibraryArtistValues(const QSqlDatabase &conn)
{
    QSqlQuery q(conn);
    const QString sql = QStringLiteral("SELECT DISTINCT artist FROM effective_metadata "
                                       "WHERE artist IS NOT NULL AND artist != '' "
                                       "UNION "
                                       "SELECT DISTINCT album_artist FROM effective_metadata "
                                       "WHERE album_artist IS NOT NULL AND album_artist != ''");
    if (!q.exec(sql)) {
        std::cerr << "Failed to query distinct artist values: " << qPrintable(q.lastError().text())
                  << "\n";
        return std::nullopt;
    }

    QStringList allValues;
    while (q.next()) {
        const QString val = q.value(0).toString().trimmed();
        if (!val.isEmpty()) {
            allValues.append(val);
        }
    }
    return allValues;
}

void collectLibraryCreditStats(const QStringList &allValues,
    const QHash<QString, butler::StoredArtistCredit> &credits, int &parsedCount,
    QList<CreditChangedItem> &changedItems, QList<CreditAkaItem> &akaItems)
{
    for (const auto &val : allValues) {
        if (!credits.contains(val)) {
            continue;
        }
        ++parsedCount;

        const auto &stored = credits.value(val);
        const auto &credit = stored.credit;
        const QString norm = butler::normalizedValue(credit);

        if (norm != val) {
            changedItems.append(CreditChangedItem {
                .originalValue = val,
                .normalizedValue = norm,
                .roles = credit.roles,
                .confidence = credit.confidence,
                .reason = credit.reason,
            });
        }

        for (const auto &performer : credit.performers) {
            if (!performer.aka.isEmpty()) {
                akaItems.append(CreditAkaItem {
                    .originalValue = val,
                    .performer = performer.name,
                    .aka = performer.aka,
                });
            }
        }
    }
}

void printLibraryChangedSection(const QList<CreditChangedItem> &changedItems)
{
    std::cout << "=== Changed Values (Count: " << changedItems.size() << ") ===\n";
    if (changedItems.isEmpty()) {
        std::cout << "  (none - all normalized values match original)\n";
        return;
    }
    for (const auto &item : changedItems) {
        std::cout << "  * \"" << item.originalValue.toStdString() << "\" -> \""
                  << item.normalizedValue.toStdString() << "\"";
        if (!item.roles.isEmpty()) {
            std::cout << "  [" << item.roles.join(QStringLiteral(", ")).toStdString() << "]";
        }
        std::cout << " (" << item.confidence << ") " << item.reason.toStdString() << "\n";
    }
}

void printLibraryAkaSection(const QList<CreditAkaItem> &akaItems)
{
    std::cout << "\n=== Performers with AKA (Count: " << akaItems.size() << ") ===\n";
    if (akaItems.isEmpty()) {
        std::cout << "  (none)\n";
        return;
    }
    for (const auto &item : akaItems) {
        std::cout << "  * \"" << item.originalValue.toStdString()
                  << "\": " << item.performer.toStdString() << " -> ["
                  << item.aka.join(QStringLiteral(", ")).toStdString() << "]\n";
    }
}

void printLibrarySummarySection(
    int totalValues, int parsedCount, int changedCount, qint64 elapsedMs, EvalHarness &harness)
{
    std::cout << "\n=== Summary Table ===\n";
    std::cout << "  Total Library Values:    " << totalValues << "\n";
    std::cout << "  Parsed Values:           " << parsedCount << "\n";
    std::cout << "  Changed Values:          " << changedCount << "\n";
    std::cout << "  Elapsed Time:            " << elapsedMs << " ms\n";

    harness.printLlmUsage();
    std::cout << "========================================\n";
}

void printLibraryReport(const QList<CreditChangedItem> &changedItems,
    const QList<CreditAkaItem> &akaItems, int totalValues, int parsedCount, qint64 elapsedMs,
    EvalHarness &harness)
{
    std::cout << "\n========================================\n";
    std::cout << "  ARTIST CREDIT LIBRARY EVALUATION REPORT \n";
    std::cout << "========================================\n\n";

    printLibraryChangedSection(changedItems);
    printLibraryAkaSection(akaItems);
    printLibrarySummarySection(
        totalValues, parsedCount, static_cast<int>(changedItems.size()), elapsedMs, harness);
}

} // namespace

ArtistCreditEval::ArtistCreditEval(EvalConfig config, QObject *parent)
    : QObject(parent)
    , m_harness(std::move(config), this)
{
}

ArtistCreditEval::~ArtistCreditEval() = default;

bool ArtistCreditEval::isLibraryMode() const
{
    return !m_harness.config().libraryPath.isEmpty();
}

bool ArtistCreditEval::init()
{
    if (!isLibraryMode()) {
        if (!loadCorpus()) {
            return false;
        }
    }
    if (!m_harness.init()) {
        return false;
    }
    if (!isLibraryMode()) {
        if (!importCorpusToDb()) {
            return false;
        }
    }
    return true;
}

bool ArtistCreditEval::loadCorpus()
{
    QFile file(m_harness.config().corpusPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "Failed to open corpus file: " << qPrintable(m_harness.config().corpusPath)
                  << " (" << qPrintable(file.errorString()) << ")\n";
        return false;
    }

    QJsonParseError parseError { };
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        std::cerr << "Failed to parse corpus JSON: " << qPrintable(parseError.errorString())
                  << "\n";
        return false;
    }

    const QJsonArray arr = doc.array();
    if (arr.isEmpty()) {
        std::cerr << "Corpus file contains no entries\n";
        return false;
    }

    m_corpus.clear();
    for (const auto &val : arr) {
        CreditCorpusItem item;
        const auto status = parseCorpusEntry(val, item);
        if (status == EntryParseStatus::Error) {
            return false;
        }
        if (status == EntryParseStatus::Success) {
            m_corpus.append(item);
        }
    }

    return true;
}

bool ArtistCreditEval::importCorpusToDb()
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

    for (const auto &item : m_corpus) {
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

        const QString title = QStringLiteral("Track %1").arg(fileCounter);
        const QString album = QStringLiteral("Album %1").arg(fileCounter);
        if (!insertRawTag(conn, trackId, QStringLiteral("TITLE"), title)
            || !insertRawTag(conn, trackId, QStringLiteral("ARTIST"), item.value)
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

    const auto commitRes = tx.commit();
    if (!commitRes.ok()) {
        std::cerr << "Failed to commit corpus import: " << qPrintable(commitRes.error().toString())
                  << "\n";
        return false;
    }

    return true;
}

void ArtistCreditEval::start()
{
    int promptVersion = 0;
    if (const auto promptRes = m_harness.prompts().load(QStringLiteral("cleanup/artist_credit"));
        promptRes.ok()) {
        promptVersion = promptRes.value().version;
    } else {
        std::cerr << "Failed to load cleanup/artist_credit prompt: "
                  << qPrintable(promptRes.error().toString()) << "\n";
        QCoreApplication::exit(2);
        return;
    }

    library::CorrectionStore store(m_harness.db(), m_harness.clock());
    const auto batchRes = store.createBatch(
        library::CorrectionKind::ArtistCredit, QStringLiteral("Artist credit eval batch"));
    if (!batchRes.ok()) {
        std::cerr << "Failed to create correction batch: "
                  << qPrintable(batchRes.error().toString()) << "\n";
        QCoreApplication::exit(2);
        return;
    }
    m_batchId = batchRes.value();

    const butler::ArtistCreditSource source(m_harness.db());
    const auto itemsRes = source.findItems(promptVersion);
    if (!itemsRes.ok()) {
        std::cerr << "Failed to find credit items: " << qPrintable(itemsRes.error().toString())
                  << "\n";
        QCoreApplication::exit(2);
        return;
    }
    const QStringList &items = itemsRes.value();

    QJsonObject params;
    params.insert(QStringLiteral("batchId"), m_batchId);

    const bool started = m_harness.runJob(QStringLiteral("butler.artist_credit"),
        QStringLiteral("Artist credit eval"), items, params, [this]() { evaluateAndFinish(); });
    if (!started) {
        QCoreApplication::exit(2);
    }
}

void ArtistCreditEval::evaluateAndFinish()
{
    if (isLibraryMode()) {
        evaluateLibraryMode();
    } else {
        evaluateCorpusMode();
    }
}

void ArtistCreditEval::evaluateCorpusMode()
{
    const butler::ArtistCreditStore store(m_harness.db(), m_harness.clock());
    auto creditsRes = store.loadAll();
    if (!creditsRes.ok()) {
        std::cerr << "Failed to load artist credits: " << qPrintable(creditsRes.error().toString())
                  << "\n";
        QCoreApplication::exit(2);
        return;
    }
    const auto &credits = creditsRes.value();

    const int totalCount = static_cast<int>(m_corpus.size());
    CorpusEvalStats stats;
    QList<CreditErrorItem> errors;

    for (const auto &item : m_corpus) {
        evaluateSingleCorpusItem(item, credits, stats, errors);
    }

    const double accuracy
        = totalCount > 0 ? (static_cast<double>(stats.correctCount) / totalCount) : 0.0;
    const double akaInclusionRate = stats.expectedAkaCount > 0
        ? (static_cast<double>(stats.matchedAkaCount) / stats.expectedAkaCount)
        : 0.0;

    printCorpusReport(
        errors, totalCount, stats, accuracy, akaInclusionRate, m_harness.elapsedMs(), m_harness);

    const int exitCode = (accuracy >= 0.80) ? 0 : 1;
    QCoreApplication::exit(exitCode);
}

void ArtistCreditEval::evaluateLibraryMode()
{
    auto connRes = m_harness.db().connection();
    if (!connRes.ok()) {
        std::cerr << "Database connection error: " << qPrintable(connRes.error().toString())
                  << "\n";
        QCoreApplication::exit(2);
        return;
    }
    const auto &conn = connRes.value();

    const auto allValuesOpt = queryLibraryArtistValues(conn);
    if (!allValuesOpt.has_value()) {
        QCoreApplication::exit(2);
        return;
    }
    const auto &allValues = *allValuesOpt;

    const butler::ArtistCreditStore store(m_harness.db(), m_harness.clock());
    auto creditsRes = store.loadAll();
    if (!creditsRes.ok()) {
        std::cerr << "Failed to load artist credits: " << qPrintable(creditsRes.error().toString())
                  << "\n";
        QCoreApplication::exit(2);
        return;
    }
    const auto &credits = creditsRes.value();

    int parsedCount = 0;
    QList<CreditChangedItem> changedItems;
    QList<CreditAkaItem> akaItems;
    collectLibraryCreditStats(allValues, credits, parsedCount, changedItems, akaItems);

    std::ranges::sort(changedItems, [](const CreditChangedItem &a, const CreditChangedItem &b) {
        return a.originalValue < b.originalValue;
    });

    std::ranges::sort(akaItems, [](const CreditAkaItem &a, const CreditAkaItem &b) {
        if (a.originalValue != b.originalValue) {
            return a.originalValue < b.originalValue;
        }
        return a.performer < b.performer;
    });

    printLibraryReport(changedItems, akaItems, static_cast<int>(allValues.size()), parsedCount,
        m_harness.elapsedMs(), m_harness);

    QCoreApplication::exit(0);
}

} // namespace linernotes::eval
