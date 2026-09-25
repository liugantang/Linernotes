// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

namespace linernotes::test {

/// 基于环境变量 LINERNOTES_TEST_FIXTURES 拼接测试素材路径；环境变量缺失时触发 qFatal。
QString fixturePath(const QString &relative);

} // namespace linernotes::test
