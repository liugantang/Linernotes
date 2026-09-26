// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LibraryRootsModel.h"

#include "UiLogging.h"

#include <QDir>
#include <QFileInfo>

#include <library/Database.h>
#include <library/LibraryRoots.h>

namespace linernotes::ui {

LibraryRootsModel::LibraryRootsModel(library::Database &db, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(db)
{
    refresh();
}

int LibraryRootsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_roots.size());
}

QVariant LibraryRootsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_roots.size()) {
        return { };
    }

    const auto &root = m_roots.at(index.row());
    switch (role) {
    case RootIdRole:
        return root.id;
    case PathRole:
        return root.path;
    case EnabledRole:
        return root.enabled;
    default:
        return { };
    }
}

QHash<int, QByteArray> LibraryRootsModel::roleNames() const
{
    return {
        { RootIdRole, "rootId" },
        { PathRole, "path" },
        { EnabledRole, "enabled" },
    };
}

void LibraryRootsModel::refresh()
{
    if (!m_db.isOpen()) {
        beginResetModel();
        m_roots.clear();
        endResetModel();
        return;
    }

    const library::LibraryRoots roots(m_db);
    const auto res = roots.list();
    if (!res.ok()) {
        qCWarning(lcUi) << "Failed to list library roots:" << res.error().toString();
        return;
    }

    beginResetModel();
    m_roots = res.value();
    endResetModel();
}

bool LibraryRootsModel::addRoot(const QUrl &folder)
{
    const QString localPath = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (localPath.isEmpty()) {
        qCWarning(lcUi) << "Cannot add library root: empty or invalid folder URL" << folder;
        return false;
    }

    const QFileInfo fi(localPath);
    if (!fi.exists() || !fi.isDir()) {
        qCWarning(lcUi) << "Cannot add library root: path does not exist or is not a directory:"
                        << localPath;
        return false;
    }

    QString cleanPath = QDir::cleanPath(fi.absoluteFilePath());
    if (cleanPath.length() > 1 && cleanPath.endsWith(u'/')) {
        cleanPath.chop(1);
    }

    for (const auto &root : m_roots) {
        if (root.path == cleanPath) {
            qCWarning(lcUi) << "Library root already exists:" << cleanPath;
            return false;
        }
    }

    library::LibraryRoots roots(m_db);
    const auto addRes = roots.add(cleanPath);
    if (!addRes.ok()) {
        qCWarning(lcUi) << "Failed to add library root:" << addRes.error().toString();
        return false;
    }

    refresh();
    emit rootsChanged();
    return true;
}

bool LibraryRootsModel::removeRoot(qint64 id)
{
    library::LibraryRoots roots(m_db);
    const auto res = roots.remove(id);
    if (!res.ok()) {
        qCWarning(lcUi) << "Failed to remove library root" << id << ":" << res.error().toString();
        return false;
    }

    refresh();
    emit rootsChanged();
    return true;
}

bool LibraryRootsModel::setRootEnabled(qint64 id, bool enabled)
{
    library::LibraryRoots roots(m_db);
    const auto res = roots.setEnabled(id, enabled);
    if (!res.ok()) {
        qCWarning(lcUi) << "Failed to set library root enabled" << id << enabled << ":"
                        << res.error().toString();
        return false;
    }

    refresh();
    emit rootsChanged();
    return true;
}

} // namespace linernotes::ui
