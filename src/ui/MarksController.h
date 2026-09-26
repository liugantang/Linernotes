// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>

#include <library/LibraryEnums.h>
#include <library/MarkStore.h>

#include <cstdint>

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::ui {

class MarksController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MarksController)

public:
    explicit MarksController(library::Database &db);
    ~MarksController() override = default;

    Q_INVOKABLE bool setFavorite(
        library::FavoriteKind kind, const QList<qint64> &ids, bool favorite);
    Q_INVOKABLE bool toggleFavorite(library::FavoriteKind kind, qint64 id);
    [[nodiscard]] Q_INVOKABLE bool isFavorite(library::FavoriteKind kind, qint64 id) const;
    Q_INVOKABLE bool setRating(const QList<qint64> &trackIds, int rating);
    [[nodiscard]] Q_INVOKABLE int rating(qint64 trackId) const;

signals:
    void marksChanged();

private:
    library::MarkStore m_store;
};

} // namespace linernotes::ui
