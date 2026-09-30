// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <butler/Errors.h>
#include <butler/TranslationLlm.h>

namespace linernotes::butler {

QHash<QString, QString> translationPromptVars(const QStringList &texts)
{
    QHash<QString, QString> vars;
    QStringList itemLines;
    itemLines.reserve(texts.size());

    for (qsizetype i = 0; i < texts.size(); ++i) {
        itemLines.append(QStringLiteral("ID %1: %2").arg(QString::number(i + 1), texts.at(i)));
    }

    vars.insert(QStringLiteral("items"),
        itemLines.isEmpty() ? QStringLiteral("(none)") : itemLines.join(QLatin1Char('\n')));
    return vars;
}

QJsonObject translationSchema()
{
    QFile file(QStringLiteral(":/schemas/cleanup/translate_titles.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

core::Result<QHash<QString, QString>> parseTranslationResult(
    const QJsonValue &value, const QStringList &texts)
{
    if (!value.isObject()) {
        return core::Error {
            .code = QString(errc::kTranslationInvalidResult),
            .message = QStringLiteral("Expected JSON object for translation result"),
            .detail = QString(),
        };
    }

    const QJsonObject obj = value.toObject();
    const QJsonValue itemsVal = obj.value(QStringLiteral("items"));
    if (!itemsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kTranslationInvalidResult),
            .message = QStringLiteral("Missing 'items' array in result"),
            .detail = QString(),
        };
    }

    const QJsonArray arr = itemsVal.toArray();
    QSet<int> seenIds;
    QHash<QString, QString> results;

    for (const auto &elem : arr) {
        if (!elem.isObject()) {
            continue;
        }
        const QJsonObject itemObj = elem.toObject();
        if (!itemObj.contains(QStringLiteral("id"))) {
            continue;
        }
        const int id = itemObj.value(QStringLiteral("id")).toInt(-1);
        if (id < 1 || id > texts.size()) {
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
        const QString &sourceText = texts.at(id - 1);
        results.insert(sourceText, translation);
    }

    return results;
}

} // namespace linernotes::butler
