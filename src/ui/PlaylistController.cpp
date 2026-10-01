// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QMetaEnum>
#include <QVariant>
#include <QVariantMap>

#include <library/Database.h>
#include <library/EnumNames.h>
#include <library/LibraryQuery.h>
#include <library/SmartRule.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/PlaylistController.h>
#include <ui/SmartLabels.h>
#include <ui/UiLogging.h>

namespace linernotes::ui {

PlaylistController::PlaylistController(library::Database &db, player::Player &player)
    : m_store(db)
    , m_player(player)
{
}

PlaylistListModel *PlaylistController::model()
{
    return &m_model;
}

void PlaylistController::refresh()
{
    const auto listRes = m_store.list();
    if (!listRes.ok()) {
        qCWarning(lcUi, "Failed to list playlists: %s", qPrintable(listRes.error().toString()));
        return;
    }
    m_model.refresh(listRes.value());
}

qint64 PlaylistController::createManual(const QString &name, const QList<qint64> &trackIds)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        qCWarning(lcUi, "Failed to create manual playlist: empty name");
        return 0;
    }

    const auto res = m_store.createManual(trimmed, trackIds);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to create playlist '%s': %s", qPrintable(name),
            qPrintable(res.error().toString()));
        return 0;
    }

    refresh();
    emit playlistsChanged();
    return res.value();
}

qint64 PlaylistController::createSmart(const QString &name, const library::SmartRule &rule)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        qCWarning(lcUi, "Failed to create smart playlist: empty name");
        return 0;
    }

    const auto res = m_store.createSmart(trimmed, rule);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to create smart playlist '%s': %s", qPrintable(name),
            qPrintable(res.error().toString()));
        return 0;
    }

    refresh();
    emit playlistsChanged();
    return res.value();
}

qint64 PlaylistController::saveQueue(const QString &name)
{
    auto *queue = m_player.queue();
    QList<qint64> trackIds;
    const int count = queue->count();
    trackIds.reserve(count);
    for (int i = 0; i < count; ++i) {
        const auto &item = queue->at(i);
        if (item.trackId >= 0) {
            trackIds.append(item.trackId);
        }
    }

    return createManual(name, trackIds);
}

bool PlaylistController::rename(qint64 id, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        qCWarning(lcUi, "Failed to rename playlist %lld: empty name", static_cast<long long>(id));
        return false;
    }

    const auto res = m_store.rename(id, trimmed);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to rename playlist %lld: %s", static_cast<long long>(id),
            qPrintable(res.error().toString()));
        return false;
    }

    refresh();
    emit playlistsChanged();
    return true;
}

bool PlaylistController::remove(qint64 id)
{
    const auto res = m_store.remove(id);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to remove playlist %lld: %s", static_cast<long long>(id),
            qPrintable(res.error().toString()));
        return false;
    }

    refresh();
    emit playlistsChanged();
    return true;
}

int PlaylistController::addTracks(qint64 id, const QList<qint64> &trackIds)
{
    const auto res = m_store.addTracks(id, trackIds);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to add tracks to playlist %lld: %s", static_cast<long long>(id),
            qPrintable(res.error().toString()));
        return -1;
    }

    emit playlistContentChanged(id);
    return res.value();
}

bool PlaylistController::removeTracks(qint64 id, const QList<qint64> &trackIds)
{
    const auto res = m_store.removeTracks(id, trackIds);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to remove tracks from playlist %lld: %s",
            static_cast<long long>(id), qPrintable(res.error().toString()));
        return false;
    }

    emit playlistContentChanged(id);
    return true;
}

bool PlaylistController::moveTracks(qint64 id, const QList<qint64> &trackIds, int beforePosition)
{
    const auto res = m_store.moveTracks(id, trackIds, beforePosition);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to move tracks in playlist %lld: %s", static_cast<long long>(id),
            qPrintable(res.error().toString()));
        return false;
    }

    emit playlistContentChanged(id);
    return true;
}

bool PlaylistController::setRule(qint64 id, const library::SmartRule &rule)
{
    const auto res = m_store.setRule(id, rule);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to set rule for playlist %lld: %s", static_cast<long long>(id),
            qPrintable(res.error().toString()));
        return false;
    }

    refresh();
    emit playlistsChanged();
    emit playlistContentChanged(id);
    return true;
}

library::SmartRule PlaylistController::rule(qint64 id) const
{
    const auto p = info(id);
    if (p.has_value() && p->rule.has_value()) {
        return p->rule.value();
    }
    return { };
}

QVariantList PlaylistController::smartFields() const
{
    QVariantList list;
    const auto metaEnum = QMetaEnum::fromType<library::SmartField>();
    for (int i = 0; i < metaEnum.keyCount(); ++i) {
        list.append(metaEnum.value(i));
    }
    return list;
}

QVariantList PlaylistController::smartOps(library::SmartField field) const
{
    QVariantList list;
    for (auto op : library::smartOpsFor(field)) {
        list.append(QVariant::fromValue(op));
    }
    return list;
}

library::SmartFieldKind PlaylistController::smartFieldKind(library::SmartField field) const
{
    return library::smartFieldKind(field);
}

QVariantList PlaylistController::smartSortKeys() const
{
    QVariantList list;
    const auto metaEnum = QMetaEnum::fromType<library::TrackSortKey>();
    for (int i = 0; i < metaEnum.keyCount(); ++i) {
        const auto val = static_cast<library::TrackSortKey>(metaEnum.value(i));
        if (val != library::TrackSortKey::PlaylistOrder) {
            list.append(metaEnum.value(i));
        }
    }
    return list;
}

QVariantList PlaylistController::smartEnumValues(library::SmartField field) const
{
    QVariantList list;
    if (field == library::SmartField::VersionType) {
        const auto metaEnum = QMetaEnum::fromType<library::VersionType>();
        list.reserve(metaEnum.keyCount());
        for (int i = 0; i < metaEnum.keyCount(); ++i) {
            const auto vt = static_cast<library::VersionType>(metaEnum.value(i));
            QVariantMap item;
            item.insert(QStringLiteral("text"), versionLabel(vt));
            item.insert(QStringLiteral("value"), library::versionTypeToString(vt));
            list.append(item);
        }
    } else if (field == library::SmartField::Language) {
        const auto metaEnum = QMetaEnum::fromType<library::TrackLanguage>();
        list.reserve(metaEnum.keyCount());
        for (int i = 0; i < metaEnum.keyCount(); ++i) {
            const auto lang = static_cast<library::TrackLanguage>(metaEnum.value(i));
            QVariantMap item;
            item.insert(QStringLiteral("text"), languageLabel(lang));
            item.insert(QStringLiteral("value"), library::trackLanguageToString(lang));
            list.append(item);
        }
    }
    return list;
}

QString PlaylistController::fieldLabel(library::SmartField field) const
{
    return smartFieldLabel(field);
}

QString PlaylistController::opLabel(library::SmartOp op) const
{
    return smartOpLabel(op);
}

QString PlaylistController::sortKeyLabel(library::TrackSortKey key) const
{
    return trackSortKeyLabel(key);
}

QString PlaylistController::versionLabel(library::VersionType type) const
{
    return versionTypeLabel(type);
}

QString PlaylistController::languageLabel(library::TrackLanguage lang) const
{
    return trackLanguageLabel(lang);
}

std::optional<library::PlaylistInfo> PlaylistController::info(qint64 id) const
{
    if (id <= 0) {
        return std::nullopt;
    }

    const auto res = m_store.playlist(id);
    if (!res.ok() || !res.value().has_value()) {
        return std::nullopt;
    }
    return res.value();
}

bool PlaylistController::isManual(qint64 id) const
{
    const auto p = info(id);
    return p.has_value() && p->kind == library::PlaylistKind::Manual;
}

} // namespace linernotes::ui
