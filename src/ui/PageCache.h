// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>

#include <core/Result.h>

#include <algorithm>
#include <functional>
#include <list>
#include <unordered_map>
#include <unordered_set>

namespace linernotes::ui {

template <typename Row> class PageCache {
public:
    using Loader = std::function<core::Result<QList<Row>>(int offset, int limit)>;

    explicit PageCache(int pageSize = 200, int maxPages = 32)
        : m_pageSize(std::max(1, pageSize))
        , m_maxPages(std::max(1, maxPages))
    {
    }

    void setPageSize(int pageSize)
    {
        m_pageSize = std::max(1, pageSize);
        clear();
    }

    const Row *row(int index, const Loader &loader)
    {
        if (index < 0) {
            return nullptr;
        }

        const int pageIndex = index / m_pageSize;
        const int offsetInPage = index % m_pageSize;

        if (m_failedPages.contains(pageIndex)) {
            return nullptr;
        }

        auto it = m_pages.find(pageIndex);
        if (it != m_pages.end()) {
            m_lruList.splice(m_lruList.begin(), m_lruList, it->second.lruIt);
            if (offsetInPage < it->second.data.size()) {
                return &it->second.data.at(offsetInPage);
            }
            return nullptr;
        }

        const auto res = loader(pageIndex * m_pageSize, m_pageSize);
        if (!res.ok()) {
            m_failedPages.insert(pageIndex);
            return nullptr;
        }

        if (static_cast<int>(m_pages.size()) >= m_maxPages && !m_lruList.empty()) {
            const int lruPageIndex = m_lruList.back();
            m_lruList.pop_back();
            m_pages.erase(lruPageIndex);
        }

        m_lruList.push_front(pageIndex);
        auto insertRes = m_pages.emplace(pageIndex, PageEntry { res.value(), m_lruList.begin() });
        const auto &pageData = insertRes.first->second.data;
        if (offsetInPage < pageData.size()) {
            return &pageData.at(offsetInPage);
        }
        return nullptr;
    }

    void clear()
    {
        m_pages.clear();
        m_lruList.clear();
        m_failedPages.clear();
    }

    [[nodiscard]] int pageSize() const { return m_pageSize; }

    [[nodiscard]] int maxPages() const { return m_maxPages; }

    [[nodiscard]] int cachedPageCount() const { return static_cast<int>(m_pages.size()); }

private:
    struct PageEntry {
        QList<Row> data;
        std::list<int>::iterator lruIt;
    };

    int m_pageSize;
    int m_maxPages;
    std::unordered_map<int, PageEntry> m_pages;
    std::list<int> m_lruList;
    std::unordered_set<int> m_failedPages;
};

} // namespace linernotes::ui
