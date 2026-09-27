// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QAbstractListModel>
#include <QList>

#include <ai/AiEnums.h>
#include <ai/UsageStore.h>
#include <core/Clock.h>

#include <cstdint>

namespace linernotes::ui {

class UsageSummaryModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(UsageSummaryModel)

    Q_PROPERTY(qint64 totalPromptTokens READ totalPromptTokens NOTIFY totalsChanged)
    Q_PROPERTY(qint64 totalCompletionTokens READ totalCompletionTokens NOTIFY totalsChanged)
    Q_PROPERTY(int totalRequests READ totalRequests NOTIFY totalsChanged)

public:
    enum Role : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt model roles need
                                // int conversion
        PurposeRole = Qt::UserRole + 1,
        ModelRole,
        RequestsRole,
        CacheHitsRole,
        FailuresRole,
        PromptTokensRole,
        CompletionTokensRole,
    };
    Q_ENUM(Role)

    explicit UsageSummaryModel(
        ai::UsageStore &usageStore, const core::Clock &clock, QObject *parent = nullptr);
    ~UsageSummaryModel() override = default;

    [[nodiscard]] int rowCount(const QModelIndex &parent = { }) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] qint64 totalPromptTokens() const;
    [[nodiscard]] qint64 totalCompletionTokens() const;
    [[nodiscard]] int totalRequests() const;

    Q_INVOKABLE void refresh(int days);

signals:
    void totalsChanged();

private:
    ai::UsageStore &m_usageStore;
    const core::Clock &m_clock;
    QList<ai::UsageSummary> m_summaries;
    qint64 m_totalPromptTokens = 0;
    qint64 m_totalCompletionTokens = 0;
    int m_totalRequests = 0;
};

} // namespace linernotes::ui
