// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QChar>
#include <QRegularExpression>

#include <butler/ArtistName.h>
#include <butler/TitleVersion.h>

#include <algorithm>
#include <array>
#include <optional>

namespace linernotes::butler {

namespace {

struct BracketPair {
    QChar left;
    QChar right;
};

constexpr std::array<BracketPair, 6> kBrackets = {
    BracketPair { .left = QLatin1Char('('), .right = QLatin1Char(')') },
    BracketPair { .left = QChar(0xFF08), .right = QChar(0xFF09) }, // （ ）
    BracketPair { .left = QLatin1Char('['), .right = QLatin1Char(']') },
    BracketPair { .left = QChar(0xFF3B), .right = QChar(0xFF3D) }, // ［ ］
    BracketPair { .left = QChar(0x3010), .right = QChar(0x3011) }, // 【 】
    BracketPair { .left = QChar(0x3014), .right = QChar(0x3015) }, // 〔 〕
};

constexpr std::array<QChar, 3> kTildes = {
    QLatin1Char('~'),
    QChar(0x301C), // 〜 WAVE DASH
    QChar(0xFF5E), // ～ FULLWIDTH TILDE
};

constexpr std::array<QChar, 3> kDashes = {
    QLatin1Char('-'),
    QChar(0x2013), // – EN DASH
    QChar(0x2014), // — EM DASH
};

qsizetype findPrecedingWhitespaceStart(const QString &title, qsizetype pos)
{
    qsizetype start = pos;
    while (start > 0 && title.at(start - 1).isSpace()) {
        --start;
    }
    return start;
}

qsizetype findRightTrimmedEnd(const QString &title, qsizetype currentEnd)
{
    qsizetype end = currentEnd;
    while (end > 0 && title.at(end - 1).isSpace()) {
        --end;
    }
    return end;
}

qsizetype findMatchingLeftBracket(
    const QString &title, qsizetype rightPos, QChar leftChar, QChar rightChar)
{
    int depth = 1;
    for (qsizetype i = rightPos - 1; i >= 0; --i) {
        if (title.at(i) == rightChar) {
            ++depth;
        } else if (title.at(i) == leftChar) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return -1;
}

qsizetype findMatchingLeftTilde(const QString &title, qsizetype rightPos, QChar tildeChar)
{
    for (qsizetype i = rightPos - 1; i >= 0; --i) {
        if (title.at(i) == tildeChar) {
            return i;
        }
    }
    return -1;
}

qsizetype findLastDashSeparator(const QString &title, qsizetype effectiveEnd)
{
    for (qsizetype i = effectiveEnd - 1; i >= 1; --i) {
        const QChar ch = title.at(i);
        const bool isDash = std::ranges::any_of(kDashes, [ch](QChar d) { return ch == d; });
        if (isDash && title.at(i - 1).isSpace() && (i + 1 < effectiveEnd)
            && title.at(i + 1).isSpace()) {
            return i;
        }
    }
    return -1;
}

struct PeelCandidate {
    QString innerText { };
    qsizetype start = -1;
    bool valid = false;
};

PeelCandidate tryPeelSuffix(const QString &title, qsizetype currentEnd)
{
    const qsizetype effectiveEnd = findRightTrimmedEnd(title, currentEnd);
    if (effectiveEnd <= 0) {
        return { };
    }

    const QChar lastChar = title.at(effectiveEnd - 1);

    // 1. Check paired brackets
    for (const auto &pair : kBrackets) {
        if (lastChar == pair.right) {
            const qsizetype posL
                = findMatchingLeftBracket(title, effectiveEnd - 1, pair.left, pair.right);
            if (posL < 0) {
                return { };
            }
            const QString inner = title.mid(posL + 1, effectiveEnd - 1 - (posL + 1)).trimmed();
            const qsizetype start = findPrecedingWhitespaceStart(title, posL);
            if (title.first(start).trimmed().isEmpty()) {
                return { };
            }
            return PeelCandidate {
                .innerText = inner,
                .start = start,
                .valid = true,
            };
        }
    }

    // 2. Check paired tildes
    for (const auto tilde : kTildes) {
        if (lastChar == tilde) {
            const qsizetype posL = findMatchingLeftTilde(title, effectiveEnd - 1, tilde);
            if (posL < 0) {
                return { };
            }
            const QString inner = title.mid(posL + 1, effectiveEnd - 1 - (posL + 1)).trimmed();
            const qsizetype start = findPrecedingWhitespaceStart(title, posL);
            if (title.first(start).trimmed().isEmpty()) {
                return { };
            }
            return PeelCandidate {
                .innerText = inner,
                .start = start,
                .valid = true,
            };
        }
    }

    // 3. Check dash separator
    const qsizetype dashPos = findLastDashSeparator(title, effectiveEnd);
    if (dashPos >= 0) {
        const QString inner = title.mid(dashPos + 1, effectiveEnd - (dashPos + 1)).trimmed();
        const qsizetype start = findPrecedingWhitespaceStart(title, dashPos);
        if (title.first(start).trimmed().isEmpty()) {
            return { };
        }
        return PeelCandidate {
            .innerText = inner,
            .start = start,
            .valid = true,
        };
    }

    return { };
}

constexpr int versionPriority(library::VersionType vt)
{
    switch (vt) {
    case library::VersionType::Instrumental:
        return 8;
    case library::VersionType::Live:
        return 7;
    case library::VersionType::Remaster:
        return 6;
    case library::VersionType::Demo:
        return 5;
    case library::VersionType::Acoustic:
        return 4;
    case library::VersionType::Remix:
        return 3;
    case library::VersionType::Edit:
        return 2;
    case library::VersionType::Alternate:
        return 1;
    case library::VersionType::Studio:
        return 0;
    }
    return 0;
}

} // namespace

QList<TitleSuffix> splitSuffixes(const QString &title)
{
    QList<TitleSuffix> result;
    constexpr int kMaxSuffixes = 3;
    qsizetype currentEnd = title.size();

    for (int iter = 0; iter < kMaxSuffixes; ++iter) {
        const PeelCandidate candidate = tryPeelSuffix(title, currentEnd);
        if (!candidate.valid) {
            break;
        }
        if (!candidate.innerText.isEmpty()) {
            result.append(TitleSuffix {
                .text = candidate.innerText,
                .start = candidate.start,
            });
        }
        currentEnd = candidate.start;
    }

    return result;
}

SuffixClass classifySuffix(const QString &suffixText)
{
    const QString norm = suffixText.normalized(QString::NormalizationForm_KC).toLower().trimmed();
    if (norm.isEmpty()) {
        return SuffixClass { .role = SuffixRole::Unknown, .type = library::VersionType::Studio };
    }

    struct Rule {
        QRegularExpression pattern;
        SuffixRole role = SuffixRole::Unknown;
        library::VersionType type = library::VersionType::Studio;
    };

    static const std::array<Rule, 9> s_rules = []() {
        return std::array<Rule, 9> {
            Rule { .pattern = QRegularExpression(QStringLiteral(
                       "instrumental|\\binst(\\.|\\b)|off[ -]?vocal|karaoke|カラオケ|からおけ|伴奏|"
                       "backing[ -]?track")),
                .role = SuffixRole::Version,
                .type = library::VersionType::Instrumental },
            Rule { .pattern
                = QRegularExpression(QStringLiteral("\\blive(#\\d+|\\b)|ライブ|现场|現場")),
                .role = SuffixRole::Version,
                .type = library::VersionType::Live },
            Rule { .pattern = QRegularExpression(QStringLiteral("remaster|リマスター")),
                .role = SuffixRole::Version,
                .type = library::VersionType::Remaster },
            Rule { .pattern = QRegularExpression(QStringLiteral("\\bdemo\\b|デモ")),
                .role = SuffixRole::Version,
                .type = library::VersionType::Demo },
            Rule { .pattern = QRegularExpression(QStringLiteral(
                       "acoustic|unplugged|アコースティック|^(piano|ピアノ)$|(piano|ピアノ).*(ver("
                       "\\.|\\b)|version\\b|arrange|アレンジ)|(ver(\\.|\\b)|version\\b|arrange|"
                       "アレンジ).*(piano|ピアノ)")),
                .role = SuffixRole::Version,
                .type = library::VersionType::Acoustic },
            Rule { .pattern
                = QRegularExpression(QStringLiteral("remix|\\bmix\\b|リミックス|extended|rework")),
                .role = SuffixRole::Version,
                .type = library::VersionType::Remix },
            Rule { .pattern = QRegularExpression(
                       QStringLiteral("tv[ -]?(size|サイズ|ver(\\.|\\b)|version\\b|edit\\b)|radio[ "
                                      "-]?edit\\b|single[ -]?(ver(\\.|\\b)|version\\b)|short[ "
                                      "-]?(ver(\\.|\\b)|version\\b)|ショート|\\bedit\\b")),
                .role = SuffixRole::Version,
                .type = library::VersionType::Edit },
            Rule { .pattern
                = QRegularExpression(QStringLiteral("\\bver(\\.|\\b)|\\bversion\\b|バージョン")),
                .role = SuffixRole::Version,
                .type = library::VersionType::Alternate },
            Rule { .pattern = QRegularExpression(QStringLiteral(
                       "^(feat|ft)(\\.|\\b)|^featuring\\b|bonus[ "
                       "-]?track|ボーナストラック|\\d+\\s*(khz|"
                       "bit)\\b|hi[ -]?res\\b|high[ -]?resolution\\b|^cv[\\.:\\s]|^cv\\b|^from\\b|"
                       "^\\d{4}$")),
                .role = SuffixRole::Annotation,
                .type = library::VersionType::Studio },
        };
    }();

    for (const auto &rule : s_rules) {
        if (rule.pattern.match(norm).hasMatch()) {
            return SuffixClass { .role = rule.role, .type = rule.type };
        }
    }

    return SuffixClass { .role = SuffixRole::Unknown, .type = library::VersionType::Studio };
}

QString suffixKey(const QString &suffixText)
{
    return exactKey(suffixText);
}

TitleVersion resolveTitle(
    const QString &title, const std::function<SuffixClass(const QString &)> &classify)
{
    const QList<TitleSuffix> suffixes = splitSuffixes(title);
    qsizetype peeledStart = -1;
    std::optional<library::VersionType> highestType;
    bool unresolved = false;

    for (const auto &suffix : suffixes) {
        const SuffixClass sc = classify ? classify(suffix.text) : SuffixClass { };
        if (sc.role == SuffixRole::Version) {
            peeledStart = suffix.start;
            if (!highestType.has_value()
                || versionPriority(sc.type) > versionPriority(*highestType)) {
                highestType = sc.type;
            }
        } else if (sc.role == SuffixRole::Annotation) {
            peeledStart = suffix.start;
        } else if (sc.role == SuffixRole::TitlePart) {
            break;
        } else {
            unresolved = true;
            break;
        }
    }

    const QString baseTitle
        = (peeledStart >= 0) ? title.left(peeledStart).trimmed() : title.trimmed();
    const library::VersionType type = highestType.value_or(library::VersionType::Studio);

    return TitleVersion {
        .baseTitle = baseTitle,
        .type = type,
        .unresolved = unresolved,
    };
}

} // namespace linernotes::butler
