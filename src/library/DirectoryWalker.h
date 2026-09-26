// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace linernotes::library {

struct WalkedFile {
    QString path; // 绝对路径
    qint64 size = 0;
    qint64 mtimeMs = 0; // lastModified，UTC 毫秒
};

struct WalkOptions {
    QStringList excludes; // 相对根目录的 glob
    std::function<bool()> isCancelled; // 可为空；每处理一个目录检查一次
};

class DirectoryWalker {
public:
    /// 支持的扩展名（小写，不含点）：mp3 flac ogg oga opus m4a m4b mp4 aac alac wav wv ape wma asf
    /// aif aiff dsf dff mpc tta
    static const QStringList &audioExtensions();
    static bool isAudioFile(const QString &fileName); // 扩展名大小写不敏感

    /// 递归列出 dir 下的音频文件（dir 必须位于 root 之内或等于 root；excludes 相对 root 解释）。
    /// 结果按路径排序。遍历中途被取消 → 返回已收集的部分并置 *cancelled = true（可为空指针）。
    static QList<WalkedFile> walk(const QString &root, const QString &dir,
        const WalkOptions &options, bool *cancelled = nullptr);
};

} // namespace linernotes::library
