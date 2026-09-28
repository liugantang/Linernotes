// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include "EvalHarness.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

namespace linernotes::eval {

struct CorpusArtist {
    QString name { };
    QString entity { };
    QStringList albums;
};

struct MisMergeItem {
    QString alias { };
    QString canonical { };
    QString source { };
    double confidence = 0.0;
    QString reason { };
    QString aliasEntity { };
    QString canonicalEntity { };
};

struct MissedPairItem {
    QString nameA { };
    QString nameB { };
    QString entity { };
};

struct SourceReport {
    int proposals = 0;
    int mismerges = 0;
};

class ArtistMergeEval : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ArtistMergeEval)

public:
    explicit ArtistMergeEval(EvalConfig config, QObject *parent = nullptr);
    ~ArtistMergeEval() override;

    bool init();
    void start();

private:
    bool loadCorpus();
    bool importCorpusToDb();
    void evaluateAndFinish();
    void printReport(const QList<MisMergeItem> &mismerges, const QList<MissedPairItem> &missedPairs,
        const QMap<QString, SourceReport> &sourceStats, int totalProposals, int totalMismerges,
        double mismergeRate, int totalGtPairs, int mergedGtPairs, double recall, qint64 elapsedMs);

    EvalHarness m_harness;

    QList<CorpusArtist> m_corpus;
    QHash<QString, QString> m_nameToEntity;
    QHash<QString, QStringList> m_entityToNames;

    qint64 m_batchId = 0;
};

} // namespace linernotes::eval
