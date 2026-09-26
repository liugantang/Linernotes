// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <core/Result.h>

namespace linernotes::library {

struct RawTag {
    QString tagType; // 'id3v1','id3v2','xiph','ape','mp4','asf','riff','other'
    int priority = 0; // 越小越优先
    QString key; // TagLib PropertyMap 的大写键
    int ordinal = 0; // 多值顺序
    QString value;
    QByteArray rawBytes; // 仅 Latin-1 编码的字段填写；否则为空
    QString rawEncoding; // rawBytes 非空时为 "latin1"
};

struct AudioProperties {
    QString
        container; // 小写："mp3","flac","ogg","opus","mp4","wav","wavpack","ape","asf","aiff","dsf","dff"
    QString
        codec; // 小写："mp3","flac","vorbis","opus","aac","alac","pcm","wavpack","ape","wma","dsd"
    qint64 durationMs = 0;
    int bitrate = 0; // kbps
    int sampleRate = 0;
    int bitDepth = 0; // 无意义的有损格式为 0
    int channels = 0;
};

struct TagReadResult {
    AudioProperties audio;
    QList<RawTag> tags;
    bool hasEmbeddedCover = false;
};

/// 读取一个音频文件的全部标签。可在任意线程并发调用（每次调用独立打开文件，无共享状态）。
/// 文件不存在、无法识别的格式、损坏 → Error{errc::kTagRead 或 errc::kTagUnsupported, message,
/// detail=路径} 不修改文件。
class TagReader {
public:
    static core::Result<TagReadResult> read(const QString &path);
};

} // namespace linernotes::library
