// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

namespace linernotes::core {

class Paths {
public:
    /// 使用系统标准位置（QStandardPaths：AppConfigLocation / AppDataLocation / CacheLocation）
    static Paths standard();
    /// 所有目录都放在 root
    /// 下（root/config、root/data、root/cache、root/logs）。用于测试和便携模式。
    static Paths underRoot(const QString &root);
    /// 若环境变量 LINERNOTES_HOME 非空则 underRoot(它)，否则 standard()
    static Paths fromEnvironment();

    QString configDir() const;
    QString dataDir() const;
    QString cacheDir() const;
    QString logDir() const; // standard() 下为 dataDir()/logs

    /// 创建全部目录；失败返回 false 并通过 lcCore 输出警告
    bool ensureCreated() const;

private:
    QString m_configDir;
    QString m_dataDir;
    QString m_cacheDir;
    QString m_logDir;
};

} // namespace linernotes::core
