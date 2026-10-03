// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>
#include <QUrl>

#include <core/Result.h>
#include <guide/Wikidata.h>

#include <optional>
#include <utility>

namespace linernotes::guide {

struct WikipediaSummary {
    QString lang { };
    QString title { };
    QString extract { };
    QUrl pageUrl { };
    bool operator==(const WikipediaSummary &) const = default;
};

QUrl wikipediaSummaryUrl(const QString &lang, const QString &title);
core::Result<WikipediaSummary> parseWikipediaSummary(const QByteArray &json, const QString &lang);
std::optional<std::pair<QString, QString>> preferredSitelink(const WikidataEntity &entity);

} // namespace linernotes::guide
