// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QAbstractListModel>
#include <QString>
#include <Qt>

#include <core/Result.h>
#include <library/LibraryQuery.h>
#include <ui/AppContext.h>

#include <optional>
#include <unordered_set>

namespace linernotes::ui {

class PagedListModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PagedListModel)

    Q_PROPERTY(
        linernotes::ui::AppContext *context READ context WRITE setContext NOTIFY contextChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(Qt::SortOrder sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)
    Q_PROPERTY(
        bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY favoritesOnlyChanged)
    Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY pageSizeChanged)

public:
    explicit PagedListModel(QObject *parent = nullptr);
    ~PagedListModel() override = default;

    // QAbstractListModel interface
    [[nodiscard]] int rowCount(const QModelIndex &parent = { }) const override;

    [[nodiscard]] AppContext *context() const;
    void setContext(AppContext *context);

    [[nodiscard]] int count() const;

    [[nodiscard]] Qt::SortOrder sortOrder() const;
    void setSortOrder(Qt::SortOrder order);

    [[nodiscard]] bool favoritesOnly() const;
    void setFavoritesOnly(bool favoritesOnly);

    [[nodiscard]] int pageSize() const;
    void setPageSize(int size);

signals:
    void contextChanged();
    void countChanged(int count);
    void sortOrderChanged(Qt::SortOrder order);
    void favoritesOnlyChanged(bool favoritesOnly);
    void pageSizeChanged(int pageSize);

protected:
    /// 子类实现：当前筛选下的总行数
    virtual core::Result<int> queryCount(const library::LibraryQuery &query) const = 0;
    /// 子类在筛选/排序属性变化时调用：reset 并重新计数
    void reload();
    /// 子类的缓存需要清空时由基类调用
    virtual void clearCache() = 0;
    /// 取当前连接构造的 LibraryQuery；context 为空或未就绪返回 nullopt
    [[nodiscard]] std::optional<library::LibraryQuery> makeQuery() const;
    /// 同一页的错误只记一次（清缓存或重载时重置）
    void warnOnce(int page, const QString &message) const;

private:
    void onLibraryChanged();

    AppContext *m_context { nullptr };
    int m_count { 0 };
    Qt::SortOrder m_sortOrder { Qt::AscendingOrder };
    bool m_favoritesOnly { false };
    int m_pageSize { 200 };
    mutable std::unordered_set<int> m_warnedPages;
};

} // namespace linernotes::ui
