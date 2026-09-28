// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QModelIndex>
#include <QString>
#include <QVariant>

#include <core/Clock.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/LibraryEnums.h>

#include <cstdint>

namespace linernotes::ui {

class CorrectionBatchModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(CorrectionBatchModel)

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool hasDecided READ hasDecided NOTIFY countChanged)

public:
    enum Role : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt 模型角色需与 int
                                // 互转
        BatchIdRole = Qt::UserRole + 1,
        KindRole,
        DescriptionRole,
        CreatedAtRole,
        CreatedTextRole,
        PendingCountRole,
        AcceptedCountRole,
        RejectedCountRole,
        RevertedCountRole,
        RevertedRole,
    };
    Q_ENUM(Role)

    explicit CorrectionBatchModel(
        library::Database &db, const core::Clock &clock, QObject *parent = nullptr);
    ~CorrectionBatchModel() override = default;

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const;
    [[nodiscard]] bool hasDecided() const;
    [[nodiscard]] const QList<library::CorrectionBatchInfo> &batches() const;

    Q_INVOKABLE void refresh();
    [[nodiscard]] Q_INVOKABLE qint64 batchIdAt(int row) const;

signals:
    void countChanged();

private:
    library::CorrectionStore m_store;
    QList<library::CorrectionBatchInfo> m_batches;
};

} // namespace linernotes::ui
