// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SmartLabels.h"

#include <QCoreApplication>

#include <library/EnumNames.h>

namespace linernotes::ui {

QString smartFieldLabel(library::SmartField field)
{
    switch (field) {
    case library::SmartField::Title:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Title");
    case library::SmartField::Artist:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Artist");
    case library::SmartField::Album:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Album");
    case library::SmartField::AlbumArtist:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Album Artist");
    case library::SmartField::Genre:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Genre");
    case library::SmartField::Year:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Year");
    case library::SmartField::Codec:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Codec");
    case library::SmartField::Rating:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Rating");
    case library::SmartField::Favorite:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Favorite");
    case library::SmartField::DateAdded:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Date Added");
    case library::SmartField::DurationSec:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Duration");
    case library::SmartField::PlayCount:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Play count");
    case library::SmartField::SkipCount:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Skip count");
    case library::SmartField::CompletedCount:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Times completed");
    case library::SmartField::LastPlayed:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Last played");
    case library::SmartField::VersionType:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Version");
    case library::SmartField::Language:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Language");
    case library::SmartField::AlbumFavorite:
        return QCoreApplication::translate(
            "linernotes::ui::PlaylistController", "Album is favorite");
    case library::SmartField::ArtistFavorite:
        return QCoreApplication::translate(
            "linernotes::ui::PlaylistController", "Artist is favorite");
    case library::SmartField::AlbumCompletion:
        return QCoreApplication::translate(
            "linernotes::ui::PlaylistController", "Album completion (%)");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString smartOpLabel(library::SmartOp op)
{
    switch (op) {
    case library::SmartOp::Contains:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Contains");
    case library::SmartOp::NotContains:
        return QCoreApplication::translate(
            "linernotes::ui::PlaylistController", "Does Not Contain");
    case library::SmartOp::Is:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Is");
    case library::SmartOp::IsNot:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Is Not");
    case library::SmartOp::StartsWith:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Starts With");
    case library::SmartOp::Equals:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Equals");
    case library::SmartOp::NotEquals:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Does Not Equal");
    case library::SmartOp::Greater:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Greater Than");
    case library::SmartOp::Less:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Less Than");
    case library::SmartOp::Between:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Between");
    case library::SmartOp::IsTrue:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Is True");
    case library::SmartOp::IsFalse:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Is False");
    case library::SmartOp::InLastDays:
        return QCoreApplication::translate(
            "linernotes::ui::PlaylistController", "In the Last (Days)");
    case library::SmartOp::NotInLastDays:
        return QCoreApplication::translate(
            "linernotes::ui::PlaylistController", "Not in the Last (Days)");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString trackSortKeyLabel(library::TrackSortKey key)
{
    switch (key) {
    case library::TrackSortKey::Default:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Default");
    case library::TrackSortKey::Title:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Title");
    case library::TrackSortKey::Artist:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Artist");
    case library::TrackSortKey::Album:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Album");
    case library::TrackSortKey::Year:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Year");
    case library::TrackSortKey::Duration:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Duration");
    case library::TrackSortKey::DateAdded:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Date Added");
    case library::TrackSortKey::PlaylistOrder:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Playlist Order");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString versionTypeLabel(library::VersionType type)
{
    switch (type) {
    case library::VersionType::Studio:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Studio");
    case library::VersionType::Live:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Live");
    case library::VersionType::Remaster:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Remaster");
    case library::VersionType::Acoustic:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Acoustic");
    case library::VersionType::Remix:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Remix");
    case library::VersionType::Demo:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Demo");
    case library::VersionType::Instrumental:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Instrumental");
    case library::VersionType::Edit:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Edit");
    case library::VersionType::Alternate:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Alt. version");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString trackLanguageLabel(library::TrackLanguage lang)
{
    switch (lang) {
    case library::TrackLanguage::Chinese:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Chinese");
    case library::TrackLanguage::Japanese:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Japanese");
    case library::TrackLanguage::Korean:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Korean");
    case library::TrackLanguage::Western:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Western");
    case library::TrackLanguage::Other:
        return QCoreApplication::translate("linernotes::ui::PlaylistController", "Other");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString smartConditionLabel(const library::SmartCondition &c)
{
    const QString fieldStr = smartFieldLabel(c.field);
    const QString opStr = smartOpLabel(c.op);
    const auto kind = library::smartFieldKind(c.field);

    if (kind == library::SmartFieldKind::Bool) {
        return QStringLiteral("%1 %2").arg(fieldStr, opStr);
    }
    if (c.op == library::SmartOp::Between) {
        return QStringLiteral("%1 %2 %3 \u2013 %4")
            .arg(fieldStr, opStr, c.value.toString(), c.value2.toString());
    }
    if (c.field == library::SmartField::VersionType) {
        const auto vt = library::versionTypeFromString(c.value.toString());
        const QString valStr = vt.has_value() ? versionTypeLabel(vt.value()) : c.value.toString();
        return QStringLiteral("%1 %2 %3").arg(fieldStr, opStr, valStr);
    }
    if (c.field == library::SmartField::Language) {
        const auto lang = library::trackLanguageFromString(c.value.toString());
        const QString valStr
            = lang.has_value() ? trackLanguageLabel(lang.value()) : c.value.toString();
        return QStringLiteral("%1 %2 %3").arg(fieldStr, opStr, valStr);
    }
    return QStringLiteral("%1 %2 %3").arg(fieldStr, opStr, c.value.toString());
}

QString smartPlayWindowLabel(const library::SmartRule &rule)
{
    if (rule.playedFrom.has_value() && rule.playedTo.has_value()) {
        return QCoreApplication::translate("linernotes::ui::Nlq", "Play period: %1 \u2013 %2")
            .arg(rule.playedFrom->toString(Qt::ISODate), rule.playedTo->toString(Qt::ISODate));
    }
    if (rule.playedFrom.has_value()) {
        return QCoreApplication::translate("linernotes::ui::Nlq", "Play period: from %1")
            .arg(rule.playedFrom->toString(Qt::ISODate));
    }
    if (rule.playedTo.has_value()) {
        return QCoreApplication::translate("linernotes::ui::Nlq", "Play period: until %1")
            .arg(rule.playedTo->toString(Qt::ISODate));
    }
    return { };
}

QStringList nlqChips(const nlq::Query &query)
{
    QStringList chips;

    for (const auto &c : query.rule.conditions) {
        chips.append(smartConditionLabel(c));
    }

    if (query.rule.playedFrom.has_value() || query.rule.playedTo.has_value()) {
        const QString pw = smartPlayWindowLabel(query.rule);
        if (!pw.isEmpty()) {
            chips.append(pw);
        }
    }

    return chips;
}

} // namespace linernotes::ui
