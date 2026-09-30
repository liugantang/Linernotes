// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AlbumInfoLlm.h"

#include <QDate>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

#include <butler/Errors.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace linernotes::butler {

namespace {

struct ParsedField {
    QJsonValue value;
    AlbumInfoEvidence evidence = AlbumInfoEvidence::Path;
    double confidence = 0.0;
    bool valid = false;
};

struct DiscTrackTotalInfo {
    int trackTotal = 0;
    AlbumInfoEvidence evidence = AlbumInfoEvidence::Path;
    double confidence = 0.0;
};

ParsedField parseField(const QJsonValue &fieldVal)
{
    if (!fieldVal.isObject()) {
        return { };
    }
    const QJsonObject obj = fieldVal.toObject();
    if (!obj.contains(QStringLiteral("value")) || obj.value(QStringLiteral("value")).isNull()) {
        return { };
    }
    if (!obj.contains(QStringLiteral("evidence"))
        || !obj.value(QStringLiteral("evidence")).isString()) {
        return { };
    }

    const QString evStr = obj.value(QStringLiteral("evidence")).toString();
    AlbumInfoEvidence evidence = AlbumInfoEvidence::Path;
    if (evStr == QLatin1StringView("path")) {
        evidence = AlbumInfoEvidence::Path;
    } else if (evStr == QLatin1StringView("tags")) {
        evidence = AlbumInfoEvidence::Tags;
    } else if (evStr == QLatin1StringView("knowledge")) {
        evidence = AlbumInfoEvidence::Knowledge;
    } else {
        return { };
    }

    double conf = obj.value(QStringLiteral("confidence")).toDouble(0.0);
    conf = std::clamp(conf, 0.0, 1.0);

    return ParsedField {
        .value = obj.value(QStringLiteral("value")),
        .evidence = evidence,
        .confidence = conf,
        .valid = true,
    };
}

QString reasonForEvidence(AlbumInfoEvidence evidence)
{
    switch (evidence) {
    case AlbumInfoEvidence::Path:
        return QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "From file or folder name"));
    case AlbumInfoEvidence::Tags:
        return QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "From tags of other tracks"));
    case AlbumInfoEvidence::Knowledge:
        return QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "From model knowledge, unverified"));
    }
    return QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "From model knowledge, unverified"));
}

void addProposal(AlbumInfoProposals &proposals, qint64 trackId, library::TagField field,
    const QString &newValue, AlbumInfoEvidence evidence, double confidence)
{
    library::CorrectionProposal prop {
        .trackId = trackId,
        .field = field,
        .oldValue = std::nullopt,
        .newValue = newValue,
        .source = library::CorrectionSource::Llm,
        .confidence = confidence,
        .reason = reasonForEvidence(evidence),
    };
    if (evidence == AlbumInfoEvidence::Knowledge) {
        proposals.fromKnowledge.append(std::move(prop));
    } else {
        proposals.fromEvidence.append(std::move(prop));
    }
}

std::optional<int> parsePositiveInt(const QJsonValue &val)
{
    int num = 0;
    if (val.isDouble()) {
        num = val.toInt();
    } else if (val.isString()) {
        bool ok = false;
        num = val.toString().toInt(&ok);
        if (!ok) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    if (num < 1) {
        return std::nullopt;
    }
    return num;
}

QStringList formatExistingFields(const AlbumInfoTrack &t)
{
    QStringList existing;
    if (t.titleUnusable) {
        existing.append(QStringLiteral("title=\"%1\" (unusable)").arg(t.title));
    } else if (!t.title.trimmed().isEmpty()) {
        existing.append(QStringLiteral("title=%1").arg(t.title));
    }
    if (!t.artist.trimmed().isEmpty()) {
        existing.append(QStringLiteral("artist=%1").arg(t.artist));
    }
    if (!t.albumArtist.trimmed().isEmpty()) {
        existing.append(QStringLiteral("albumArtist=%1").arg(t.albumArtist));
    }
    if (t.year.has_value() && *t.year > 0) {
        existing.append(QStringLiteral("year=%1").arg(*t.year));
    }
    if (t.trackNumber.has_value() && *t.trackNumber > 0) {
        existing.append(QStringLiteral("trackNumber=%1").arg(*t.trackNumber));
    }
    if (t.discNumber.has_value() && *t.discNumber > 0) {
        existing.append(QStringLiteral("discNumber=%1").arg(*t.discNumber));
    }
    if (t.trackTotal.has_value() && *t.trackTotal > 0) {
        existing.append(QStringLiteral("trackTotal=%1").arg(*t.trackTotal));
    }
    if (t.discTotal.has_value() && *t.discTotal > 0) {
        existing.append(QStringLiteral("discTotal=%1").arg(*t.discTotal));
    }
    return existing;
}

QStringList formatTrackMissingFields(const AlbumInfoTrack &t)
{
    QStringList missing;
    if (t.titleUnusable) {
        missing.append(QStringLiteral("title"));
    }
    if (t.artist.trimmed().isEmpty()) {
        missing.append(QStringLiteral("artist"));
    }
    if (!t.trackNumber.has_value() || *t.trackNumber <= 0) {
        missing.append(QStringLiteral("trackNumber"));
    }
    if (!t.discNumber.has_value() || *t.discNumber <= 0) {
        missing.append(QStringLiteral("discNumber"));
    }
    return missing;
}

QString formatAlbumMissing(const AlbumInfoInput &album)
{
    QStringList missing;

    const bool anyMissingYear = std::ranges::any_of(
        album.tracks, [](const AlbumInfoTrack &t) { return !t.year.has_value() || *t.year <= 0; });
    if (anyMissingYear) {
        missing.append(QStringLiteral("year"));
    }

    const bool anyMissingAlbumArtist = std::ranges::any_of(
        album.tracks, [](const AlbumInfoTrack &t) { return t.albumArtist.trimmed().isEmpty(); });
    if (anyMissingAlbumArtist || album.albumArtist.trimmed().isEmpty()) {
        missing.append(QStringLiteral("albumArtist"));
    }

    const bool anyMissingDiscTotal = std::ranges::any_of(album.tracks,
        [](const AlbumInfoTrack &t) { return !t.discTotal.has_value() || *t.discTotal <= 0; });
    if (anyMissingDiscTotal) {
        missing.append(QStringLiteral("discTotal"));
    }

    QSet<int> discsSet;
    for (const auto &t : album.tracks) {
        discsSet.insert((t.discNumber.has_value() && *t.discNumber > 0) ? *t.discNumber : 1);
    }
    QList<int> discsList(discsSet.begin(), discsSet.end());
    std::ranges::sort(discsList);

    for (const int d : discsList) {
        const bool missingInDisc = std::ranges::any_of(album.tracks, [d](const AlbumInfoTrack &t) {
            const int trackDisc
                = (t.discNumber.has_value() && *t.discNumber > 0) ? *t.discNumber : 1;
            return trackDisc == d && (!t.trackTotal.has_value() || *t.trackTotal <= 0);
        });
        if (missingInDisc) {
            missing.append(QStringLiteral("trackTotal(disc %1)").arg(d));
        }
    }

    return missing.isEmpty() ? QStringLiteral("-") : missing.join(QStringLiteral(", "));
}

QString formatTrackLine(const AlbumInfoTrack &t)
{
    const QStringList existing = formatExistingFields(t);
    const QString existingStr
        = existing.isEmpty() ? QStringLiteral("-") : existing.join(QStringLiteral("; "));

    const QStringList missing = formatTrackMissingFields(t);
    const QString missingStr
        = missing.isEmpty() ? QStringLiteral("-") : missing.join(QStringLiteral(", "));

    return QStringLiteral("%1 | %2 | %3s | %4 | %5")
        .arg(t.trackId)
        .arg(t.relativePath)
        .arg(t.durationMs / 1000)
        .arg(existingStr, missingStr);
}

QString formatAlbumBlock(const AlbumInfoInput &album)
{
    QStringList lines;
    lines.append(QStringLiteral("## Album %1").arg(album.albumId));
    lines.append(QStringLiteral("title: %1 | albumArtist: %2")
            .arg(album.title.isEmpty() ? QStringLiteral("(empty)") : album.title,
                album.albumArtist.isEmpty() ? QStringLiteral("(empty)") : album.albumArtist));
    lines.append(QStringLiteral("directory: %1")
            .arg(album.directory.isEmpty() ? QStringLiteral("(empty)") : album.directory));
    lines.append(QStringLiteral("album missing: %1").arg(formatAlbumMissing(album)));
    lines.append(QStringLiteral("tracks (id | path | duration | existing | missing):"));

    for (const auto &t : album.tracks) {
        lines.append(formatTrackLine(t));
    }

    return lines.join(QLatin1Char('\n'));
}

std::optional<ParsedField> parseAlbumYear(const QJsonObject &albumObj)
{
    auto rawYear = parseField(albumObj.value(QStringLiteral("year")));
    if (!rawYear.valid) {
        return std::nullopt;
    }
    const auto yOpt = parsePositiveInt(rawYear.value);
    const int maxYear = QDate::currentDate().year() + 1;
    if (!yOpt.has_value() || *yOpt < 1900 || *yOpt > maxYear) {
        return std::nullopt;
    }
    return rawYear;
}

std::optional<ParsedField> parseAlbumArtist(const QJsonObject &albumObj)
{
    auto rawAlbumArtist = parseField(albumObj.value(QStringLiteral("albumArtist")));
    if (!rawAlbumArtist.valid || !rawAlbumArtist.value.isString()) {
        return std::nullopt;
    }
    if (rawAlbumArtist.value.toString().trimmed().isEmpty()) {
        return std::nullopt;
    }
    return rawAlbumArtist;
}

std::optional<ParsedField> parseAlbumDiscTotal(
    const QJsonObject &albumObj, const QList<AlbumInfoTrack> &tracks)
{
    auto rawDiscTotal = parseField(albumObj.value(QStringLiteral("discTotal")));
    if (!rawDiscTotal.valid) {
        return std::nullopt;
    }
    const auto dtOpt = parsePositiveInt(rawDiscTotal.value);
    if (!dtOpt.has_value()) {
        return std::nullopt;
    }
    for (const auto &t : tracks) {
        if (t.discNumber.has_value() && *t.discNumber > *dtOpt) {
            return std::nullopt;
        }
    }
    return rawDiscTotal;
}

QHash<int, DiscTrackTotalInfo> parseDiscTrackTotals(const QJsonObject &albumObj)
{
    QHash<int, DiscTrackTotalInfo> discTrackTotals;
    if (!albumObj.contains(QStringLiteral("discs"))
        || !albumObj.value(QStringLiteral("discs")).isArray()) {
        return discTrackTotals;
    }
    const QJsonArray discsArr = albumObj.value(QStringLiteral("discs")).toArray();
    for (const auto &discVal : discsArr) {
        if (!discVal.isObject()) {
            continue;
        }
        const QJsonObject dObj = discVal.toObject();
        const int discNum = dObj.value(QStringLiteral("disc")).toInt(0);
        if (discNum < 1) {
            continue;
        }
        const auto rawTt = parseField(dObj.value(QStringLiteral("trackTotal")));
        if (!rawTt.valid) {
            continue;
        }
        const auto ttOpt = parsePositiveInt(rawTt.value);
        if (!ttOpt.has_value()) {
            continue;
        }
        discTrackTotals.insert(discNum,
            DiscTrackTotalInfo {
                .trackTotal = *ttOpt,
                .evidence = rawTt.evidence,
                .confidence = rawTt.confidence,
            });
    }
    return discTrackTotals;
}

std::optional<ParsedField> determineProposedDiscNumber(const AlbumInfoTrack &t,
    const QHash<qint64, QJsonObject> &trackObjs, const std::optional<ParsedField> &albumDiscTotal)
{
    const bool discNumMissing = !t.discNumber.has_value() || *t.discNumber <= 0;
    if (!discNumMissing || !trackObjs.contains(t.trackId)) {
        return std::nullopt;
    }
    const QJsonObject &tObj = trackObjs.value(t.trackId);
    auto rawDn = parseField(tObj.value(QStringLiteral("discNumber")));
    if (!rawDn.valid) {
        return std::nullopt;
    }
    const auto dnOpt = parsePositiveInt(rawDn.value);
    if (!dnOpt.has_value()) {
        return std::nullopt;
    }
    int effDiscTotal = 0;
    if (albumDiscTotal.has_value()) {
        effDiscTotal = parsePositiveInt(albumDiscTotal->value).value_or(0);
    } else if (t.discTotal.has_value()) {
        effDiscTotal = *t.discTotal;
    }
    if (effDiscTotal > 0 && *dnOpt > effDiscTotal) {
        return std::nullopt;
    }
    return rawDn;
}

int determineEffectiveDisc(
    const AlbumInfoTrack &t, const std::optional<ParsedField> &proposedDiscNumber)
{
    if (proposedDiscNumber.has_value()) {
        return parsePositiveInt(proposedDiscNumber->value).value_or(1);
    }
    if (t.discNumber.has_value() && *t.discNumber > 0) {
        return *t.discNumber;
    }
    return 1;
}

void validateDiscTrackTotalsAgainstTracks(QHash<int, DiscTrackTotalInfo> &discTrackTotals,
    const QList<AlbumInfoTrack> &tracks, const QHash<qint64, QJsonObject> &trackObjs,
    const std::optional<ParsedField> &albumDiscTotal)
{
    for (const auto &t : tracks) {
        const auto proposedDn = determineProposedDiscNumber(t, trackObjs, albumDiscTotal);
        const int disc = determineEffectiveDisc(t, proposedDn);
        if (discTrackTotals.contains(disc) && t.trackNumber.has_value() && *t.trackNumber > 0) {
            if (*t.trackNumber > discTrackTotals.value(disc).trackTotal) {
                discTrackTotals.remove(disc);
            }
        }
    }
}

void processAlbumLevelProposalsForTrack(AlbumInfoProposals &proposals, const AlbumInfoTrack &t,
    const std::optional<ParsedField> &albumArtist, const std::optional<ParsedField> &albumDiscTotal,
    const std::optional<ParsedField> &albumYear)
{
    if (albumArtist.has_value() && t.albumArtist.trimmed().isEmpty()) {
        addProposal(proposals, t.trackId, library::TagField::AlbumArtist,
            albumArtist->value.toString().trimmed(), albumArtist->evidence,
            albumArtist->confidence);
    }

    if (albumDiscTotal.has_value() && (!t.discTotal.has_value() || *t.discTotal <= 0)) {
        const int dt = parsePositiveInt(albumDiscTotal->value).value_or(0);
        if (dt > 0) {
            addProposal(proposals, t.trackId, library::TagField::DiscTotal, QString::number(dt),
                albumDiscTotal->evidence, albumDiscTotal->confidence);
        }
    }

    if (albumYear.has_value() && (!t.year.has_value() || *t.year <= 0)) {
        const int y = parsePositiveInt(albumYear->value).value_or(0);
        if (y >= 1900) {
            addProposal(proposals, t.trackId, library::TagField::Year, QString::number(y),
                albumYear->evidence, albumYear->confidence);
        }
    }
}

void processTrackNumberProposal(AlbumInfoProposals &proposals, const AlbumInfoTrack &t,
    const QJsonObject &tObj, int effDisc, const QHash<int, DiscTrackTotalInfo> &discTrackTotals)
{
    const bool trkNumMissing = !t.trackNumber.has_value() || *t.trackNumber <= 0;
    if (!trkNumMissing) {
        return;
    }
    const auto rawTn = parseField(tObj.value(QStringLiteral("trackNumber")));
    if (!rawTn.valid) {
        return;
    }
    const auto tnOpt = parsePositiveInt(rawTn.value);
    if (!tnOpt.has_value()) {
        return;
    }
    int effTrackTotal = 0;
    if (discTrackTotals.contains(effDisc)) {
        effTrackTotal = discTrackTotals.value(effDisc).trackTotal;
    } else if (t.trackTotal.has_value()) {
        effTrackTotal = *t.trackTotal;
    }
    if (effTrackTotal == 0 || *tnOpt <= effTrackTotal) {
        addProposal(proposals, t.trackId, library::TagField::TrackNumber, QString::number(*tnOpt),
            rawTn.evidence, rawTn.confidence);
    }
}

void processTrackTitleProposal(
    AlbumInfoProposals &proposals, const AlbumInfoTrack &t, const QJsonObject &tObj)
{
    if (!t.titleUnusable) {
        return;
    }
    const auto rawTitle = parseField(tObj.value(QStringLiteral("title")));
    if (rawTitle.valid && rawTitle.value.isString()) {
        const QString titleStr = rawTitle.value.toString().trimmed();
        if (!titleStr.isEmpty()) {
            addProposal(proposals, t.trackId, library::TagField::Title, titleStr, rawTitle.evidence,
                rawTitle.confidence);
        }
    }
}

void processTrackArtistProposal(
    AlbumInfoProposals &proposals, const AlbumInfoTrack &t, const QJsonObject &tObj)
{
    if (!t.artist.trimmed().isEmpty()) {
        return;
    }
    const auto rawArtist = parseField(tObj.value(QStringLiteral("artist")));
    if (rawArtist.valid && rawArtist.value.isString()) {
        const QString artistStr = rawArtist.value.toString().trimmed();
        if (!artistStr.isEmpty()) {
            addProposal(proposals, t.trackId, library::TagField::Artist, artistStr,
                rawArtist.evidence, rawArtist.confidence);
        }
    }
}

void processTrackLevelProposalsForTrack(AlbumInfoProposals &proposals, const AlbumInfoTrack &t,
    const QHash<qint64, QJsonObject> &trackObjs,
    const std::optional<ParsedField> &proposedDiscNumber, int effDisc,
    const QHash<int, DiscTrackTotalInfo> &discTrackTotals)
{
    if (proposedDiscNumber.has_value()) {
        const int dn = parsePositiveInt(proposedDiscNumber->value).value_or(0);
        if (dn > 0) {
            addProposal(proposals, t.trackId, library::TagField::DiscNumber, QString::number(dn),
                proposedDiscNumber->evidence, proposedDiscNumber->confidence);
        }
    }

    if (discTrackTotals.contains(effDisc) && (!t.trackTotal.has_value() || *t.trackTotal <= 0)) {
        const auto &dtt = discTrackTotals.value(effDisc);
        addProposal(proposals, t.trackId, library::TagField::TrackTotal,
            QString::number(dtt.trackTotal), dtt.evidence, dtt.confidence);
    }

    if (!trackObjs.contains(t.trackId)) {
        return;
    }
    const QJsonObject &tObj = trackObjs.value(t.trackId);

    processTrackNumberProposal(proposals, t, tObj, effDisc, discTrackTotals);
    processTrackTitleProposal(proposals, t, tObj);
    processTrackArtistProposal(proposals, t, tObj);
}

void processAlbumProposals(
    AlbumInfoProposals &proposals, const AlbumInfoInput &album, const QJsonObject &albumObj)
{
    const auto albumYear = parseAlbumYear(albumObj);
    const auto albumArtist = parseAlbumArtist(albumObj);
    const auto albumDiscTotal = parseAlbumDiscTotal(albumObj, album.tracks);
    auto discTrackTotals = parseDiscTrackTotals(albumObj);

    QHash<qint64, QJsonObject> trackObjs;
    if (albumObj.contains(QStringLiteral("tracks"))
        && albumObj.value(QStringLiteral("tracks")).isArray()) {
        const QJsonArray tracksArr = albumObj.value(QStringLiteral("tracks")).toArray();
        for (const auto &tVal : tracksArr) {
            if (!tVal.isObject()) {
                continue;
            }
            const QJsonObject tObj = tVal.toObject();
            const qint64 trackId = tObj.value(QStringLiteral("id")).toInteger(0);
            if (trackId > 0) {
                trackObjs.insert(trackId, tObj);
            }
        }
    }

    validateDiscTrackTotalsAgainstTracks(discTrackTotals, album.tracks, trackObjs, albumDiscTotal);

    for (const auto &t : album.tracks) {
        const auto proposedDn = determineProposedDiscNumber(t, trackObjs, albumDiscTotal);
        const int effDisc = determineEffectiveDisc(t, proposedDn);

        processAlbumLevelProposalsForTrack(proposals, t, albumArtist, albumDiscTotal, albumYear);
        processTrackLevelProposalsForTrack(
            proposals, t, trackObjs, proposedDn, effDisc, discTrackTotals);
    }
}

} // namespace

QHash<QString, QString> albumInfoPromptVars(const QList<AlbumInfoInput> &albums)
{
    QHash<QString, QString> vars;
    if (albums.isEmpty()) {
        vars.insert(QStringLiteral("albums"), QStringLiteral("(none)"));
        return vars;
    }

    QStringList albumBlocks;
    albumBlocks.reserve(albums.size());

    for (const auto &album : albums) {
        albumBlocks.append(formatAlbumBlock(album));
    }

    vars.insert(QStringLiteral("albums"), albumBlocks.join(QStringLiteral("\n\n")));
    return vars;
}

QJsonObject albumInfoSchema()
{
    QFile file(QStringLiteral(":/schemas/cleanup/album_info.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

core::Result<AlbumInfoProposals> buildAlbumInfoProposals(
    const QJsonValue &value, const QList<AlbumInfoInput> &albums)
{
    if (!value.isObject()) {
        return core::Error {
            .code = QString(errc::kAlbumInfoInvalidResult),
            .message = QStringLiteral("Expected JSON object for album_info result"),
            .detail = { },
        };
    }

    const QJsonObject root = value.toObject();
    if (!root.contains(QStringLiteral("albums"))
        || !root.value(QStringLiteral("albums")).isArray()) {
        return core::Error {
            .code = QString(errc::kAlbumInfoInvalidResult),
            .message = QStringLiteral("Missing or invalid 'albums' array in album_info result"),
            .detail = { },
        };
    }

    QHash<qint64, const AlbumInfoInput *> albumMap;
    for (const auto &album : albums) {
        albumMap.insert(album.albumId, &album);
    }

    AlbumInfoProposals proposals;
    const QJsonArray albumsArr = root.value(QStringLiteral("albums")).toArray();

    for (const auto &albumVal : albumsArr) {
        if (!albumVal.isObject()) {
            continue;
        }
        const QJsonObject albumObj = albumVal.toObject();
        const qint64 albumId = albumObj.value(QStringLiteral("id")).toInteger(0);
        if (!albumMap.contains(albumId)) {
            continue;
        }

        const auto *inputAlbum = albumMap.value(albumId);
        processAlbumProposals(proposals, *inputAlbum, albumObj);
    }

    return proposals;
}

} // namespace linernotes::butler
