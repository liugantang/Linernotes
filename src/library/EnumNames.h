// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QMetaEnum>
#include <QString>
#include <QStringView>

#include <library/ArtistNamePreference.h>
#include <library/LibraryEnums.h>

#include <optional>

namespace linernotes::library {

namespace detail {

/// 按名字反查枚举值：遍历 Q_ENUM_NS / Q_ENUM 登记的全部取值，与名字函数比对，保证两个方向一致。
template <typename E, typename NameFunc>
std::optional<E> enumFromName(QStringView name, NameFunc nameOf)
{
    const QMetaEnum meta = QMetaEnum::fromType<E>();
    for (int i = 0; i < meta.keyCount(); ++i) {
        const auto value = static_cast<E>(meta.value(i));
        if (nameOf(value) == name) {
            return value;
        }
    }
    return std::nullopt;
}

} // namespace detail

inline QString tagFieldToColumn(TagField field)
{
    switch (field) {
    case TagField::Title:
        return QStringLiteral("title");
    case TagField::Artist:
        return QStringLiteral("artist");
    case TagField::Album:
        return QStringLiteral("album");
    case TagField::AlbumArtist:
        return QStringLiteral("album_artist");
    case TagField::Genre:
        return QStringLiteral("genre");
    case TagField::Composer:
        return QStringLiteral("composer");
    case TagField::Year:
        return QStringLiteral("year");
    case TagField::TrackNumber:
        return QStringLiteral("track_number");
    case TagField::TrackTotal:
        return QStringLiteral("track_total");
    case TagField::DiscNumber:
        return QStringLiteral("disc_number");
    case TagField::DiscTotal:
        return QStringLiteral("disc_total");
    }
    return { };
}

inline std::optional<TagField> tagFieldFromColumn(QStringView name)
{
    return detail::enumFromName<TagField>(name, tagFieldToColumn);
}

inline QString correctionKindToString(CorrectionKind kind)
{
    switch (kind) {
    case CorrectionKind::Manual:
        return QStringLiteral("manual");
    case CorrectionKind::Mojibake:
        return QStringLiteral("mojibake");
    case CorrectionKind::ArtistSplit:
        return QStringLiteral("artist_split");
    case CorrectionKind::ArtistMerge:
        return QStringLiteral("artist_merge");
    case CorrectionKind::ArtistCredit:
        return QStringLiteral("artist_credit");
    case CorrectionKind::MbMatch:
        return QStringLiteral("mb_match");
    }
    return { };
}

inline std::optional<CorrectionKind> correctionKindFromString(QStringView name)
{
    return detail::enumFromName<CorrectionKind>(name, correctionKindToString);
}

inline QString correctionSourceToString(CorrectionSource source)
{
    switch (source) {
    case CorrectionSource::Rule:
        return QStringLiteral("rule");
    case CorrectionSource::Llm:
        return QStringLiteral("llm");
    case CorrectionSource::MusicBrainz:
        return QStringLiteral("musicbrainz");
    case CorrectionSource::User:
        return QStringLiteral("user");
    }
    return { };
}

inline std::optional<CorrectionSource> correctionSourceFromString(QStringView name)
{
    return detail::enumFromName<CorrectionSource>(name, correctionSourceToString);
}

inline QString correctionStatusToString(CorrectionStatus status)
{
    switch (status) {
    case CorrectionStatus::Pending:
        return QStringLiteral("pending");
    case CorrectionStatus::Accepted:
        return QStringLiteral("accepted");
    case CorrectionStatus::Rejected:
        return QStringLiteral("rejected");
    case CorrectionStatus::Reverted:
        return QStringLiteral("reverted");
    }
    return { };
}

inline std::optional<CorrectionStatus> correctionStatusFromString(QStringView name)
{
    return detail::enumFromName<CorrectionStatus>(name, correctionStatusToString);
}

inline QString trackIssueKindToString(TrackIssueKind kind)
{
    switch (kind) {
    case TrackIssueKind::NeedsOnlineLookup:
        return QStringLiteral("needs_online");
    }
    return { };
}

inline std::optional<TrackIssueKind> trackIssueKindFromString(QStringView name)
{
    return detail::enumFromName<TrackIssueKind>(name, trackIssueKindToString);
}

inline QString artistNamePreferenceToString(ArtistNamePreference pref)
{
    switch (pref) {
    case ArtistNamePreference::Original:
        return QStringLiteral("original");
    case ArtistNamePreference::SimplifiedChinese:
        return QStringLiteral("simplified_chinese");
    case ArtistNamePreference::English:
        return QStringLiteral("english");
    }
    return { };
}

inline std::optional<ArtistNamePreference> artistNamePreferenceFromString(QStringView name)
{
    return detail::enumFromName<ArtistNamePreference>(name, artistNamePreferenceToString);
}

inline QString versionTypeToString(VersionType type)
{
    switch (type) {
    case VersionType::Studio:
        return QStringLiteral("studio");
    case VersionType::Live:
        return QStringLiteral("live");
    case VersionType::Remaster:
        return QStringLiteral("remaster");
    case VersionType::Acoustic:
        return QStringLiteral("acoustic");
    case VersionType::Remix:
        return QStringLiteral("remix");
    case VersionType::Demo:
        return QStringLiteral("demo");
    case VersionType::Instrumental:
        return QStringLiteral("instrumental");
    case VersionType::Edit:
        return QStringLiteral("edit");
    case VersionType::Alternate:
        return QStringLiteral("alternate");
    }
    return { };
}

inline std::optional<VersionType> versionTypeFromString(QStringView name)
{
    return detail::enumFromName<VersionType>(name, versionTypeToString);
}

} // namespace linernotes::library
