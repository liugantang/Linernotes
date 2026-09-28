// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

namespace linernotes::core {
struct Error;
}

namespace linernotes::ui {

/// 把错误转成给用户看的文案（已翻译）。Error::message / detail 不出现在返回值里。
[[nodiscard]] QString userErrorText(const core::Error &error);

} // namespace linernotes::ui
