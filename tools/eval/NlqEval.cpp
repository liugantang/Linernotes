// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "NlqEval.h"

#include <QCoreApplication>
#include <QDate>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>

#include <library/LibraryEnums.h>
#include <library/SmartRule.h>
#include <nlq/LibrarySummary.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

namespace linernotes::eval {

namespace {

bool numericConditionValueEquivalent(
    const library::SmartCondition &a, const library::SmartCondition &b)
{
    bool ok1 = false;
    bool ok2 = false;
    const double v1 = a.value.toDouble(&ok1);
    const double v2 = b.value.toDouble(&ok2);
    if (!ok1 || !ok2 || std::abs(v1 - v2) > 1e-4) {
        return false;
    }
    if (a.op == library::SmartOp::Between) {
        const double v1Second = a.value2.toDouble(&ok1);
        const double v2Second = b.value2.toDouble(&ok2);
        if (!ok1 || !ok2 || std::abs(v1Second - v2Second) > 1e-4) {
            return false;
        }
    }
    return true;
}

QStringList extractTrimmedList(const QVariant &v)
{
    const QStringList list = library::smartTextValues(v);
    QStringList res;
    res.reserve(list.size());
    for (const auto &item : list) {
        res.append(item.trimmed());
    }
    return res;
}

bool stringListEquivalent(const QStringList &actualList, const QStringList &expectedList)
{
    for (const auto &exp : expectedList) {
        const bool found = std::ranges::any_of(actualList,
            [&exp](const QString &act) { return act.compare(exp, Qt::CaseInsensitive) == 0; });
        if (!found) {
            return false;
        }
    }
    return true;
}

bool textConditionValueEquivalent(
    const library::SmartCondition &a, const library::SmartCondition &b)
{
    const QStringList actualList = extractTrimmedList(a.value);
    const QStringList expectedList = extractTrimmedList(b.value);

    if (!stringListEquivalent(actualList, expectedList)) {
        return false;
    }

    if (a.op == library::SmartOp::Between) {
        const QString s1Second = a.value2.toString().trimmed();
        const QString s2Second = b.value2.toString().trimmed();
        if (s1Second.compare(s2Second, Qt::CaseInsensitive) != 0) {
            return false;
        }
    }
    return true;
}

bool conditionValueEquivalent(const library::SmartCondition &a, const library::SmartCondition &b)
{
    if (a.op == library::SmartOp::IsTrue || a.op == library::SmartOp::IsFalse) {
        return true;
    }

    if (a.op == library::SmartOp::InLastDays || a.op == library::SmartOp::NotInLastDays
        || library::smartFieldKind(a.field) == library::SmartFieldKind::Number) {
        return numericConditionValueEquivalent(a, b);
    }

    return textConditionValueEquivalent(a, b);
}

bool conditionEquivalent(const library::SmartCondition &a, const library::SmartCondition &b)
{
    if (a.field != b.field || a.op != b.op) {
        return false;
    }
    return conditionValueEquivalent(a, b);
}

bool conditionsEquivalent(
    const QList<library::SmartCondition> &actual, const QList<library::SmartCondition> &expected)
{
    if (actual.size() != expected.size()) {
        return false;
    }
    std::vector<bool> matched(static_cast<std::size_t>(expected.size()), false);
    for (const auto &actCond : actual) {
        bool found = false;
        for (std::size_t i = 0; i < matched.size(); ++i) {
            if (!matched.at(i)
                && conditionEquivalent(actCond, expected.at(static_cast<qsizetype>(i)))) {
                matched.at(i) = true;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

nlq::LibrarySummary buildFixedLibrarySummary(const QString &todayStr, const QString &timeZoneId)
{
    nlq::LibrarySummary summary;
    summary.today = QDate::fromString(todayStr, Qt::ISODate);
    summary.timeZoneId = timeZoneId;
    summary.trackCount = 16000;
    summary.albumCount = 2200;
    summary.artistCount = 2200;
    summary.minYear = 1978;
    summary.maxYear = 2026;
    summary.firstPlayed = QDate(2025, 1, 1);
    summary.lastPlayed = QDate(2026, 10, 1);
    summary.playEventCount = 12000;

    summary.topGenres = {
        qMakePair(QStringLiteral("Pop"), 4500),
        qMakePair(QStringLiteral("Rock"), 3200),
        qMakePair(QStringLiteral("Electronic"), 2100),
        qMakePair(QStringLiteral("Jazz"), 1500),
        qMakePair(QStringLiteral("Classical"), 1200),
        qMakePair(QStringLiteral("R&B"), 800),
        qMakePair(QStringLiteral("ACG"), 600),
        qMakePair(QStringLiteral("Folk"), 500),
    };

    summary.languages = {
        qMakePair(library::TrackLanguage::Chinese, 7000),
        qMakePair(library::TrackLanguage::Japanese, 4000),
        qMakePair(library::TrackLanguage::Western, 3500),
        qMakePair(library::TrackLanguage::Korean, 1200),
        qMakePair(library::TrackLanguage::Other, 300),
    };

    summary.versionTypes = {
        qMakePair(library::VersionType::Studio, 14000),
        qMakePair(library::VersionType::Live, 1000),
        qMakePair(library::VersionType::Remaster, 400),
        qMakePair(library::VersionType::Acoustic, 250),
        qMakePair(library::VersionType::Remix, 200),
        qMakePair(library::VersionType::Instrumental, 100),
        qMakePair(library::VersionType::Alternate, 50),
    };

    return summary;
}

std::optional<NlqCorpusItem> parseCorpusItem(const QJsonObject &itemObj, qsizetype idx)
{
    const QString question = itemObj.value(QStringLiteral("question")).toString().trimmed();
    if (question.isEmpty()) {
        std::cerr << "Corpus item at index " << idx << " has empty question\n";
        return std::nullopt;
    }

    std::optional<nlq::Query> previous;
    if (itemObj.contains(QStringLiteral("previous"))
        && !itemObj.value(QStringLiteral("previous")).isNull()) {
        const auto prevVal = itemObj.value(QStringLiteral("previous"));
        if (!prevVal.isObject()) {
            std::cerr << "Corpus item at index " << idx << " has invalid previous object\n";
            return std::nullopt;
        }
        const auto prevRes = nlq::Query::fromJson(prevVal.toObject());
        if (!prevRes.ok()) {
            std::cerr << "Corpus item at index " << idx
                      << " has invalid previous query: " << qPrintable(prevRes.error().toString())
                      << "\n";
            return std::nullopt;
        }
        previous = prevRes.value();
    }

    const QJsonArray expArr = itemObj.value(QStringLiteral("expected")).toArray();
    if (expArr.isEmpty()) {
        std::cerr << "Corpus item at index " << idx << " has empty expected array\n";
        return std::nullopt;
    }

    QList<nlq::Query> expectedQueries;
    expectedQueries.reserve(expArr.size());
    for (const auto &expVal : expArr) {
        if (!expVal.isObject()) {
            std::cerr << "Corpus item at index " << idx << " contains non-object in expected\n";
            return std::nullopt;
        }
        const auto expRes = nlq::Query::fromJson(expVal.toObject());
        if (!expRes.ok()) {
            std::cerr << "Corpus item at index " << idx << " contains invalid expected query: "
                      << qPrintable(expRes.error().toString()) << "\n";
            return std::nullopt;
        }
        expectedQueries.append(expRes.value());
    }

    return NlqCorpusItem {
        .question = question,
        .previous = previous,
        .expected = expectedQueries,
    };
}

void printErrorItem(const NlqErrorItem &err)
{
    std::cout << "  * Question: \"" << err.question.toStdString() << "\"\n";
    std::cout << "    Expected: ";
    if (err.expected.size() == 1) {
        const QJsonDocument doc(err.expected.at(0).toJson());
        std::cout << doc.toJson(QJsonDocument::Compact).toStdString() << "\n";
    } else {
        QJsonArray arr;
        for (const auto &exp : err.expected) {
            arr.append(exp.toJson());
        }
        const QJsonDocument doc(arr);
        std::cout << doc.toJson(QJsonDocument::Compact).toStdString() << "\n";
    }
    if (err.isError) {
        std::cout << "    Actual:   <error: " << err.errorReason.toStdString() << ">\n";
    } else if (err.actual.has_value()) {
        const QJsonDocument doc(err.actual->toJson());
        std::cout << "    Actual:   " << doc.toJson(QJsonDocument::Compact).toStdString() << "\n";
        if (!err.explanation.isEmpty()) {
            std::cout << "    Explanation: " << err.explanation.toStdString() << "\n";
        }
    }
}

void printSummaryReport(int totalCount, int correctCount, int failedCount, qint64 totalElapsedMs,
    qint64 totalPromptTokens, qint64 totalCompletionTokens)
{
    const double accuracy = totalCount > 0 ? (static_cast<double>(correctCount) / totalCount) : 0.0;
    const qint64 avgElapsedMs = totalCount > 0 ? (totalElapsedMs / totalCount) : 0;

    std::cout << "\n=== Summary Table ===\n";
    std::cout << "  Total Items:       " << totalCount << "\n";
    std::cout << "  Correct Items:     " << correctCount << "\n";
    std::cout << "  Accuracy:          " << QString::number(accuracy * 100.0, 'f', 2).toStdString()
              << "%\n";
    std::cout << "  Failed Items:      " << failedCount << "\n";
    std::cout << "  Elapsed Time:      " << totalElapsedMs << " ms\n";
    std::cout << "  Avg Time / Item:   " << avgElapsedMs << " ms\n";
    std::cout << "  Prompt Tokens:     " << totalPromptTokens << "\n";
    std::cout << "  Completion Tokens: " << totalCompletionTokens << "\n";
    std::cout << "  Total Tokens:      " << (totalPromptTokens + totalCompletionTokens) << "\n";
    std::cout << "========================================\n";
    if (accuracy >= 0.85) {
        std::cout << "RESULT: PASSED (Accuracy >= 85.0%)\n";
    } else {
        std::cout << "RESULT: FAILED (Accuracy < 85.0%)\n";
    }
    std::cout << "========================================\n";
}

} // namespace

bool nlqQueryEquivalent(const nlq::Query &actual, const nlq::Query &expected)
{
    if (actual.entity != expected.entity) {
        return false;
    }
    if (actual.rule.match != expected.rule.match) {
        return false;
    }
    if (actual.rule.playedFrom != expected.rule.playedFrom) {
        return false;
    }
    if (actual.rule.playedTo != expected.rule.playedTo) {
        return false;
    }
    if (actual.sortKey != expected.sortKey) {
        return false;
    }
    if (actual.sortKey != nlq::SortKey::Default && actual.sortKey != nlq::SortKey::Random) {
        if (actual.sortOrder != expected.sortOrder) {
            return false;
        }
    }
    if (actual.limit != expected.limit) {
        return false;
    }
    return conditionsEquivalent(actual.rule.conditions, expected.rule.conditions);
}

NlqEval::NlqEval(EvalConfig config, QObject *parent)
    : QObject(parent)
    , m_harness(std::move(config), this)
{
}

NlqEval::~NlqEval() = default;

bool NlqEval::init()
{
    if (!loadCorpus()) {
        return false;
    }
    if (!m_harness.init()) {
        return false;
    }
    return true;
}

bool NlqEval::loadCorpus()
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
    m_corpus.today = root.value(QStringLiteral("today")).toString(QStringLiteral("2026-10-01"));
    m_corpus.timeZone
        = root.value(QStringLiteral("timeZone")).toString(QStringLiteral("Asia/Shanghai"));

    const QJsonArray itemsArr = root.value(QStringLiteral("items")).toArray();
    if (itemsArr.isEmpty()) {
        std::cerr << "Corpus file contains no items\n";
        return false;
    }

    m_corpus.items.clear();
    m_corpus.items.reserve(itemsArr.size());

    for (qsizetype idx = 0; idx < itemsArr.size(); ++idx) {
        const auto &val = itemsArr.at(idx);
        if (!val.isObject()) {
            std::cerr << "Corpus item at index " << idx << " is not an object\n";
            return false;
        }
        const auto parsedOpt = parseCorpusItem(val.toObject(), idx);
        if (!parsedOpt.has_value()) {
            return false;
        }
        m_corpus.items.append(*parsedOpt);
    }

    return true;
}

void NlqEval::start()
{
    const auto summary = buildFixedLibrarySummary(m_corpus.today, m_corpus.timeZone);
    m_librarySummaryText = nlq::renderLibrarySummary(summary);

    m_interpreter = std::make_unique<nlq::Interpreter>(m_harness.llm(), m_harness.prompts(), this);
    connect(
        m_interpreter.get(), &nlq::Interpreter::finished, this, &NlqEval::onInterpreterFinished);

    m_currentIndex = 0;
    m_correctCount = 0;
    m_failedCount = 0;
    m_totalPromptTokens = 0;
    m_totalCompletionTokens = 0;
    m_errors.clear();

    m_totalTimer.start();
    runNextItem();
}

void NlqEval::runNextItem()
{
    if (m_currentIndex >= m_corpus.items.size()) {
        finishEvaluation();
        return;
    }

    const auto &item = m_corpus.items.at(m_currentIndex);
    if (m_harness.config().verbose) {
        std::cout << "[" << (m_currentIndex + 1) << "/" << m_corpus.items.size() << "] "
                  << item.question.toStdString() << "\n";
    }

    m_itemTimer.start();
    m_interpreter->interpret(item.question, m_librarySummaryText, item.previous);
}

void NlqEval::onInterpreterFinished()
{
    const auto usage = m_interpreter->usage();
    m_totalPromptTokens += usage.promptTokens;
    m_totalCompletionTokens += usage.completionTokens;

    const auto &item = m_corpus.items.at(m_currentIndex);
    const auto &res = m_interpreter->result();

    if (!res.ok()) {
        ++m_failedCount;
        m_errors.append(NlqErrorItem {
            .question = item.question,
            .expected = item.expected,
            .actual = std::nullopt,
            .explanation = QString(),
            .errorReason = res.error().toString(),
            .isError = true,
        });
    } else {
        const auto &interp = res.value();
        bool matched = false;
        for (const auto &exp : item.expected) {
            if (nlqQueryEquivalent(interp.query, exp)) {
                matched = true;
                break;
            }
        }
        if (matched) {
            ++m_correctCount;
        } else {
            m_errors.append(NlqErrorItem {
                .question = item.question,
                .expected = item.expected,
                .actual = interp.query,
                .explanation = interp.explanation,
                .errorReason = QString(),
                .isError = false,
            });
        }
    }

    ++m_currentIndex;
    runNextItem();
}

void NlqEval::finishEvaluation()
{
    std::cout << "\n========================================\n";
    std::cout << "      NLQ EVALUATION REPORT             \n";
    std::cout << "========================================\n\n";

    std::cout << "=== Errors (Count: " << m_errors.size() << ") ===\n";
    if (m_errors.isEmpty()) {
        std::cout << "  (none - all queries translated correctly)\n";
    } else {
        for (const auto &err : m_errors) {
            printErrorItem(err);
        }
    }

    const int totalCount = static_cast<int>(m_corpus.items.size());
    printSummaryReport(totalCount, m_correctCount, m_failedCount, m_totalTimer.elapsed(),
        m_totalPromptTokens, m_totalCompletionTokens);

    const double accuracy
        = totalCount > 0 ? (static_cast<double>(m_correctCount) / totalCount) : 0.0;
    const int exitCode = (accuracy >= 0.85) ? 0 : 1;
    QCoreApplication::exit(exitCode);
}

} // namespace linernotes::eval
