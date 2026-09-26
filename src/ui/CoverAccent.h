// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QColor>
#include <QImage>

namespace linernotes::ui {

/// 根据封面图像提取主强调色。
/// 若图像无效或有效彩色像素不足 5%，返回无效 QColor()。
QColor coverAccentColor(const QImage &image);

} // namespace linernotes::ui
