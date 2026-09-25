// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include <player/PlayMode.h>
#include <player/PlayOrder.h>

#include <cstdint>
#include <optional>

namespace linernotes::player {

struct QueueItem {
    QString source; // 本地文件路径
    qint64 trackId = -1; // 曲库 ID，阶段 2 之后才有，现在为 -1
    quint64 uid = 0; // 队列内唯一标识，由 PlayQueue 在加入时分配（调用方传入的值被忽略）

    bool operator==(const QueueItem &other) const = default;
};

class PlayQueue : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(linernotes::player::PlayMode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { SourceRole = Qt::UserRole + 1, TrackIdRole, UidRole, IsCurrentRole };
    Q_ENUM(Role)

    explicit PlayQueue(quint64 seed, QObject *parent = nullptr);
    ~PlayQueue() override = default;

    // QAbstractListModel
    [[nodiscard]] int rowCount(const QModelIndex &parent = { }) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role) const override; // Qt::DisplayRole 返回文件名
    [[nodiscard]] QHash<int, QByteArray>
    roleNames() const override; // "source" "trackId" "uid" "isCurrent"

    [[nodiscard]] int count() const;
    [[nodiscard]] const QueueItem &at(int row) const; // 越界是编程错误，Q_ASSERT
    [[nodiscard]] int rowOfUid(quint64 uid) const; // 找不到返回 -1
    [[nodiscard]] int currentIndex() const; // -1 表示无当前
    [[nodiscard]] std::optional<QueueItem> currentItem() const;
    [[nodiscard]] PlayMode mode() const;
    void setMode(PlayMode mode);

    // 编辑（F-PLY-03）
    void setItems(const QList<QueueItem> &items, int startIndex = -1); // 整体替换，beginResetModel
    void append(const QList<QueueItem> &items);
    void insert(int row, const QList<QueueItem> &items);
    /// “下一首播放”：插到当前曲目之后（无当前时插到最前），并通过 PlayOrder::scheduleNext
    /// 保证它们紧接着按给定顺序播放（Shuffle 下也成立）。
    void insertNext(const QList<QueueItem> &items);
    void remove(int row, int count = 1);
    void move(int from, int to); // 单行移动，to 为移动后的下标；用 beginMoveRows（注意 Qt 对
                                 // destinationChild 的约定）
    void clear();

    // 导航（委托给 PlayOrder）
    [[nodiscard]] std::optional<QueueItem> peekNext(PlayOrder::Advance advance) const;
    std::optional<QueueItem> advance(PlayOrder::Advance advance);
    std::optional<QueueItem> previous();
    std::optional<QueueItem> jumpTo(int row); // 越界返回 nullopt

signals:
    void currentIndexChanged(int index);
    void modeChanged(linernotes::player::PlayMode mode);
    void countChanged(int count);
    /// 任何可能改变“当前/下一首”的操作之后都发出（增删移动、清空、setItems、模式切换、导航、scheduleNext），
    /// Player 据此重新计算需要向 mpv 预加载的下一首。允许多发，不允许漏发。
    void upcomingChanged();

private:
    void notifyCurrentIndexChanged(int oldCurrent, int newCurrent);

    QList<QueueItem> m_items;
    PlayOrder m_order;
    quint64 m_nextUid = 1;
};

} // namespace linernotes::player
