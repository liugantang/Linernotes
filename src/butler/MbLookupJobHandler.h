// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <ai/JobHandler.h>

#include <memory>

namespace linernotes::butler {

class MusicBrainzClient;

class MbLookupJobHandler final : public ai::JobHandler {
public:
    explicit MbLookupJobHandler(MusicBrainzClient &mbClient);
    ~MbLookupJobHandler() override = default;
    Q_DISABLE_COPY_MOVE(MbLookupJobHandler)

    [[nodiscard]] QString kind() const override; // "butler.mb_lookup"
    [[nodiscard]] ai::TokenUsage estimate(
        const QString &itemKey, const QJsonObject &params) const override;
    std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject &params,
        std::function<void(const core::Result<void> &)> done) override;

private:
    MusicBrainzClient &m_mbClient;
};

} // namespace linernotes::butler
