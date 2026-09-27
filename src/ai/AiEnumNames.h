// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>
#include <QStringView>

#include <ai/AiEnums.h>

#include <optional>

namespace linernotes::ai {

QString purposeName(Purpose purpose);
std::optional<Purpose> purposeFromName(QStringView name);

} // namespace linernotes::ai
