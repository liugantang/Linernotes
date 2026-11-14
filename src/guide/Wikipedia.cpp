// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Wikipedia.h"

#include "Errors.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace linernotes::guide {

QUrl wikipediaSummaryUrl(const QString &lang, const QString &title)
{
    QString formattedTitle = title;
    formattedTitle.replace(QLatin1Char(' '), QLatin1Char('_'));
    const QByteArray encodedTitle = QUrl::toPercentEncoding(formattedTitle);
    const QString urlString = QStringLiteral("https://%1.wikipedia.org/api/rest_v1/page/summary/%2")
                                  .arg(lang, QString::fromUtf8(encodedTitle));
    return QUrl::fromEncoded(urlString.toUtf8());
}

core::Result<WikipediaSummary> parseWikipediaSummary(const QByteArray &json, const QString &lang)
{
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kParseFailed),
            .message = QStringLiteral("Failed to parse Wikipedia summary"),
            .detail = parseErr.error != QJsonParseError::NoError
                ? parseErr.errorString()
                : QStringLiteral("Invalid JSON object"),
        };
    }

    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("disambiguation")) {
        return core::Error {
            .code = QString(errc::kParseFailed),
            .message = QStringLiteral("Wikipedia page is a disambiguation page"),
            .detail = QStringLiteral("Disambiguation page returned"),
        };
    }

    WikipediaSummary summary;
    summary.lang = lang;
    summary.title = obj.value(QStringLiteral("title")).toString();
    summary.extract = obj.value(QStringLiteral("extract")).toString();

    const QJsonObject contentUrls = obj.value(QStringLiteral("content_urls")).toObject();
    const QJsonObject desktop = contentUrls.value(QStringLiteral("desktop")).toObject();
    const QString pageUrlStr = desktop.value(QStringLiteral("page")).toString();
    if (!pageUrlStr.isEmpty()) {
        summary.pageUrl = QUrl(pageUrlStr);
    }

    return summary;
}

std::optional<std::pair<QString, QString>> preferredSitelink(const WikidataEntity &entity)
{
    static const auto s_sites = {
        std::make_pair(QStringLiteral("zhwiki"), QStringLiteral("zh")),
        std::make_pair(QStringLiteral("jawiki"), QStringLiteral("ja")),
        std::make_pair(QStringLiteral("enwiki"), QStringLiteral("en")),
    };

    for (const auto &[siteKey, langCode] : s_sites) {
        if (const auto it = entity.sitelinkTitles.find(siteKey);
            it != entity.sitelinkTitles.end() && !it->isEmpty()) {
            return std::make_pair(langCode, *it);
        }
    }

    return std::nullopt;
}

} // namespace linernotes::guide
