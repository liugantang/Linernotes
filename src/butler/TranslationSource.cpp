// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QChar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <butler/Errors.h>
#include <butler/TranslationSource.h>
#include <library/Database.h>
#include <library/Errors.h>

#include <algorithm>

namespace linernotes::butler {

namespace {

bool isKana(uint cp)
{
    const auto s = QChar::script(cp);
    if (s == QChar::Script_Hiragana || s == QChar::Script_Katakana) {
        return true;
    }
    return (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x31F0 && cp <= 0x31FF)
        || (cp >= 0xFF65 && cp <= 0xFF9F);
}

bool isHangul(uint cp)
{
    const auto s = QChar::script(cp);
    if (s == QChar::Script_Hangul) {
        return true;
    }
    return (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF)
        || (cp >= 0x3130 && cp <= 0x318F) || (cp >= 0xA960 && cp <= 0xA97F)
        || (cp >= 0xD7B0 && cp <= 0xD7FF);
}

bool isLatin(uint cp)
{
    const auto s = QChar::script(cp);
    if (s == QChar::Script_Latin) {
        return true;
    }
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}

} // namespace

bool needsTranslation(const QString &text)
{
    const auto ucs4 = text.toUcs4();
    if (std::ranges::any_of(ucs4, [](uint cp) { return isKana(cp) || isHangul(cp); })) {
        return true;
    }
    // 单个拉丁字母（如 “A 面”“炎 (M)”）不足以说明是外文
    return std::ranges::count_if(ucs4, isLatin) >= 2;
}

core::Result<QStringList> parseTranslationItemKey(const QString &itemKey)
{
    QJsonParseError parseErr { };
    const auto doc = QJsonDocument::fromJson(itemKey.toUtf8(), &parseErr);
    if (doc.isNull() || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kTranslationInvalidKey),
            .message = QStringLiteral("Failed to parse item key as JSON object"),
            .detail = itemKey,
        };
    }

    const QJsonObject obj = doc.object();
    const QJsonValue itemsVal = obj.value(QStringLiteral("items"));
    if (!itemsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kTranslationInvalidKey),
            .message = QStringLiteral("Missing or invalid 'items' array in item key"),
            .detail = itemKey,
        };
    }

    QStringList texts;
    const QJsonArray arr = itemsVal.toArray();
    texts.reserve(arr.size());

    for (const auto &elem : arr) {
        if (!elem.isString()) {
            return core::Error {
                .code = QString(errc::kTranslationInvalidKey),
                .message = QStringLiteral("Item in 'items' array is not a string"),
                .detail = itemKey,
            };
        }
        texts.append(elem.toString());
    }

    return texts;
}

TranslationSource::TranslationSource(library::Database &db)
    : m_db(db)
{
}

core::Result<QStringList> TranslationSource::collectPendingTexts(int promptVersion) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery qKnown(conn);
    qKnown.prepare(QStringLiteral(
        "SELECT source_text FROM text_translations WHERE target_lang = ? AND prompt_version = ?"));
    qKnown.addBindValue(QString(kTargetLangZhHans));
    qKnown.addBindValue(promptVersion);
    if (!qKnown.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = qKnown.lastError().text(),
            .detail = QString(),
        };
    }

    QSet<QString> knownTexts;
    while (qKnown.next()) {
        knownTexts.insert(qKnown.value(0).toString());
    }

    QSqlQuery qTexts(conn);
    const QString sql = QStringLiteral(
        "SELECT DISTINCT title FROM effective_metadata WHERE title IS NOT NULL AND title != '' "
        "UNION "
        "SELECT DISTINCT album FROM effective_metadata WHERE album IS NOT NULL AND album != ''");
    if (!qTexts.exec(sql)) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = qTexts.lastError().text(),
            .detail = QString(),
        };
    }

    QSet<QString> pendingSet;
    while (qTexts.next()) {
        const QString text = qTexts.value(0).toString().trimmed();
        if (text.isEmpty() || !needsTranslation(text)) {
            continue;
        }
        if (!knownTexts.contains(text)) {
            pendingSet.insert(text);
        }
    }

    QStringList result = pendingSet.values();
    std::ranges::sort(result);
    return result;
}

core::Result<int> TranslationSource::countPending(int promptVersion) const
{
    auto textsRes = collectPendingTexts(promptVersion);
    if (!textsRes.ok()) {
        return textsRes.error();
    }
    return static_cast<int>(textsRes.value().size());
}

core::Result<QStringList> TranslationSource::findItems(int promptVersion) const
{
    auto textsRes = collectPendingTexts(promptVersion);
    if (!textsRes.ok()) {
        return textsRes.error();
    }

    const auto &texts = textsRes.value();
    if (texts.isEmpty()) {
        return QStringList();
    }

    QStringList groupKeys;
    for (qsizetype i = 0; i < texts.size(); i += kBatchSize) {
        const qsizetype end = std::min(i + kBatchSize, texts.size());
        QJsonArray arr;
        for (qsizetype j = i; j < end; ++j) {
            arr.append(texts.at(j));
        }
        QJsonObject rootObj;
        rootObj.insert(QStringLiteral("items"), arr);
        groupKeys.append(QString::fromUtf8(QJsonDocument(rootObj).toJson(QJsonDocument::Compact)));
    }

    return groupKeys;
}

} // namespace linernotes::butler
