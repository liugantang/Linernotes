// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <core/Result.h>
#include <guide/LrcParser.h>

namespace linernotes::guide {

// 需要发给模型的行：ParsedLyrics 中 text trim 后非空的行，按出现顺序；返回它们在 lines 中的下标
[[nodiscard]] QList<int> translatableLineIndexes(const ParsedLyrics &lyrics);

// 变量 title / artist / lines；lines 中的 ID 从 1 开始，与 translatableLineIndexes 的顺序对应
[[nodiscard]] QHash<QString, QString> lyricsTranslationPromptVars(
    const ParsedLyrics &lyrics, const QString &title, const QString &artist);

[[nodiscard]] QJsonObject lyricsTranslationSchema(); // ":/schemas/guide/translate_lyrics.json"

// 返回与 lyrics.lines 等长的译文列表（未翻译的行为空串）。id 越界 / 重复（保留第一个）跳过。
// 缺失的 id 超过待翻译行数的 2%（向上取整，至少允许 0 个）时返回错误 guide.translation_misaligned。
[[nodiscard]] core::Result<QStringList> parseLyricsTranslation(
    const QJsonValue &value, const ParsedLyrics &lyrics);

// 需要翻译：歌词非空、没有任何行已带 translation、且 inferTrackLanguage(全部歌词文本) != Chinese
[[nodiscard]] bool needsTranslation(const ParsedLyrics &lyrics);

// 缓存键：歌词原文（LyricsResult 中的 text）的 SHA-1 十六进制
[[nodiscard]] QString lyricsHash(const QString &rawText);

} // namespace linernotes::guide
