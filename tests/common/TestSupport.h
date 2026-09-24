#pragma once

#include <QString>

namespace aimusic::test {

/// 基于环境变量 AIMUSIC_TEST_FIXTURES 拼接测试素材路径；环境变量缺失时触发 qFatal。
QString fixturePath(const QString &relative);

} // namespace aimusic::test
