// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <core/Result.h>
#include <library/LibraryEnums.h>

#include <optional>

namespace linernotes::library {

/// 一个文件全部标签的快照（按标签类型分别保存），用于撤销。
struct TagSnapshot {
    struct Block {
        QString type { }; // "id3v2" / "id3v1" / "ape" / "xiph" / "mp4"
        QMap<QString, QStringList> properties; // TagLib PropertyMap 原样（键大写）
        QStringList unsupported; // PropertyMap::unsupportedData()，只记录不还原
        int id3v2Version = 0; // 仅 id3v2：3 或 4
        bool operator==(const Block &) const = default;
    };
    QList<Block> blocks; // 文件中实际存在的标签；不存在的标签类型不出现
    bool operator==(const TagSnapshot &) const = default;
    QJsonObject toJson() const;
    static std::optional<TagSnapshot> fromJson(const QJsonObject &obj);
};

class TagWriter {
public:
    /// 支持写回的格式：FLAC（xiph）、MP3（id3v2；若文件原本还有 id3v1 /
    /// ape，也记入快照）、M4A/MP4（mp4）、Ogg Vorbis 与 Opus（xiph）。 其他格式 →
    /// Error{errc::kTagWriteUnsupported}。
    static bool isSupported(const QString &path);
    static core::Result<TagSnapshot> snapshot(const QString &path);

    /// 在主标签（MP3 为 id3v2，FLAC/Ogg 为 xiph，M4A 为 mp4）上修改字段：以当前 PropertyMap
    /// 为基础，只改传入的字段，其他键保持不变 （setProperties 前后 unsupportedData 应不丢失——TagLib
    /// 的 setProperties 不会删除 unsupported 数据，写测试确认）。 值为空串 =
    /// 删除该字段。字段到键的映射：
    ///   Title→TITLE、Artist→ARTIST、Album→ALBUM、AlbumArtist→ALBUMARTIST、Genre→GENRE、Composer→COMPOSER、Year→DATE；
    ///   TrackNumber/TrackTotal：xiph 用 TRACKNUMBER + TRACKTOTAL 两个键；id3v2 / mp4 合成一个
    ///   TRACKNUMBER "n/total"（没有 total 时 "n"）； DiscNumber/DiscTotal 同理（DISCNUMBER /
    ///   DISCTOTAL）。只改 number 或只改 total 时，另一半取文件当前值。
    /// MP3：保存时保持原 id3v2 版本（原来是 2.3 就用 2.3 保存，没有 id3v2 时用 2.4）；id3v1、ape
    /// 标签原样保留、不修改。 写完立刻重新打开读回主标签，被修改的键与期望不符 →
    /// Error{errc::kTagWriteVerifyFailed}（调用方负责用快照还原）。
    static core::Result<void> writeFields(
        const QString &path, const QHash<TagField, QString> &values);

    /// 把文件标签恢复成快照：每个 Block 用 setProperties 写回对应标签（id3v2 按快照版本保存）；
    /// 快照中不存在、但文件现在存在的标签类型删除（MPEG::File::strip 等）。还原后再 snapshot
    /// 一次，与传入快照不相等 → kTagWriteVerifyFailed。
    static core::Result<void> restore(const QString &path, const TagSnapshot &snapshot);
};

} // namespace linernotes::library
