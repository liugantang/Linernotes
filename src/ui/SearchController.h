// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <cstdint>

class QTimer;

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::ui {

class SearchController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SearchController)

    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY queryChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList albums READ albums NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList artists READ artists NOTIFY resultsChanged)
    Q_PROPERTY(bool searching READ isSearching NOTIFY searchingChanged)

public:
    explicit SearchController(library::Database &db, QObject *parent = nullptr);
    ~SearchController() override = default;

    [[nodiscard]] QString query() const;
    void setQuery(const QString &q);

    [[nodiscard]] bool isActive() const;
    [[nodiscard]] bool isSearching() const;

    [[nodiscard]] QVariantList tracks() const;
    [[nodiscard]] QVariantList albums() const;
    [[nodiscard]] QVariantList artists() const;

    Q_INVOKABLE QList<qint64> trackIds() const;
    Q_INVOKABLE void clear();
    Q_INVOKABLE void refresh();

signals:
    void queryChanged();
    void resultsChanged();
    void searchingChanged();

private:
    void performSearch();

    library::Database &m_db;
    QString m_query;
    bool m_searching { false };
    QTimer *m_debounceTimer { nullptr };

    QVariantList m_tracks;
    QVariantList m_albums;
    QVariantList m_artists;
    QList<qint64> m_trackIds;
};

} // namespace linernotes::ui
