// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlayQueue.h"

#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace linernotes::player {

PlayQueue::PlayQueue(quint64 seed, QObject *parent)
    : QAbstractListModel(parent)
    , m_order(seed)
{
}

int PlayQueue::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_items.size());
}

QVariant PlayQueue::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()
        || index.column() != 0) {
        return { };
    }

    const QueueItem &item = m_items.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return QFileInfo(item.source).fileName();
    case SourceRole:
        return item.source;
    case TrackIdRole:
        return item.trackId;
    case UidRole:
        return item.uid;
    case IsCurrentRole:
        return index.row() == currentIndex();
    default:
        return { };
    }
}

QHash<int, QByteArray> PlayQueue::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles.insert(SourceRole, "source");
    roles.insert(TrackIdRole, "trackId");
    roles.insert(UidRole, "uid");
    roles.insert(IsCurrentRole, "isCurrent");
    return roles;
}

int PlayQueue::count() const
{
    return static_cast<int>(m_items.size());
}

const QueueItem &PlayQueue::at(int row) const
{
    Q_ASSERT(row >= 0 && row < m_items.size());
    return m_items.at(row);
}

int PlayQueue::rowOfUid(quint64 uid) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).uid == uid) {
            return i;
        }
    }
    return -1;
}

int PlayQueue::currentIndex() const
{
    return m_order.current();
}

std::optional<QueueItem> PlayQueue::currentItem() const
{
    const int idx = currentIndex();
    if (idx >= 0 && idx < m_items.size()) {
        return m_items.at(idx);
    }
    return std::nullopt;
}

PlayMode PlayQueue::mode() const
{
    return m_order.mode();
}

void PlayQueue::setMode(PlayMode mode)
{
    if (m_order.mode() == mode) {
        return;
    }

    m_order.setMode(mode);
    emit modeChanged(mode);
    emit upcomingChanged();
}

void PlayQueue::notifyCurrentIndexChanged(int oldCurrent, int newCurrent)
{
    if (oldCurrent == newCurrent) {
        return;
    }
    emit currentIndexChanged(newCurrent);
    if (oldCurrent >= 0 && oldCurrent < m_items.size()) {
        const QModelIndex idx = index(oldCurrent, 0);
        emit dataChanged(idx, idx, { IsCurrentRole });
    }
    if (newCurrent >= 0 && newCurrent < m_items.size()) {
        const QModelIndex idx = index(newCurrent, 0);
        emit dataChanged(idx, idx, { IsCurrentRole });
    }
}

void PlayQueue::setItems(const QList<QueueItem> &items, int startIndex)
{
    const int oldCount = static_cast<int>(m_items.size());
    const int oldCurrent = m_order.current();

    beginResetModel();
    m_items.clear();
    m_items.reserve(items.size());
    for (auto item : items) {
        item.uid = m_nextUid++;
        m_items.append(item);
    }
    m_order.reset(static_cast<int>(m_items.size()), startIndex);
    endResetModel();

    Q_ASSERT(m_order.count() == m_items.size());

    if (oldCount != m_items.size()) {
        emit countChanged(static_cast<int>(m_items.size()));
    }
    if (oldCurrent != m_order.current()) {
        emit currentIndexChanged(m_order.current());
    }
    emit upcomingChanged();
}

void PlayQueue::append(const QList<QueueItem> &items)
{
    insert(static_cast<int>(m_items.size()), items);
}

void PlayQueue::insert(int row, const QList<QueueItem> &items)
{
    if (items.isEmpty()) {
        return;
    }

    if (row < 0 || row > m_items.size()) {
        return;
    }

    const int insertCount = static_cast<int>(items.size());
    const int oldCurrent = m_order.current();

    beginInsertRows(QModelIndex(), row, row + insertCount - 1);
    m_items.reserve(m_items.size() + insertCount);
    for (int i = 0; i < insertCount; ++i) {
        QueueItem item = items.at(i);
        item.uid = m_nextUid++;
        m_items.insert(row + i, item);
    }
    m_order.onInserted(row, insertCount);
    endInsertRows();

    Q_ASSERT(m_order.count() == m_items.size());

    emit countChanged(static_cast<int>(m_items.size()));
    notifyCurrentIndexChanged(oldCurrent, m_order.current());
    emit upcomingChanged();
}

void PlayQueue::insertNext(const QList<QueueItem> &items)
{
    if (items.isEmpty()) {
        return;
    }

    const int insertRow = (currentIndex() == -1) ? 0 : (currentIndex() + 1);
    const int insertCount = static_cast<int>(items.size());

    insert(insertRow, items);

    for (int i = insertCount - 1; i >= 0; --i) {
        m_order.scheduleNext(insertRow + i);
    }

    emit upcomingChanged();
}

void PlayQueue::remove(int row, int count)
{
    if (count <= 0 || row < 0 || row >= m_items.size()) {
        return;
    }

    const int actualCount = std::min(count, static_cast<int>(m_items.size() - row));
    const int oldCurrent = m_order.current();

    beginRemoveRows(QModelIndex(), row, row + actualCount - 1);
    m_items.remove(row, actualCount);
    m_order.onRemoved(row, actualCount);
    endRemoveRows();

    Q_ASSERT(m_order.count() == m_items.size());

    emit countChanged(static_cast<int>(m_items.size()));
    notifyCurrentIndexChanged(oldCurrent, m_order.current());
    emit upcomingChanged();
}

void PlayQueue::move(int from, int to)
{
    if (from < 0 || from >= m_items.size() || to < 0 || to >= m_items.size() || from == to) {
        return;
    }

    const int oldCurrent = m_order.current();
    const int destChild = (from < to) ? (to + 1) : to;

    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), destChild)) {
        return;
    }
    m_items.move(from, to);
    m_order.onMoved(from, to);
    endMoveRows();

    Q_ASSERT(m_order.count() == m_items.size());

    notifyCurrentIndexChanged(oldCurrent, m_order.current());
    emit upcomingChanged();
}

void PlayQueue::clear()
{
    if (m_items.isEmpty()) {
        return;
    }

    const int oldCount = static_cast<int>(m_items.size());
    const int oldCurrent = m_order.current();

    beginResetModel();
    m_items.clear();
    m_order.reset(0, -1);
    endResetModel();

    Q_ASSERT(m_order.count() == m_items.size());

    if (oldCount != 0) {
        emit countChanged(0);
    }
    if (oldCurrent != -1) {
        emit currentIndexChanged(-1);
    }
    emit upcomingChanged();
}

std::optional<QueueItem> PlayQueue::peekNext(PlayOrder::Advance advance) const
{
    const auto nextIndex = m_order.peekNext(advance);
    if (nextIndex.has_value() && nextIndex.value() >= 0 && nextIndex.value() < m_items.size()) {
        return m_items.at(nextIndex.value());
    }
    return std::nullopt;
}

std::optional<QueueItem> PlayQueue::advance(PlayOrder::Advance advance)
{
    const int oldCurrent = m_order.current();
    const auto nextIndex = m_order.advance(advance);
    notifyCurrentIndexChanged(oldCurrent, m_order.current());
    emit upcomingChanged();

    if (nextIndex.has_value() && nextIndex.value() >= 0 && nextIndex.value() < m_items.size()) {
        return m_items.at(nextIndex.value());
    }
    return std::nullopt;
}

std::optional<QueueItem> PlayQueue::previous()
{
    const int oldCurrent = m_order.current();
    const auto prevIndex = m_order.previous();
    notifyCurrentIndexChanged(oldCurrent, m_order.current());
    emit upcomingChanged();

    if (prevIndex.has_value() && prevIndex.value() >= 0 && prevIndex.value() < m_items.size()) {
        return m_items.at(prevIndex.value());
    }
    return std::nullopt;
}

std::optional<QueueItem> PlayQueue::jumpTo(int row)
{
    if (row < 0 || row >= m_items.size()) {
        return std::nullopt;
    }

    const int oldCurrent = m_order.current();
    m_order.jumpTo(row);
    notifyCurrentIndexChanged(oldCurrent, m_order.current());
    emit upcomingChanged();

    return m_items.at(row);
}

} // namespace linernotes::player
