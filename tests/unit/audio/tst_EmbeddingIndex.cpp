// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>

#include <audio/EmbeddingIndex.h>

#include <array>
#include <vector>

namespace {

using linernotes::audio::EmbeddingIndex;

class TstEmbeddingIndex : public QObject {
    Q_OBJECT

private slots:
    void nearestSortsDescendingExcludingSelf();
    void nearestWithKLargerThanSize();
    void nearestWithUnknownIdReturnsEmpty();
    void nearestWithQueryVector();
    void ignoresDimensionMismatch();
};

void TstEmbeddingIndex::nearestSortsDescendingExcludingSelf()
{
    EmbeddingIndex index(3);

    // v1: (1, 0, 0)
    const std::array<float, 3> v1 { 1.0F, 0.0F, 0.0F };
    // v2: (0.8, 0.6, 0) - dot with v1 is 0.8
    const std::array<float, 3> v2 { 0.8F, 0.6F, 0.0F };
    // v3: (0, 1, 0) - dot with v1 is 0.0
    const std::array<float, 3> v3 { 0.0F, 1.0F, 0.0F };
    // v4: (-1, 0, 0) - dot with v1 is -1.0
    const std::array<float, 3> v4 { -1.0F, 0.0F, 0.0F };

    index.add(1, v1);
    index.add(2, v2);
    index.add(3, v3);
    index.add(4, v4);

    QCOMPARE(index.size(), 4);
    QVERIFY(index.contains(1));
    QVERIFY(index.contains(2));
    QVERIFY(index.contains(3));
    QVERIFY(index.contains(4));
    QVERIFY(!index.contains(999));

    // Nearest to id 1, k=2: should return [2 (score 0.8), 3 (score 0.0)], NOT including 1
    const auto res = index.nearest(1, 2);
    QCOMPARE(res.size(), 2);
    QCOMPARE(res.at(0).id, 2);
    QVERIFY(std::abs(res.at(0).score - 0.8F) < 1e-4F);
    QCOMPARE(res.at(1).id, 3);
    QVERIFY(std::abs(res.at(1).score - 0.0F) < 1e-4F);
}

void TstEmbeddingIndex::nearestWithKLargerThanSize()
{
    EmbeddingIndex index(3);
    const std::array<float, 3> v1 { 1.0F, 0.0F, 0.0F };
    const std::array<float, 3> v2 { 0.0F, 1.0F, 0.0F };
    index.add(10, v1);
    index.add(20, v2);

    // k = 10, total other items = 1
    const auto res = index.nearest(10, 10);
    QCOMPARE(res.size(), 1);
    QCOMPARE(res.at(0).id, 20);
    QVERIFY(std::abs(res.at(0).score - 0.0F) < 1e-4F);
}

void TstEmbeddingIndex::nearestWithUnknownIdReturnsEmpty()
{
    EmbeddingIndex index(3);
    const std::array<float, 3> v1 { 1.0F, 0.0F, 0.0F };
    index.add(10, v1);

    const auto res = index.nearest(999, 5);
    QVERIFY(res.isEmpty());
}

void TstEmbeddingIndex::nearestWithQueryVector()
{
    EmbeddingIndex index(4);

    const std::array<float, 4> v1 { 1.0F, 0.0F, 0.0F, 0.0F };
    const std::array<float, 4> v2 { 0.0F, 1.0F, 0.0F, 0.0F };
    const std::array<float, 4> v3 { 0.0F, 0.0F, 1.0F, 0.0F };

    index.add(100, v1);
    index.add(200, v2);
    index.add(300, v3);

    // Query is (0.6, 0.8, 0, 0) - dot with v1 is 0.6, with v2 is 0.8, with v3 is 0.0
    const std::array<float, 4> query { 0.6F, 0.8F, 0.0F, 0.0F };
    const auto res = index.nearest(query, 3);

    QCOMPARE(res.size(), 3);
    QCOMPARE(res.at(0).id, 200);
    QVERIFY(std::abs(res.at(0).score - 0.8F) < 1e-4F);
    QCOMPARE(res.at(1).id, 100);
    QVERIFY(std::abs(res.at(1).score - 0.6F) < 1e-4F);
    QCOMPARE(res.at(2).id, 300);
    QVERIFY(std::abs(res.at(2).score - 0.0F) < 1e-4F);
}

void TstEmbeddingIndex::ignoresDimensionMismatch()
{
    EmbeddingIndex index(3);
    const std::array<float, 2> shortVec { 1.0F, 0.0F };
    const std::array<float, 4> longVec { 1.0F, 0.0F, 0.0F, 0.0F };

    index.add(1, shortVec);
    index.add(2, longVec);

    QCOMPARE(index.size(), 0);
    QVERIFY(!index.contains(1));
    QVERIFY(!index.contains(2));
}

} // namespace

QTEST_GUILESS_MAIN(TstEmbeddingIndex)

#include "tst_EmbeddingIndex.moc"
