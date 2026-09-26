// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QByteArray>
#include <QSet>
#include <QString>

#include <core/Result.h>

#include <array>

namespace linernotes::library {

/// 封面缩略图缓存（磁盘）。线程安全：所有方法可在任意线程并发调用（只做文件操作，不碰数据库）。
class CoverStore {
public:
    static constexpr std::array<int, 3> kSizes { 128, 512, 1024 }; // 最长边像素
    explicit CoverStore(QString cacheDir); // 例如 Paths::cacheDir() + "/covers"

    struct Info {
        QString hash;
        QString mime;
        int width = 0;
        int height = 0;
    };

    /// 计算 hash（"v1:" + Blake2b-160 十六进制，同 FileFingerprint 的风格），若该 hash
    /// 的缩略图不全则生成。 图片无法解码 → Error{errc::kCoverDecode}。
    core::Result<Info> ingest(const QByteArray &imageData);

    /// 返回不大于 size 的最合适缩略图路径（不存在返回空串）。
    QString thumbnailPath(const QString &hash, int size) const;

    /// 删除不在 keep 中的缩略图（清理孤儿），返回删除的 hash 数。
    int prune(const QSet<QString> &keep);

    QString cacheDir() const { return m_cacheDir; }

private:
    QString m_cacheDir;
};

} // namespace linernotes::library
