// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QNetworkAccessManager>
#include <QUrl>

#include <ai/JobHandler.h>
#include <core/Clock.h>
#include <core/Result.h>
#include <library/CoverStore.h>

#include <functional>
#include <memory>
#include <string_view>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

inline constexpr std::string_view kCoverArtArchiveBase { "https://coverartarchive.org" };

class CoverArtJobHandler final : public ai::JobHandler {
public:
    CoverArtJobHandler(library::Database &db, QNetworkAccessManager &network,
        library::CoverStore &covers, const core::Clock &clock,
        QUrl baseUrl = QUrl(QString::fromUtf8(
            kCoverArtArchiveBase.data(), static_cast<qsizetype>(kCoverArtArchiveBase.size()))));
    ~CoverArtJobHandler() override = default;
    Q_DISABLE_COPY_MOVE(CoverArtJobHandler)

    [[nodiscard]] QString kind() const override; // "butler.cover_art"
    [[nodiscard]] int maxInFlight() const override;
    [[nodiscard]] ai::TokenUsage estimate(
        const QString &itemKey, const QJsonObject &params) const override;
    std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject &params,
        std::function<void(const core::Result<void> &)> done) override;

private:
    library::Database &m_db;
    QNetworkAccessManager &m_network;
    library::CoverStore &m_covers;
    const core::Clock &m_clock;
    QUrl m_baseUrl;
};

} // namespace linernotes::butler
