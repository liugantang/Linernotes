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
#include <ui/RowSelection.h>

#include <cstdint>

namespace linernotes::ui {

class CorrectionListModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(CorrectionListModel)

    Q_PROPERTY(
        StatusFilter statusFilter READ statusFilter WRITE setStatusFilter NOTIFY filterChanged)
    Q_PROPERTY(double minConfidence READ minConfidence WRITE setMinConfidence NOTIFY filterChanged)
    Q_PROPERTY(linernotes::ui::RowSelection *selection READ selection CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY countChanged)
    Q_PROPERTY(int staleCount READ staleCount NOTIFY countChanged)
    Q_PROPERTY(qint64 batchId READ batchId NOTIFY batchChanged)

public:
    enum class CorrectionEntity : std::uint8_t { Track, Artist };
    Q_ENUM(CorrectionEntity)

    enum class StatusFilter : std::uint8_t { All, Pending, Accepted, Rejected, Reverted, Stale };
    Q_ENUM(StatusFilter)

    enum Role : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt 模型角色需与 int
                                // 互转
        CorrectionIdRole = Qt::UserRole + 1,
        EntityRole,
        SubjectRole,
        DetailRole,
        FieldRole,
        OldValueRole,
        NewValueRole,
        ConfidenceRole,
        ReasonRole,
        SourceRole,
        StatusRole,
        StaleRole
    };
    Q_ENUM(Role)

    struct UnifiedCorrectionItem {
        qint64 correctionId = 0;
        CorrectionEntity entity = CorrectionEntity::Track;
        QString subject { };
        QString detail { };
        QString field { };
        QString oldValue { };
        QString newValue { };
        double confidence = 0.0;
        QString reason { };
        library::CorrectionSource source = library::CorrectionSource::Rule;
        library::CorrectionStatus status = library::CorrectionStatus::Pending;
        bool stale = false;
    };

    explicit CorrectionListModel(
        library::Database &db, const core::Clock &clock, QObject *parent = nullptr);
    ~CorrectionListModel() override = default;

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] StatusFilter statusFilter() const;
    void setStatusFilter(StatusFilter filter);

    [[nodiscard]] double minConfidence() const;
    void setMinConfidence(double minConfidence);

    [[nodiscard]] linernotes::ui::RowSelection *selection();
    [[nodiscard]] const linernotes::ui::RowSelection *selection() const;

    [[nodiscard]] int count() const;
    [[nodiscard]] int pendingCount() const;
    [[nodiscard]] int staleCount() const;
    [[nodiscard]] qint64 batchId() const;
    [[nodiscard]] library::CorrectionKind batchKind() const;

    [[nodiscard]] const QList<UnifiedCorrectionItem> &filteredRows() const;
    [[nodiscard]] const QList<UnifiedCorrectionItem> &allRows() const;

    Q_INVOKABLE void setBatch(qint64 batchId, library::CorrectionKind kind);
    Q_INVOKABLE void refresh();
    [[nodiscard]] Q_INVOKABLE qint64 correctionIdAt(int row) const;
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();

    [[nodiscard]] QList<qint64> selectedCorrectionIds() const;
    Q_INVOKABLE [[nodiscard]] QList<qint64> allPendingCorrectionIds(double minConfidence) const;

signals:
    void filterChanged();
    void countChanged();
    void batchChanged();

private:
    void applyFilter();

    library::CorrectionStore m_store;
    RowSelection m_selection;
    qint64 m_batchId = 0;
    library::CorrectionKind m_batchKind = library::CorrectionKind::Manual;
    StatusFilter m_statusFilter = StatusFilter::All;
    double m_minConfidence = 0.0;
    QList<UnifiedCorrectionItem> m_allRows;
    QList<UnifiedCorrectionItem> m_filteredRows;
};

} // namespace linernotes::ui
