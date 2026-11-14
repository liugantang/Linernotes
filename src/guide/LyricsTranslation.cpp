// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LyricsTranslation.h"

#include "Errors.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <library/TrackLanguage.h>

#include <cmath>

namespace linernotes::guide {

QList<int> translatableLineIndexes(const ParsedLyrics &lyrics)
{
    QList<int> indexes;
    for (qsizetype i = 0; i < lyrics.lines.size(); ++i) {
        if (!lyrics.lines.at(i).text.trimmed().isEmpty()) {
            indexes.append(static_cast<int>(i));
        }
    }
    return indexes;
}

QHash<QString, QString> lyricsTranslationPromptVars(
    const ParsedLyrics &lyrics, const QString &title, const QString &artist)
{
    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("title"), title);
    vars.insert(QStringLiteral("artist"), artist);

    const auto idxs = translatableLineIndexes(lyrics);
    QStringList lineItems;
    lineItems.reserve(idxs.size());

    for (qsizetype i = 0; i < idxs.size(); ++i) {
        const int lineIdx = idxs.at(i);
        const QString &text = lyrics.lines.at(lineIdx).text;
        lineItems.append(QStringLiteral("ID %1: %2").arg(QString::number(i + 1), text));
    }

    vars.insert(QStringLiteral("lines"),
        lineItems.isEmpty() ? QStringLiteral("(none)") : lineItems.join(QLatin1Char('\n')));
    return vars;
}

QJsonObject lyricsTranslationSchema()
{
    QFile file(QStringLiteral(":/schemas/guide/translate_lyrics.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

core::Result<QStringList> parseLyricsTranslation(
    const QJsonValue &value, const ParsedLyrics &lyrics)
{
    const auto idxs = translatableLineIndexes(lyrics);
    const int totalTranslatable = static_cast<int>(idxs.size());

    QStringList result;
    result.resize(lyrics.lines.size());

    if (totalTranslatable == 0) {
        return result;
    }

    if (!value.isObject()) {
        return core::Error {
            .code = QString(errc::kTranslationMisaligned),
            .message = QStringLiteral("Expected JSON object for translation result"),
            .detail = QString(),
        };
    }

    const QJsonObject obj = value.toObject();
    const QJsonValue linesVal = obj.value(QStringLiteral("lines"));
    if (!linesVal.isArray()) {
        return core::Error {
            .code = QString(errc::kTranslationMisaligned),
            .message = QStringLiteral("Missing 'lines' array in translation result"),
            .detail = QString(),
        };
    }

    const QJsonArray arr = linesVal.toArray();
    QSet<int> seenIds;

    for (const auto &elem : arr) {
        if (!elem.isObject()) {
            continue;
        }
        const QJsonObject itemObj = elem.toObject();
        if (!itemObj.contains(QStringLiteral("id"))) {
            continue;
        }
        const int id = itemObj.value(QStringLiteral("id")).toInt(-1);
        if (id < 1 || id > totalTranslatable) {
            continue;
        }
        if (seenIds.contains(id)) {
            continue;
        }
        if (!itemObj.contains(QStringLiteral("translation"))) {
            continue;
        }

        const QString translation = itemObj.value(QStringLiteral("translation")).toString();
        seenIds.insert(id);

        const int targetLineIndex = idxs.at(id - 1);
        result.replace(targetLineIndex, translation);
    }

    const int missingCount = totalTranslatable - static_cast<int>(seenIds.size());
    const int maxAllowedMissing = static_cast<int>(std::ceil(totalTranslatable * 0.02));

    if (missingCount > maxAllowedMissing) {
        return core::Error {
            .code = QString(errc::kTranslationMisaligned),
            .message = QStringLiteral("Too many missing translation lines"),
            .detail = QStringLiteral("Missing %1 of %2 lines")
                .arg(QString::number(missingCount), QString::number(totalTranslatable)),
        };
    }

    return result;
}

bool needsTranslation(const ParsedLyrics &lyrics)
{
    if (lyrics.lines.isEmpty()) {
        return false;
    }

    for (const auto &line : lyrics.lines) {
        if (!line.translation.isEmpty()) {
            return false;
        }
    }

    QString allText;
    for (const auto &line : lyrics.lines) {
        if (!line.text.isEmpty()) {
            if (!allText.isEmpty()) {
                allText.append(QLatin1Char('\n'));
            }
            allText.append(line.text);
        }
    }

    if (allText.trimmed().isEmpty()) {
        return false;
    }

    return library::inferTrackLanguage(allText, QString(), QString())
        != library::TrackLanguage::Chinese;
}

QString lyricsHash(const QString &rawText)
{
    return QString::fromUtf8(
        QCryptographicHash::hash(rawText.toUtf8(), QCryptographicHash::Sha1).toHex());
}

} // namespace linernotes::guide
