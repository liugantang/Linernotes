// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QTextStream>

#include <butler/ArtistCluster.h>
#include <butler/ArtistName.h>
#include <common/TestSupport.h>

namespace {

using linernotes::butler::ArtistCluster;
using linernotes::butler::ArtistEntry;
using linernotes::butler::clusterArtists;
using linernotes::butler::exactKey;
using linernotes::butler::romanTokens;
using linernotes::test::fixturePath;

QList<QStringList> loadLines(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return { };
    }
    QTextStream in(&file);
    QList<QStringList> lines;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QStringList parts = line.split(QStringLiteral(" | "), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            lines.append(parts);
        }
    }
    return lines;
}

struct DatasetEntries {
    QList<ArtistEntry> allEntries;
    QHash<QString, qint64> nameToId;
};

void appendGroupEntries(
    const QList<QStringList> &groups, DatasetEntries &dataset, qint64 &nextId, int &trackCount)
{
    for (const auto &group : groups) {
        for (const QString &rawName : group) {
            const QString name = rawName.trimmed();
            if (!dataset.nameToId.contains(name)) {
                const qint64 id = nextId++;
                dataset.nameToId.insert(name, id);
                dataset.allEntries.append(ArtistEntry {
                    .artistId = id,
                    .name = name,
                    .trackCount = trackCount--,
                });
            }
        }
    }
}

DatasetEntries buildUniqueEntries(
    const QList<QStringList> &sameGroups, const QList<QStringList> &distGroups)
{
    DatasetEntries dataset;
    qint64 nextId = 1;
    int trackCount = 1000;
    appendGroupEntries(sameGroups, dataset, nextId, trackCount);
    appendGroupEntries(distGroups, dataset, nextId, trackCount);
    return dataset;
}

void verifySameGroupsFormClusters(const QList<QStringList> &sameGroups,
    const QHash<QString, qint64> &nameToId, const QHash<qint64, int> &idToClusterIndex)
{
    for (const auto &group : sameGroups) {
        QVERIFY(!group.isEmpty());
        const qint64 firstId = nameToId.value(group.at(0).trimmed(), -1);
        QVERIFY(firstId != -1);
        QVERIFY(idToClusterIndex.contains(firstId));
        const int expectedClusterIdx = idToClusterIndex.value(firstId);

        for (const QString &rawName : group) {
            const qint64 id = nameToId.value(rawName.trimmed(), -1);
            QVERIFY(id != -1);
            QVERIFY(idToClusterIndex.contains(id));
            QCOMPARE(idToClusterIndex.value(id), expectedClusterIdx);
        }
    }
}

void verifyClustersMatchSameGroups(const QList<ArtistCluster> &clusters,
    const QList<QStringList> &sameGroups, const QHash<QString, qint64> &nameToId)
{
    QHash<qint64, int> idToGroupIndex;
    for (qsizetype g = 0; g < sameGroups.size(); ++g) {
        for (const QString &rawName : sameGroups.at(g)) {
            const qint64 id = nameToId.value(rawName.trimmed(), -1);
            idToGroupIndex.insert(id, static_cast<int>(g));
        }
    }

    for (const auto &cluster : clusters) {
        QVERIFY(!cluster.members.isEmpty());
        const qint64 firstId = cluster.members.first().artistId;
        QVERIFY2(idToGroupIndex.contains(firstId),
            qPrintable(cluster.members.first().name + QStringLiteral(" ~ ")
                + cluster.members.last().name));
        const int expectedGroupIndex = idToGroupIndex.value(firstId);

        for (const auto &member : cluster.members) {
            QCOMPARE(idToGroupIndex.value(member.artistId, -1), expectedGroupIndex);
        }
        QCOMPARE(cluster.members.size(), sameGroups.at(expectedGroupIndex).size());
    }
    QCOMPARE(clusters.size(), sameGroups.size());
}

class TstArtistName : public QObject {
    Q_OBJECT

private slots:
    void exactKeyNormalizes_data();
    void exactKeyNormalizes();
    void romanTokensHandlesOrderAndLongVowels();
    void realClustersAreRuleMergeable();
    void distinctArtistsNeverRuleMerged();
    void allRealDataTogether();
};

void TstArtistName::exactKeyNormalizes_data()
{
    QTest::addColumn<QString>("input1");
    QTest::addColumn<QString>("input2");
    QTest::addColumn<QString>("expectedKey");

    QTest::newRow("fullwidth/halfwidth")
        << QStringLiteral("Pastel＊Palettes") << QStringLiteral("Pastel*Palettes")
        << QStringLiteral("pastelpalettes");

    QTest::newRow("case") << QStringLiteral("GALNERYUS") << QStringLiteral("Galneryus")
                          << QStringLiteral("galneryus");

    QTest::newRow("traditional/simplified")
        << QStringLiteral("周杰倫") << QStringLiteral("周杰伦") << QStringLiteral("周杰伦");

    QTest::newRow("punctuation") << QStringLiteral("May’n") << QStringLiteral("May'n")
                                 << QStringLiteral("mayn");

    QTest::newRow("space") << QStringLiteral("佐々木 淳") << QStringLiteral("佐々木淳")
                           << QStringLiteral("佐々木淳");
}

void TstArtistName::exactKeyNormalizes()
{
    QFETCH(QString, input1);
    QFETCH(QString, input2);
    QFETCH(QString, expectedKey);

    QCOMPARE(exactKey(input1), expectedKey);
    QCOMPARE(exactKey(input2), expectedKey);
}

void TstArtistName::romanTokensHandlesOrderAndLongVowels()
{
    const QStringList expected { QStringLiteral("ito"), QStringLiteral("kanako") };
    QCOMPARE(romanTokens(QStringLiteral("Kanako Itou")), expected);
    QCOMPARE(romanTokens(QStringLiteral("Itou Kanako")), expected);
    QCOMPARE(romanTokens(QStringLiteral("Kanako Ito")), expected);

    // 含假名/汉字/韩文的名字返回空
    QVERIFY(romanTokens(QStringLiteral("佐々木 淳")).isEmpty());
    QVERIFY(romanTokens(QStringLiteral("雨の日の散歩")).isEmpty());
    QVERIFY(romanTokens(QStringLiteral("林晓风")).isEmpty());
    QVERIFY(romanTokens(QStringLiteral("새벽의 노래")).isEmpty());
}

void TstArtistName::realClustersAreRuleMergeable()
{
    const QString filePath = fixturePath(QStringLiteral("artists/same_artist_rule.txt"));
    const auto groups = loadLines(filePath);
    QVERIFY(!groups.isEmpty());

    qint64 nextId = 1;
    for (const auto &names : groups) {
        QList<ArtistEntry> entries;
        int trackCount = 100;
        for (const QString &name : names) {
            entries.append(ArtistEntry {
                .artistId = nextId++,
                .name = name.trimmed(),
                .trackCount = trackCount--,
            });
        }

        const auto clustering = clusterArtists(entries);
        QCOMPARE(clustering.clusters.size(), 1);
        const auto &cluster = clustering.clusters.at(0);
        QCOMPARE(cluster.members.size(), entries.size());
    }
}

void TstArtistName::distinctArtistsNeverRuleMerged()
{
    const QString filePath = fixturePath(QStringLiteral("artists/distinct_artists.txt"));
    const auto groups = loadLines(filePath);
    QVERIFY(!groups.isEmpty());

    qint64 nextId = 1;
    for (const auto &names : groups) {
        QList<ArtistEntry> entries;
        int trackCount = 10;
        for (const QString &name : names) {
            entries.append(ArtistEntry {
                .artistId = nextId++,
                .name = name.trimmed(),
                .trackCount = trackCount--,
            });
        }

        const auto clustering = clusterArtists(entries);
        QVERIFY(clustering.clusters.isEmpty());
    }
}

void TstArtistName::allRealDataTogether()
{
    const QString samePath = fixturePath(QStringLiteral("artists/same_artist_rule.txt"));
    const QString distPath = fixturePath(QStringLiteral("artists/distinct_artists.txt"));

    const auto sameGroups = loadLines(samePath);
    const auto distGroups = loadLines(distPath);
    QVERIFY(!sameGroups.isEmpty());
    QVERIFY(!distGroups.isEmpty());

    const auto dataset = buildUniqueEntries(sameGroups, distGroups);
    const auto clustering = clusterArtists(dataset.allEntries);

    QHash<qint64, int> idToClusterIndex;
    for (qsizetype i = 0; i < clustering.clusters.size(); ++i) {
        for (const auto &m : clustering.clusters.at(i).members) {
            idToClusterIndex.insert(m.artistId, static_cast<int>(i));
        }
    }

    verifySameGroupsFormClusters(sameGroups, dataset.nameToId, idToClusterIndex);
    verifyClustersMatchSameGroups(clustering.clusters, sameGroups, dataset.nameToId);
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistName)

#include "tst_ArtistName.moc"
