// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include "EvalHarness.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace linernotes::eval {

struct CreditCorpusItem {
    QString value { };
    QStringList performers;
    QHash<QString, QStringList> aka;
};

struct CreditErrorItem {
    QString value { };
    QStringList expectedPerformers;
    QHash<QString, QStringList> expectedAka;
    QStringList actualPerformers;
    QHash<QString, QStringList> actualAka;
    QString reason { };
    bool unparsed = false;
};

struct CreditChangedItem {
    QString originalValue { };
    QString normalizedValue { };
    QStringList roles;
    double confidence = 0.0;
    QString reason { };
};

struct CreditAkaItem {
    QString originalValue { };
    QString performer { };
    QStringList aka;
};

class ArtistCreditEval : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ArtistCreditEval)

public:
    explicit ArtistCreditEval(EvalConfig config, QObject *parent = nullptr);
    ~ArtistCreditEval() override;

    bool init();
    void start();

private:
    [[nodiscard]] bool isLibraryMode() const;
    bool loadCorpus();
    bool importCorpusToDb();
    void evaluateAndFinish();
    void evaluateCorpusMode();
    void evaluateLibraryMode();

    EvalHarness m_harness;
    QList<CreditCorpusItem> m_corpus;
    qint64 m_batchId = 0;
};

} // namespace linernotes::eval
