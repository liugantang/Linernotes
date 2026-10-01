// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>
#include <QStringList>

#include <library/LibraryEnums.h>
#include <library/SmartRule.h>
#include <nlq/NlqQuery.h>

namespace linernotes::ui {

[[nodiscard]] QString smartFieldLabel(library::SmartField field);
[[nodiscard]] QString smartOpLabel(library::SmartOp op);
[[nodiscard]] QString trackSortKeyLabel(library::TrackSortKey key);
[[nodiscard]] QString versionTypeLabel(library::VersionType type);
[[nodiscard]] QString trackLanguageLabel(library::TrackLanguage lang);
[[nodiscard]] QString smartConditionLabel(const library::SmartCondition &cond);
[[nodiscard]] QString smartPlayWindowLabel(const library::SmartRule &rule);

[[nodiscard]] QStringList nlqChips(const nlq::Query &query);

} // namespace linernotes::ui
