// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <library/Database.h>
#include <ui/PagedListModel.h>
#include <ui/UiLogging.h>

namespace linernotes::ui {

PagedListModel::PagedListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PagedListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_count;
}

AppContext *PagedListModel::context() const
{
    return m_context;
}

void PagedListModel::setContext(AppContext *context)
{
    if (m_context == context) {
        return;
    }

    if (m_context != nullptr) {
        disconnect(m_context, &AppContext::libraryReadyChanged, this, &PagedListModel::reload);
        disconnect(m_context, &AppContext::libraryChanged, this, &PagedListModel::onLibraryChanged);
    }

    m_context = context;

    if (m_context != nullptr) {
        connect(m_context, &AppContext::libraryReadyChanged, this, &PagedListModel::reload);
        connect(m_context, &AppContext::libraryChanged, this, &PagedListModel::onLibraryChanged);
    }

    emit contextChanged();
    reload();
}

int PagedListModel::count() const
{
    return m_count;
}

Qt::SortOrder PagedListModel::sortOrder() const
{
    return m_sortOrder;
}

void PagedListModel::setSortOrder(Qt::SortOrder order)
{
    if (m_sortOrder == order) {
        return;
    }
    m_sortOrder = order;
    emit sortOrderChanged(order);
    reload();
}

bool PagedListModel::favoritesOnly() const
{
    return m_favoritesOnly;
}

void PagedListModel::setFavoritesOnly(bool favoritesOnly)
{
    if (m_favoritesOnly == favoritesOnly) {
        return;
    }
    m_favoritesOnly = favoritesOnly;
    emit favoritesOnlyChanged(favoritesOnly);
    reload();
}

int PagedListModel::pageSize() const
{
    return m_pageSize;
}

void PagedListModel::setPageSize(int size)
{
    if (size <= 0 || size == m_pageSize) {
        return;
    }
    m_pageSize = size;
    emit pageSizeChanged(size);
    reload();
}

std::optional<library::LibraryQuery> PagedListModel::makeQuery() const
{
    if (m_context == nullptr || !m_context->isLibraryReady()) {
        return std::nullopt;
    }
    const auto connRes = m_context->database()->connection();
    if (!connRes.ok()) {
        return std::nullopt;
    }
    return library::LibraryQuery(connRes.value());
}

void PagedListModel::warnOnce(int page, const QString &message) const
{
    if (m_warnedPages.insert(page).second) {
        qCWarning(lcUi, "%s", qPrintable(message));
    }
}

void PagedListModel::reload()
{
    beginResetModel();
    clearCache();
    m_warnedPages.clear();
    const int oldCount = m_count;
    const auto queryOpt = makeQuery();
    if (queryOpt.has_value()) {
        const auto countRes = queryCount(queryOpt.value());
        if (countRes.ok()) {
            m_count = countRes.value();
        } else {
            qCWarning(lcUi, "Failed to count rows: %s", qPrintable(countRes.error().toString()));
            m_count = 0;
        }
    } else {
        m_count = 0;
    }
    endResetModel();

    if (m_count != oldCount) {
        emit countChanged(m_count);
    }
}

void PagedListModel::onLibraryChanged()
{
    const auto queryOpt = makeQuery();
    if (!queryOpt.has_value()) {
        if (m_count > 0) {
            beginResetModel();
            clearCache();
            m_warnedPages.clear();
            m_count = 0;
            endResetModel();
            emit countChanged(0);
        }
        return;
    }

    const auto countRes = queryCount(queryOpt.value());
    if (!countRes.ok()) {
        qCWarning(lcUi, "Failed to recount rows on libraryChanged: %s",
            qPrintable(countRes.error().toString()));
        return;
    }

    const int oldCount = m_count;
    const int newCount = countRes.value();

    // Do NOT call beginResetModel()/endResetModel() here.
    // Resetting the model causes views (ListView, TableView, GridView) to lose their current scroll
    // position and jump back to index 0 (top).
    // Instead, adjust row count at the end with beginInsertRows / beginRemoveRows,
    // clear the page cache, and notify dataChanged for all rows to refresh stale metadata
    // while keeping the user's scroll position intact.
    if (newCount > oldCount) {
        beginInsertRows(QModelIndex(), oldCount, newCount - 1);
        m_count = newCount;
        endInsertRows();
    } else if (newCount < oldCount) {
        beginRemoveRows(QModelIndex(), newCount, oldCount - 1);
        m_count = newCount;
        endRemoveRows();
    } else {
        m_count = newCount;
    }

    clearCache();
    m_warnedPages.clear();

    if (m_count > 0) {
        emit dataChanged(index(0, 0), index(m_count - 1, 0));
    }

    if (newCount != oldCount) {
        emit countChanged(m_count);
    }
}

} // namespace linernotes::ui
