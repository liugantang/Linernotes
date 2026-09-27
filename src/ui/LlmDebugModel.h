// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QAbstractListModel>
#include <QList>

#include <ai/AiEnums.h>
#include <ai/LlmDebugLog.h>

#include <cstdint>

namespace linernotes::ui {

class LlmDebugModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LlmDebugModel)

public:
    enum Role : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt model roles need
                                // int conversion
        EntryIdRole = Qt::UserRole + 1,
        TimeRole,
        PurposeRole,
        ModelRole,
        AttemptRole,
        FromCacheRole,
        OkRole,
        HttpStatusRole,
        ErrorCodeRole,
        ElapsedMsRole,
        PromptTokensRole,
        CompletionTokensRole,
    };
    Q_ENUM(Role)

    explicit LlmDebugModel(ai::LlmDebugLog &log, QObject *parent = nullptr);
    ~LlmDebugModel() override = default;

    [[nodiscard]] int rowCount(const QModelIndex &parent = { }) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
    void onEntryAdded(quint64 id);
    void onCleared();

    ai::LlmDebugLog &m_log;
    QList<quint64> m_ids;
};

} // namespace linernotes::ui
