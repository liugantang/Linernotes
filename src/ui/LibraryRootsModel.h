// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QUrl>

#include <library/LibraryRoots.h>

#include <cstdint>

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::ui {

class LibraryRootsModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LibraryRootsModel)

public:
    enum Role : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt 模型角色需与 int
                                // 互转
        RootIdRole = Qt::UserRole + 1,
        PathRole,
        EnabledRole,
    };
    Q_ENUM(Role)

    explicit LibraryRootsModel(library::Database &db, QObject *parent = nullptr);
    ~LibraryRootsModel() override = default;

    [[nodiscard]] int rowCount(const QModelIndex &parent = { }) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool addRoot(const QUrl &folder);
    Q_INVOKABLE bool removeRoot(qint64 id);
    Q_INVOKABLE bool setRootEnabled(qint64 id, bool enabled);

signals:
    void rootsChanged();

private:
    library::Database &m_db;
    QList<library::LibraryRoot> m_roots;
};

} // namespace linernotes::ui
