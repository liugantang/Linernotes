// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistMerge.h"

#include <QLatin1StringView>
#include <QSet>

#include <algorithm>

namespace linernotes::butler {

namespace {

bool isSupportedMbLocale(const QString &locale)
{
    if (locale.isEmpty()) {
        return false;
    }
    if (locale == QLatin1StringView("en") || locale == QLatin1StringView("ja")
        || locale == QLatin1StringView("ko") || locale == QLatin1StringView("zh")
        || locale == QLatin1StringView("zh_Hans") || locale == QLatin1StringView("zh_Hant")
        || locale == QLatin1StringView("zh-Hans") || locale == QLatin1StringView("zh-Hant")) {
        return true;
    }
    if (locale.startsWith(QLatin1StringView("en_")) || locale.startsWith(QLatin1StringView("en-"))
        || locale.startsWith(QLatin1StringView("ja_"))
        || locale.startsWith(QLatin1StringView("ja-"))
        || locale.startsWith(QLatin1StringView("ko_"))
        || locale.startsWith(QLatin1StringView("ko-"))
        || locale.startsWith(QLatin1StringView("zh_"))
        || locale.startsWith(QLatin1StringView("zh-"))) {
        return true;
    }
    return false;
}

const MbArtist *findMatchingMbArtist(const ArtistEntry &ours, const QList<MbArtist> &results)
{
    for (const auto &mba : results) {
        if (mba.score < 90) {
            continue;
        }
        if (mba.name == ours.name) {
            return &mba;
        }
        const bool aliasMatch = std::ranges::any_of(
            mba.aliases, [&](const MbAlias &a) { return a.name == ours.name; });
        if (aliasMatch) {
            return &mba;
        }
    }
    return nullptr;
}

bool isBetterCanonical(const ArtistEntry &current, const ArtistEntry &best)
{
    if (current.trackCount != best.trackCount) {
        return current.trackCount > best.trackCount;
    }
    const int cmp = current.name.compare(best.name);
    if (cmp != 0) {
        return cmp < 0;
    }
    return current.artistId < best.artistId;
}

std::optional<QString> findBestLocaleForAlias(
    const QString &aliasName, const QList<MbAlias> &aliases)
{
    std::optional<QString> firstSupported;
    for (const auto &al : aliases) {
        if (al.name == aliasName && isSupportedMbLocale(al.locale)) {
            if (al.primary) {
                return al.locale;
            }
            if (!firstSupported.has_value()) {
                firstSupported = al.locale;
            }
        }
    }
    return firstSupported;
}

QList<ArtistEntry> collectMergeCandidates(const ArtistEntry &ours, const MbArtist &matchedMb,
    const QHash<QString, ArtistEntry> &libraryByName)
{
    QList<ArtistEntry> mergeCandidates = { ours };
    QSet<qint64> seenCandidateIds = { ours.artistId };

    auto checkAddCandidate = [&](const QString &candName) {
        if (candName.isEmpty() || candName == ours.name) {
            return;
        }
        auto it = libraryByName.find(candName);
        if (it != libraryByName.end()) {
            const auto &entry = it.value();
            if (!seenCandidateIds.contains(entry.artistId)) {
                seenCandidateIds.insert(entry.artistId);
                mergeCandidates.append(entry);
            }
        }
    };

    checkAddCandidate(matchedMb.name);
    for (const auto &al : matchedMb.aliases) {
        checkAddCandidate(al.name);
    }
    return mergeCandidates;
}

QList<library::ArtistAliasProposal> buildMergeProposals(qint64 canonicalId,
    const QList<ArtistEntry> &mergeCandidates, const QList<MbAlias> &mbAliases,
    QSet<QString> &emittedAliases)
{
    QList<library::ArtistAliasProposal> proposals;
    const QString mergeReason
        = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "Matched via MusicBrainz"));

    for (const auto &c : mergeCandidates) {
        if (c.artistId == canonicalId) {
            continue;
        }
        const auto locale = findBestLocaleForAlias(c.name, mbAliases);
        proposals.append(library::ArtistAliasProposal {
            .canonicalArtistId = canonicalId,
            .alias = c.name,
            .locale = locale,
            .source = library::CorrectionSource::MusicBrainz,
            .confidence = 0.95,
            .reason = mergeReason,
        });
        emittedAliases.insert(c.name);
    }
    return proposals;
}

QList<library::ArtistAliasProposal> buildLocalizedProposals(qint64 canonicalId,
    const QString &canonicalName, const QList<MbAlias> &mbAliases,
    const QSet<QString> &emittedAliases)
{
    QList<QString> seenLocales;
    QHash<QString, MbAlias> bestAliasPerLocale;

    for (const auto &al : mbAliases) {
        if (!isSupportedMbLocale(al.locale)) {
            continue;
        }
        const QString &loc = al.locale;
        auto it = bestAliasPerLocale.find(loc);
        if (it == bestAliasPerLocale.end()) {
            seenLocales.append(loc);
            bestAliasPerLocale.insert(loc, al);
        } else if (!it.value().primary && al.primary) {
            it.value() = al;
        }
    }

    QList<library::ArtistAliasProposal> proposals;
    const QString localeReason
        = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "MusicBrainz localized alias"));

    for (const auto &loc : seenLocales) {
        const auto &selectedAlias = bestAliasPerLocale.value(loc);
        const QString trimmedName = selectedAlias.name.trimmed();
        if (trimmedName.isEmpty() || trimmedName == canonicalName
            || emittedAliases.contains(trimmedName)) {
            continue;
        }
        proposals.append(library::ArtistAliasProposal {
            .canonicalArtistId = canonicalId,
            .alias = trimmedName,
            .locale = selectedAlias.locale,
            .source = library::CorrectionSource::MusicBrainz,
            .confidence = 0.9,
            .reason = localeReason,
        });
    }
    return proposals;
}

} // namespace

qint64 pickCanonical(const QList<ArtistEntry> &members)
{
    if (members.isEmpty()) {
        return 0;
    }

    const ArtistEntry *best = &members.at(0);
    for (qsizetype i = 1; i < members.size(); ++i) {
        const auto &current = members.at(i);
        if (isBetterCanonical(current, *best)) {
            best = &current;
        }
    }
    return best->artistId;
}

QList<library::ArtistAliasProposal> groupProposals(const ArtistGroup &group)
{
    if (group.members.size() < 2) {
        return { };
    }

    const qint64 canonicalId = pickCanonical(group.members);
    if (canonicalId <= 0) {
        return { };
    }

    const double confidence = 0.95;
    const QString reason
        = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "Same name with different spelling"));

    QList<library::ArtistAliasProposal> proposals;
    proposals.reserve(group.members.size() - 1);

    for (const auto &member : group.members) {
        if (member.artistId == canonicalId) {
            continue;
        }
        proposals.append(library::ArtistAliasProposal {
            .canonicalArtistId = canonicalId,
            .alias = member.name,
            .locale = std::nullopt,
            .source = library::CorrectionSource::Rule,
            .confidence = confidence,
            .reason = reason,
        });
    }

    return proposals;
}

QList<library::ArtistAliasProposal> musicBrainzProposals(const ArtistEntry &ours,
    const QList<MbArtist> &results, const QHash<QString, ArtistEntry> &libraryByName)
{
    const MbArtist *matchedMb = findMatchingMbArtist(ours, results);
    if (matchedMb == nullptr) {
        return { };
    }

    const QList<ArtistEntry> mergeCandidates
        = collectMergeCandidates(ours, *matchedMb, libraryByName);
    const qint64 canonicalId = pickCanonical(mergeCandidates);

    QString canonicalName;
    for (const auto &c : mergeCandidates) {
        if (c.artistId == canonicalId) {
            canonicalName = c.name;
            break;
        }
    }

    QSet<QString> emittedAliases;
    QList<library::ArtistAliasProposal> proposals
        = buildMergeProposals(canonicalId, mergeCandidates, matchedMb->aliases, emittedAliases);

    const auto localizedProposals
        = buildLocalizedProposals(canonicalId, canonicalName, matchedMb->aliases, emittedAliases);
    proposals.append(localizedProposals);

    return proposals;
}

} // namespace linernotes::butler
