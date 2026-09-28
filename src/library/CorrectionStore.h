// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <core/Result.h>
#include <library/ArtistAliasCorrections.h>
#include <library/LibraryEnums.h>

#include <cstdint>
#include <optional>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {

class Database;

struct CorrectionProposal {
    qint64 trackId = 0;
    TagField field = TagField::Title;
    std::optional<QString> oldValue; // nullopt：写入时取当前 effective 值
    QString newValue { };
    CorrectionSource source = CorrectionSource::Rule;
    double confidence = 0.0; // [0,1]，越界返回 kInvalidArgument（或项目中等价的错误码）
    QString reason { };

    bool operator==(const CorrectionProposal &other) const = default;
};

struct CorrectionBatchInfo {
    qint64 id = 0;
    CorrectionKind kind = CorrectionKind::Manual;
    QString description { };
    qint64 createdAt = 0;
    std::optional<qint64> revertedAt;
    int pending = 0;
    int accepted = 0;
    int rejected = 0;
    int reverted = 0; // 各状态条数（分行声明，带默认值）

    bool operator==(const CorrectionBatchInfo &other) const = default;
};

struct CorrectionRow {
    qint64 id = 0;
    qint64 batchId = 0;
    qint64 trackId = 0;
    TagField field = TagField::Title;
    QString oldValue { };
    QString newValue { };
    CorrectionSource source = CorrectionSource::Rule;
    double confidence = 0.0;
    QString reason { };
    CorrectionStatus status = CorrectionStatus::Pending;
    QString trackTitle { }; // 当前 effective 标题（界面显示“哪首歌”）
    QString filePath { };
    bool stale = false;

    bool operator==(const CorrectionRow &other) const = default;
};

struct CorrectionFilter {
    std::optional<TagField> field;
    std::optional<CorrectionStatus> status;
    double minConfidence = 0.0;
    double maxConfidence = 1.0;

    bool operator==(const CorrectionFilter &other) const = default;
};

class CorrectionStore {
public:
    CorrectionStore(Database &db, const core::Clock &clock);

    core::Result<qint64> createBatch(CorrectionKind kind, const QString &description);
    /// 写入提议。confidence >= autoAcceptThreshold 的直接置为 accepted（decided_at = now）；
    /// 为 nullopt 时全部 pending。同一事务；有被接受的项时刷新 effective（见下）。
    core::Result<void> addProposals(qint64 batchId, const QList<CorrectionProposal> &proposals,
        std::optional<double> autoAcceptThreshold = std::nullopt);

    core::Result<void> addArtistAliasProposals(qint64 batchId,
        const QList<ArtistAliasProposal> &proposals,
        std::optional<double> autoAcceptThreshold = std::nullopt);

    core::Result<QList<CorrectionBatchInfo>> batches() const; // created_at 倒序
    core::Result<QList<CorrectionRow>> corrections(
        qint64 batchId, const CorrectionFilter &filter = { }) const; // 按 id 升序
    core::Result<QList<ArtistAliasRow>> artistAliasCorrections(qint64 batchId) const; // 按 id 升序

    core::Result<AcceptOutcome> accept(const QList<qint64> &correctionIds); // 只影响 pending 的
    core::Result<void> reject(const QList<qint64> &correctionIds); // 只影响 pending 的
    /// 用户编辑后接受：new_value 改为 value，source 改为 user，状态 accepted。只允许 pending。
    core::Result<void> acceptEdited(qint64 correctionId, const QString &value);
    /// 整批撤销：accepted → reverted，pending → rejected，写
    /// reverted_at。已撤销的批次再撤销是空操作。
    core::Result<void> revertBatch(qint64 batchId);

private:
    Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::library
