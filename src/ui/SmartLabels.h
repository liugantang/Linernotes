// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QVariantList>

#include <library/LibraryEnums.h>
#include <library/SmartRule.h>
#include <nlq/NlqQuery.h>

#include <cstdint>

namespace linernotes::ui {

[[nodiscard]] QString smartFieldLabel(library::SmartField field);
[[nodiscard]] QString smartOpLabel(library::SmartOp op);
[[nodiscard]] QString trackSortKeyLabel(library::TrackSortKey key);
[[nodiscard]] QString versionTypeLabel(library::VersionType type);
[[nodiscard]] QString trackLanguageLabel(library::TrackLanguage lang);
[[nodiscard]] QString nlqSortKeyLabel(nlq::SortKey key);
[[nodiscard]] QString nlqEntityLabel(nlq::Entity entity);

enum class NlqChipKind : std::uint8_t {
    Entity,
    Condition,
    PlayWindow,
    Sort,
    Limit,
};

struct NlqChip {
    NlqChipKind kind = NlqChipKind::Entity;
    int index = 0;
    QString text;
    bool operator==(const NlqChip &) const = default;
};

[[nodiscard]] QList<NlqChip> nlqChips(const nlq::Query &query);
[[nodiscard]] QVariantList nlqChipsToVariantList(const QList<NlqChip> &chips);

} // namespace linernotes::ui
