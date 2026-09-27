// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QTest>

#include <core/Result.h>
#include <ui/PageCache.h>

using linernotes::ui::PageCache;

namespace {

class TstPageCache : public QObject {
    Q_OBJECT

private slots:
    void samePageNoDuplicateLoad();
    void lruEvictionAfterMaxPages();
    void loadFailureReturnsNullptr();
};

void TstPageCache::samePageNoDuplicateLoad()
{
    int loaderCalls = 0;
    PageCache<int> cache(4, 5);
    auto loader = [&loaderCalls](int offset, int limit) -> linernotes::core::Result<QList<int>> {
        ++loaderCalls;
        QList<int> result;
        for (int i = 0; i < limit; ++i) {
            result.append(offset + i);
        }
        return result;
    };

    // First access to page 0 (row 0) -> calls loader
    const int *v0 = cache.row(0, loader);
    QVERIFY(v0 != nullptr);
    QCOMPARE(*v0, 0);
    QCOMPARE(loaderCalls, 1);

    // Accessing row 1 and row 3 (still within page 0) -> hits cache, no extra loader calls
    const int *v1 = cache.row(1, loader);
    QVERIFY(v1 != nullptr);
    QCOMPARE(*v1, 1);
    QCOMPARE(loaderCalls, 1);

    const int *v3 = cache.row(3, loader);
    QVERIFY(v3 != nullptr);
    QCOMPARE(*v3, 3);
    QCOMPARE(loaderCalls, 1);

    // Negative index returns nullptr without calling loader
    QCOMPARE(cache.row(-1, loader), nullptr);
    QCOMPARE(loaderCalls, 1);
}

void TstPageCache::lruEvictionAfterMaxPages()
{
    int loaderCalls = 0;
    PageCache<int> cache(2, 2);
    auto loader = [&loaderCalls](int offset, int limit) -> linernotes::core::Result<QList<int>> {
        ++loaderCalls;
        QList<int> result;
        for (int i = 0; i < limit; ++i) {
            result.append(offset + i);
        }
        return result;
    };

    // Access page 0 (rows 0, 1) -> loader called (count = 1)
    const int *r0 = cache.row(0, loader);
    QVERIFY(r0 != nullptr);
    QCOMPARE(*r0, 0);
    QCOMPARE(loaderCalls, 1);

    // Access page 1 (rows 2, 3) -> loader called (count = 2)
    const int *r2 = cache.row(2, loader);
    QVERIFY(r2 != nullptr);
    QCOMPARE(*r2, 2);
    QCOMPARE(loaderCalls, 2);

    // Access page 0 again -> cache hit, page 0 becomes MRU, page 1 is LRU
    const int *r1 = cache.row(1, loader);
    QVERIFY(r1 != nullptr);
    QCOMPARE(*r1, 1);
    QCOMPARE(loaderCalls, 2);

    // Access page 2 (rows 4, 5) -> exceeds maxPages (2), evicts LRU (page 1)
    const int *r4 = cache.row(4, loader);
    QVERIFY(r4 != nullptr);
    QCOMPARE(*r4, 4);
    QCOMPARE(loaderCalls, 3);

    // Page 0 should still be cached
    const int *r0Again = cache.row(0, loader);
    QVERIFY(r0Again != nullptr);
    QCOMPARE(*r0Again, 0);
    QCOMPARE(loaderCalls, 3);

    // Page 1 was evicted, so accessing row 2 calls loader again (count = 4)
    const int *r2Again = cache.row(2, loader);
    QVERIFY(r2Again != nullptr);
    QCOMPARE(*r2Again, 2);
    QCOMPARE(loaderCalls, 4);
}

void TstPageCache::loadFailureReturnsNullptr()
{
    int loaderCalls = 0;
    PageCache<int> cache(4, 5);
    auto failingLoader = [&loaderCalls](int, int) -> linernotes::core::Result<QList<int>> {
        ++loaderCalls;
        return linernotes::core::Error {
            .code = QStringLiteral("db.error"),
            .message = QStringLiteral("database query failed"),
            .detail = QString(),
        };
    };

    // First attempt fails and returns nullptr
    const int *v0 = cache.row(0, failingLoader);
    QCOMPARE(v0, nullptr);
    QCOMPARE(loaderCalls, 1);

    // Subsequent access to same page returns nullptr without calling loader again
    const int *v1 = cache.row(1, failingLoader);
    QCOMPARE(v1, nullptr);
    QCOMPARE(loaderCalls, 1);

    // After clear(), loader is called again
    cache.clear();
    const int *v2 = cache.row(1, failingLoader);
    QCOMPARE(v2, nullptr);
    QCOMPARE(loaderCalls, 2);
}

} // namespace

QTEST_GUILESS_MAIN(TstPageCache)
#include "tst_PageCache.moc"
