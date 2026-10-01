// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SmartLabels.h"

#include <QCoreApplication>
#include <QVariantMap>

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

QString nlqSortKeyLabel(nlq::SortKey key)
{
    switch (key) {
    case nlq::SortKey::Default:
        return QCoreApplication::translate("linernotes::ui::Nlq", "Default");
    case nlq::SortKey::PlayCount:
        return QCoreApplication::translate("linernotes::ui::Nlq", "Play count");
    case nlq::SortKey::LastPlayed:
        return QCoreApplication::translate("linernotes::ui::Nlq", "Last played");
    case nlq::SortKey::Rating:
        return QCoreApplication::translate("linernotes::ui::Nlq", "Rating");
    case nlq::SortKey::Year:
        return QCoreApplication::translate("linernotes::ui::Nlq", "Year");
    case nlq::SortKey::DateAdded:
        return QCoreApplication::translate("linernotes::ui::Nlq", "Date added");
    case nlq::SortKey::Duration:
        return QCoreApplication::translate("linernotes::ui::Nlq", "Duration");
    case nlq::SortKey::Random:
        return QCoreApplication::translate("linernotes::ui::Nlq", "Random");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString nlqEntityLabel(nlq::Entity entity)
{
    switch (entity) {
    case nlq::Entity::Track:
        return QCoreApplication::translate("linernotes::ui::Nlq", "Tracks");
    case nlq::Entity::Album:
        return QCoreApplication::translate("linernotes::ui::Nlq", "Albums");
    case nlq::Entity::Artist:
        return QCoreApplication::translate("linernotes::ui::Nlq", "Artists");
    }
    Q_UNREACHABLE_RETURN(QString());
}

namespace {

QString formatConditionText(const library::SmartCondition &c)
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

} // namespace

QList<NlqChip> nlqChips(const nlq::Query &query)
{
    QList<NlqChip> chips;

    // 1. Entity
    chips.append(NlqChip {
        .kind = NlqChipKind::Entity,
        .index = 0,
        .text = nlqEntityLabel(query.entity),
    });

    // 2. Conditions
    for (int i = 0; i < query.rule.conditions.size(); ++i) {
        const auto &c = query.rule.conditions.at(i);
        chips.append(NlqChip {
            .kind = NlqChipKind::Condition,
            .index = i,
            .text = formatConditionText(c),
        });
    }

    // 3. Play period
    if (query.rule.playedFrom.has_value() || query.rule.playedTo.has_value()) {
        QString periodText;
        if (query.rule.playedFrom.has_value() && query.rule.playedTo.has_value()) {
            periodText
                = QCoreApplication::translate("linernotes::ui::Nlq", "Play period: %1 \u2013 %2")
                      .arg(query.rule.playedFrom->toString(Qt::ISODate),
                          query.rule.playedTo->toString(Qt::ISODate));
        } else if (query.rule.playedFrom.has_value()) {
            periodText = QCoreApplication::translate("linernotes::ui::Nlq", "Play period: from %1")
                             .arg(query.rule.playedFrom->toString(Qt::ISODate));
        } else {
            periodText = QCoreApplication::translate("linernotes::ui::Nlq", "Play period: until %1")
                             .arg(query.rule.playedTo->toString(Qt::ISODate));
        }
        chips.append(NlqChip {
            .kind = NlqChipKind::PlayWindow,
            .index = 0,
            .text = periodText,
        });
    }

    // 4. Sort
    const QString sortKeyName = nlqSortKeyLabel(query.sortKey);
    const QString arrow = (query.sortOrder == Qt::AscendingOrder) ? QStringLiteral("\u2191")
                                                                  : QStringLiteral("\u2193");
    QString sortText;
    if (query.sortKey == nlq::SortKey::Random || query.sortKey == nlq::SortKey::Default) {
        sortText = QCoreApplication::translate("linernotes::ui::Nlq", "Sort: %1").arg(sortKeyName);
    } else {
        sortText = QCoreApplication::translate("linernotes::ui::Nlq", "Sort: %1 %2")
                       .arg(sortKeyName, arrow);
    }
    chips.append(NlqChip {
        .kind = NlqChipKind::Sort,
        .index = 0,
        .text = sortText,
    });

    // 5. Limit
    chips.append(NlqChip {
        .kind = NlqChipKind::Limit,
        .index = 0,
        .text = QCoreApplication::translate("linernotes::ui::Nlq", "Limit: %1").arg(query.limit),
    });

    return chips;
}

QVariantList nlqChipsToVariantList(const QList<NlqChip> &chips)
{
    QVariantList list;
    list.reserve(chips.size());
    for (const auto &chip : chips) {
        QVariantMap map;
        map.insert(QStringLiteral("kind"), static_cast<int>(chip.kind));
        map.insert(QStringLiteral("index"), chip.index);
        map.insert(QStringLiteral("text"), chip.text);
        list.append(map);
    }
    return list;
}

} // namespace linernotes::ui
