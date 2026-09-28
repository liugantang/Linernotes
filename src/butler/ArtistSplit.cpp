// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistSplit.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringView>
#include <QtGlobal>

#include <algorithm>
#include <array>

namespace linernotes::butler {

namespace {

enum class SepType : std::uint8_t { Strong, Weak };

struct SepMatch {
    qsizetype start = 0;
    qsizetype length = 0;
    SepType type = SepType::Weak;
};

struct SeparatorRule {
    QStringView text;
    SepType type = SepType::Weak;
    bool requireSurroundingSpaces = false;
    bool requireLeadingNonAlnum = false;
    bool requireTrailingNonAlnum = false;
    bool requireTrailingSpaceAndAbsorb = false;
    bool caseSensitive = false;
};

const auto &separatorRules()
{
    static const std::array<SeparatorRule, 17> s_rules = { {
        // featuring / feat. / feat / ft.
        { .text = QStringView(u"featuring"),
            .type = SepType::Strong,
            .requireLeadingNonAlnum = true,
            .requireTrailingNonAlnum = true },
        { .text = QStringView(u"feat."), .type = SepType::Strong, .requireLeadingNonAlnum = true },
        { .text = QStringView(u"feat"),
            .type = SepType::Strong,
            .requireLeadingNonAlnum = true,
            .requireTrailingSpaceAndAbsorb = true },
        { .text = QStringView(u"ft."), .type = SepType::Strong, .requireLeadingNonAlnum = true },
        // x / × / 、
        { .text = QStringView(u"×"), .type = SepType::Strong },
        { .text = QStringView(u"、"), .type = SepType::Strong },
        { .text = QStringView(u"x"),
            .type = SepType::Strong,
            .requireSurroundingSpaces = true,
            .caseSensitive = true },
        // vs. / vs
        { .text = QStringView(u"vs."), .type = SepType::Strong, .requireSurroundingSpaces = true },
        { .text = QStringView(u"vs"), .type = SepType::Strong, .requireSurroundingSpaces = true },
        // and / with
        { .text = QStringView(u"and"), .type = SepType::Weak, .requireSurroundingSpaces = true },
        { .text = QStringView(u"with"), .type = SepType::Weak, .requireSurroundingSpaces = true },
        // single char weak seps
        { .text = QStringView(u","), .type = SepType::Weak },
        { .text = QStringView(u"，"), .type = SepType::Weak },
        { .text = QStringView(u"&"), .type = SepType::Weak },
        { .text = QStringView(u"+"), .type = SepType::Weak },
        { .text = QStringView(u"/"), .type = SepType::Weak },
        { .text = QStringView(u"／"), .type = SepType::Weak },
    } };
    return s_rules;
}

bool containsCv(const QString &val)
{
    static const char *s_cvStr = R"((?:\(|（|\b)cv(?::|\.|\s|\)|$))";
    static const QRegularExpression s_cvRegex(
        QString::fromUtf8(s_cvStr), QRegularExpression::CaseInsensitiveOption);
    return val.contains(s_cvRegex);
}

inline bool isOpeningBracket(QChar c)
{
    return c == QLatin1Char('(') || c == QChar(0xFF08) || c == QLatin1Char('[')
        || c == QChar(0x3010);
}

inline bool isClosingBracket(QChar c)
{
    return c == QLatin1Char(')') || c == QChar(0xFF09) || c == QLatin1Char(']')
        || c == QChar(0x3011);
}

bool isPureDigits(const QString &str)
{
    const QString trimmed = str.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    return std::ranges::all_of(trimmed, [](QChar c) { return c.isDigit(); });
}

bool isKnownArtist(const QString &part, const QSet<QString> &knownArtists)
{
    if (knownArtists.contains(part)) {
        return true;
    }
    return std::ranges::any_of(
        knownArtists, [&](const QString &k) { return k.compare(part, Qt::CaseInsensitive) == 0; });
}

bool checkRule(QStringView s, qsizetype i, const SeparatorRule &rule, SepMatch &match)
{
    const auto rem = s.mid(i);
    const auto cs = rule.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (!rem.startsWith(rule.text, cs)) {
        return false;
    }

    const qsizetype ruleLen = rule.text.length();
    const qsizetype totalLen = s.length();

    if (rule.requireSurroundingSpaces) {
        if (i == 0 || !s.at(i - 1).isSpace() || i + ruleLen >= totalLen
            || !s.at(i + ruleLen).isSpace()) {
            return false;
        }
    }

    if (rule.requireLeadingNonAlnum) {
        if (i > 0 && s.at(i - 1).isLetterOrNumber()) {
            return false;
        }
    }

    if (rule.requireTrailingNonAlnum) {
        if (i + ruleLen < totalLen && s.at(i + ruleLen).isLetterOrNumber()) {
            return false;
        }
    }

    if (rule.requireTrailingSpaceAndAbsorb) {
        if (i + ruleLen >= totalLen || !s.at(i + ruleLen).isSpace()) {
            return false;
        }
        qsizetype lenWithSpaces = ruleLen;
        while (i + lenWithSpaces < totalLen && s.at(i + lenWithSpaces).isSpace()) {
            ++lenWithSpaces;
        }
        match = SepMatch {
            .start = i,
            .length = lenWithSpaces,
            .type = rule.type,
        };
        return true;
    }

    match = SepMatch {
        .start = i,
        .length = ruleLen,
        .type = rule.type,
    };
    return true;
}

bool matchSeparatorAt(const QString &s, qsizetype i, SepMatch &match)
{
    const QStringView view(s);
    for (const auto &rule : separatorRules()) {
        if (checkRule(view, i, rule, match)) {
            return true;
        }
    }
    return false;
}

QList<SepMatch> findSeparatorsInSegment(const QString &seg)
{
    QList<SepMatch> seps;
    qsizetype depth = 0;
    qsizetype lastEnd = 0;
    const qsizetype len = seg.length();

    for (qsizetype i = 0; i < len; ++i) {
        const QChar c = seg.at(i);
        if (isOpeningBracket(c)) {
            ++depth;
        } else if (isClosingBracket(c)) {
            if (depth > 0) {
                --depth;
            }
        } else if (depth == 0) {
            SepMatch match;
            if (matchSeparatorAt(seg, i, match)) {
                const QString leftText = seg.mid(lastEnd, match.start - lastEnd).trimmed();
                if (!leftText.isEmpty()) {
                    seps.append(match);
                    lastEnd = match.start + match.length;
                    i = lastEnd - 1;
                }
            }
        }
    }
    return seps;
}

QStringList extractPartsFromSeparators(const QString &seg, const QList<SepMatch> &seps)
{
    QStringList rawParts;
    qsizetype prev = 0;
    for (const auto &sep : seps) {
        const QString part = seg.mid(prev, sep.start - prev).trimmed();
        if (!part.isEmpty()) {
            rawParts.append(part);
        }
        prev = sep.start + sep.length;
    }
    const QString lastPart = seg.mid(prev).trimmed();
    if (!lastPart.isEmpty()) {
        rawParts.append(lastPart);
    }

    QStringList parts;
    for (const auto &p : rawParts) {
        if (!p.isEmpty() && !parts.contains(p)) {
            parts.append(p);
        }
    }
    return parts;
}

SplitDecision classifySeparators(
    const QStringList &parts, const QList<SepMatch> &seps, const QSet<QString> &knownArtists)
{
    const bool allPartsKnown = !knownArtists.isEmpty()
        && std::ranges::all_of(
            parts, [&](const QString &p) { return isKnownArtist(p, knownArtists); });

    if (allPartsKnown) {
        return SplitDecision {
            .verdict = SplitVerdict::Split,
            .parts = parts,
            .confidence = 0.9,
            .reason = QString::fromUtf8(
                QT_TRANSLATE_NOOP("butler", "All parts are known artists in the library")),
        };
    }

    const bool hasWeak
        = std::ranges::any_of(seps, [](const SepMatch &sep) { return sep.type == SepType::Weak; });
    const bool hasStrong = std::ranges::any_of(
        seps, [](const SepMatch &sep) { return sep.type == SepType::Strong; });

    if (!hasWeak && hasStrong) {
        return SplitDecision {
            .verdict = SplitVerdict::Split,
            .parts = parts,
            .confidence = 0.85,
            .reason
            = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "Explicit separator (feat., ×, 、)")),
        };
    }

    return SplitDecision {
        .verdict = SplitVerdict::Ambiguous,
        .parts = parts,
        .confidence = 0.0,
        .reason
        = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "Ambiguous separator; needs AI review")),
    };
}

SplitDecision decideSegment(const QString &seg, const QSet<QString> &knownArtists)
{
    const auto seps = findSeparatorsInSegment(seg);
    if (seps.isEmpty()) {
        return SplitDecision {
            .verdict = SplitVerdict::Keep,
            .parts = { seg.trimmed() },
            .confidence = 0.0,
            .reason = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "No split needed")),
        };
    }

    const QStringList parts = extractPartsFromSeparators(seg, seps);
    if (parts.size() <= 1) {
        return SplitDecision {
            .verdict = SplitVerdict::Keep,
            .parts = { seg.trimmed() },
            .confidence = 0.0,
            .reason = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "No split needed")),
        };
    }

    if (std::ranges::any_of(parts, isPureDigits)) {
        return SplitDecision {
            .verdict = SplitVerdict::Keep,
            .parts = { seg.trimmed() },
            .confidence = 0.0,
            .reason
            = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "Contains a numeric part; not split")),
        };
    }

    if (containsCv(seg)) {
        return SplitDecision {
            .verdict = SplitVerdict::Ambiguous,
            .parts = parts,
            .confidence = 0.0,
            .reason = QString::fromUtf8(
                QT_TRANSLATE_NOOP("butler", "Contains a CV credit; needs AI review")),
        };
    }

    return classifySeparators(parts, seps, knownArtists);
}

QStringList splitTopLevelSlash(const QString &value)
{
    QStringList segments;
    qsizetype start = 0;
    qsizetype depth = 0;
    const qsizetype len = value.length();
    for (qsizetype i = 0; i < len; ++i) {
        const QChar c = value.at(i);
        if (isOpeningBracket(c)) {
            ++depth;
        } else if (isClosingBracket(c)) {
            if (depth > 0) {
                --depth;
            }
        } else if (depth == 0 && i + 3 <= len && value.mid(i, 3) == QStringLiteral(" / ")) {
            segments.append(value.mid(start, i - start).trimmed());
            start = i + 3;
            i += 2;
        }
    }
    segments.append(value.mid(start).trimmed());
    return segments;
}

SplitDecision combineSegmentDecisions(
    const QString &original, const QStringList &segments, const QSet<QString> &knownArtists)
{
    QStringList combinedParts;
    bool anyAmbiguous = false;
    bool anySplit = false;
    double maxConfidence = 0.0;
    QString mainReason;

    for (const auto &seg : segments) {
        const auto dec = decideSegment(seg, knownArtists);
        for (const auto &p : dec.parts) {
            if (!p.isEmpty() && !combinedParts.contains(p)) {
                combinedParts.append(p);
            }
        }
        if (dec.verdict == SplitVerdict::Ambiguous) {
            anyAmbiguous = true;
        } else if (dec.verdict == SplitVerdict::Split) {
            anySplit = true;
            maxConfidence = std::max(maxConfidence, dec.confidence);
        }
        if (dec.verdict != SplitVerdict::Keep && mainReason.isEmpty()) {
            mainReason = dec.reason;
        }
    }

    if (combinedParts.size() <= 1 || combinedParts == QStringList { original }) {
        return SplitDecision {
            .verdict = SplitVerdict::Keep,
            .parts = { original },
            .confidence = 0.0,
            .reason = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "No split needed")),
        };
    }

    if (anyAmbiguous) {
        return SplitDecision {
            .verdict = SplitVerdict::Ambiguous,
            .parts = combinedParts,
            .confidence = 0.0,
            .reason = mainReason.isEmpty() ? QString::fromUtf8(QT_TRANSLATE_NOOP(
                                                 "butler", "Ambiguous separator; needs AI review"))
                                           : mainReason,
        };
    }

    if (anySplit) {
        return SplitDecision {
            .verdict = SplitVerdict::Split,
            .parts = combinedParts,
            .confidence = maxConfidence > 0.0 ? maxConfidence : 0.85,
            .reason = mainReason.isEmpty() ? QString::fromUtf8(QT_TRANSLATE_NOOP(
                                                 "butler", "Explicit separator (feat., ×, 、)"))
                                           : mainReason,
        };
    }

    return SplitDecision {
        .verdict = SplitVerdict::Keep,
        .parts = combinedParts,
        .confidence = 0.0,
        .reason = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "No split needed")),
    };
}

} // namespace

bool hasSeparators(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    const QStringList segments = splitTopLevelSlash(trimmed);
    if (segments.size() > 1) {
        return true;
    }
    return !findSeparatorsInSegment(trimmed).isEmpty();
}

SplitDecision decideSplit(const QString &value, const QSet<QString> &knownArtists)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return SplitDecision {
            .verdict = SplitVerdict::Keep,
            .parts = { },
            .confidence = 0.0,
            .reason = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "No split needed")),
        };
    }

    const QStringList segments = splitTopLevelSlash(trimmed);
    if (segments.size() <= 1) {
        return decideSegment(trimmed, knownArtists);
    }

    return combineSegmentDecisions(trimmed, segments, knownArtists);
}

} // namespace linernotes::butler
