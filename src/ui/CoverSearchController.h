// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

#include <butler/ItunesSearch.h>

#include <cstdint>

class QNetworkAccessManager;
class QNetworkReply;

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
class CoverStore;
} // namespace linernotes::library

namespace linernotes::ui {

class CoverSearchController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(CoverSearchController)

    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

public:
    CoverSearchController(library::Database &db, QNetworkAccessManager &network,
        library::CoverStore &coverStore, const core::Clock &clock, QObject *parent = nullptr);
    ~CoverSearchController() override;

    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] QVariantList results() const;
    [[nodiscard]] QString errorText() const;

    Q_INVOKABLE void search(qint64 albumId);
    Q_INVOKABLE void choose(int index);
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void resultsChanged();
    void errorTextChanged();
    void coverChanged(qint64 albumId);

private:
    struct SearchTarget {
        QString term;
        QString country;
    };

    void cancelInternal();
    void startNextSearchRequest();
    void onSearchReplyFinished(const QString &country);
    void onDownloadReplyFinished(const QUrl &downloadUrl);
    void updateResultsProperty();

    library::Database &m_db;
    QNetworkAccessManager &m_network;
    library::CoverStore &m_coverStore;
    const core::Clock &m_clock;

    bool m_busy = false;
    qint64 m_albumId = 0;
    QList<butler::ItunesAlbum> m_albums;
    QVariantList m_results;
    QString m_errorText;

    QList<SearchTarget> m_pendingTargets;
    QPointer<QNetworkReply> m_currentReply;
    QPointer<QNetworkReply> m_downloadReply;
    int m_totalCount = 0;
    int m_failedCount = 0;
};

} // namespace linernotes::ui
