// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtGlobal>

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::rec {
class SimilarTracks;
} // namespace linernotes::rec

namespace linernotes::ui {

class LibraryActions;

class SimilarController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SimilarController)

    Q_PROPERTY(QString seedTitle READ seedTitle NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY resultsChanged)
    Q_PROPERTY(bool seedAnalyzed READ isSeedAnalyzed NOTIFY resultsChanged)

public:
    SimilarController(rec::SimilarTracks &similarTracks, library::Database &db,
        LibraryActions &actions, QObject *parent = nullptr);
    ~SimilarController() override = default;

    [[nodiscard]] QString seedTitle() const;
    [[nodiscard]] QVariantList rows() const;
    [[nodiscard]] bool isSeedAnalyzed() const;

    Q_INVOKABLE void find(qint64 trackId);
    Q_INVOKABLE void playAll();
    Q_INVOKABLE void playRow(int index);
    Q_INVOKABLE void enqueueAll();

signals:
    void resultsChanged();
    void openRequested();

private:
    [[nodiscard]] QList<qint64> collectResultTrackIds() const;

    rec::SimilarTracks &m_similarTracks;
    library::Database &m_db;
    LibraryActions &m_actions;

    qint64 m_seedTrackId = 0;
    QString m_seedTitle;
    QVariantList m_rows;
    bool m_seedAnalyzed = false;
};

} // namespace linernotes::ui
