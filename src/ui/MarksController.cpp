// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MarksController.h"

#include <ui/UiLogging.h>

namespace linernotes::ui {

MarksController::MarksController(library::Database &db)
    : m_store(db)
{
}

bool MarksController::setFavorite(
    library::FavoriteKind kind, const QList<qint64> &ids, bool favorite)
{
    const auto res = m_store.setFavorite(kind, ids, favorite);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to set favorite: %s", qPrintable(res.error().toString()));
        return false;
    }
    emit marksChanged();
    return true;
}

bool MarksController::toggleFavorite(library::FavoriteKind kind, qint64 id)
{
    const auto isFavRes = m_store.isFavorite(kind, id);
    if (!isFavRes.ok()) {
        qCWarning(lcUi, "Failed to check favorite status for id %lld: %s",
            static_cast<long long>(id), qPrintable(isFavRes.error().toString()));
        return false;
    }
    return setFavorite(kind, { id }, !isFavRes.value());
}

bool MarksController::isFavorite(library::FavoriteKind kind, qint64 id) const
{
    const auto res = m_store.isFavorite(kind, id);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to check favorite status for id %lld: %s",
            static_cast<long long>(id), qPrintable(res.error().toString()));
        return false;
    }
    return res.value();
}

bool MarksController::setRating(const QList<qint64> &trackIds, int rating)
{
    const auto res = m_store.setRating(trackIds, rating);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to set rating: %s", qPrintable(res.error().toString()));
        return false;
    }
    emit marksChanged();
    return true;
}

int MarksController::rating(qint64 trackId) const
{
    const auto res = m_store.rating(trackId);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to get rating for track id %lld: %s",
            static_cast<long long>(trackId), qPrintable(res.error().toString()));
        return 0;
    }
    return res.value();
}

} // namespace linernotes::ui
