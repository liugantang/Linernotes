// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>

#include <core/Clock.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/LibraryEnums.h>
#include <ui/CorrectionBatchModel.h>
#include <ui/CorrectionListModel.h>

#include <cstdint>

namespace linernotes::ui {

class CorrectionReviewController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(CorrectionReviewController)

    Q_PROPERTY(linernotes::ui::CorrectionBatchModel *batchModel READ batchModel CONSTANT)
    Q_PROPERTY(linernotes::ui::CorrectionListModel *listModel READ listModel CONSTANT)
    Q_PROPERTY(qint64 currentBatchId READ currentBatchId NOTIFY currentBatchChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QString noticeText READ noticeText NOTIFY noticeTextChanged)

public:
    explicit CorrectionReviewController(
        library::Database &db, const core::Clock &clock, QObject *parent = nullptr);
    ~CorrectionReviewController() override = default;

    [[nodiscard]] CorrectionBatchModel *batchModel();
    [[nodiscard]] const CorrectionBatchModel *batchModel() const;
    [[nodiscard]] CorrectionListModel *listModel();
    [[nodiscard]] const CorrectionListModel *listModel() const;

    [[nodiscard]] qint64 currentBatchId() const;
    [[nodiscard]] QString errorText() const;
    [[nodiscard]] QString noticeText() const;

    Q_INVOKABLE void selectBatch(qint64 batchId);
    Q_INVOKABLE void acceptSelected();
    Q_INVOKABLE void rejectSelected();
    Q_INVOKABLE void acceptAllPending(double minConfidence);
    Q_INVOKABLE void acceptEdited(qint64 correctionId, const QString &value);
    Q_INVOKABLE void revertBatch(qint64 batchId);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void showStale();
    Q_INVOKABLE void clearNotice();

signals:
    void currentBatchChanged();
    void errorTextChanged();
    void noticeTextChanged();
    void libraryModified();

private:
    library::Database &m_db;
    const core::Clock &m_clock;
    library::CorrectionStore m_store;
    CorrectionBatchModel m_batchModel;
    CorrectionListModel m_listModel;
    qint64 m_currentBatchId = 0;
    library::CorrectionKind m_currentBatchKind = library::CorrectionKind::Manual;
    QString m_errorText;
    QString m_noticeText;
};

} // namespace linernotes::ui
