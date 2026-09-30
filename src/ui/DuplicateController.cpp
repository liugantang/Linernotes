// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "DuplicateController.h"

#include "Format.h"
#include "UiLogging.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QtConcurrent/QtConcurrent>

#include <butler/DuplicateResolver.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/Errors.h>

#include <algorithm>
#include <utility>

namespace linernotes::ui {

namespace {

struct RawRow {
    qint64 groupId = 0;
    butler::DuplicateKind kind = butler::DuplicateKind::Exact;
    qint64 trackId = 0;
    double keepScore = 0.0;
    bool recommended = false;
    QString title;
    QString artist;
    QString album;
    qint64 albumId = 0;
    QString format;
    QString durationText;
    QString path;
};

QString formatAudioSummary(const QString &codec, int sampleRate, int bitDepth, int bitrate)
{
    QString baseCodec = codec.trimmed().toUpper();
    if (baseCodec.isEmpty()) {
        baseCodec = QStringLiteral("AUDIO");
    }

    QString rateStr;
    if (sampleRate > 0) {
        if (sampleRate % 1000 == 0) {
            rateStr = QString::number(sampleRate / 1000);
        } else {
            rateStr = QString::number(sampleRate / 1000.0, 'f', 1);
        }
    }

    if (bitDepth > 0 && !rateStr.isEmpty()) {
        return QStringLiteral("%1 %2/%3").arg(baseCodec).arg(bitDepth).arg(rateStr);
    }
    if (bitrate > 0) {
        return QStringLiteral("%1 %2 kbps").arg(baseCodec).arg(bitrate);
    }
    if (!rateStr.isEmpty()) {
        return QStringLiteral("%1 %2 kHz").arg(baseCodec).arg(rateStr);
    }
    return baseCodec;
}

QList<RawRow> fetchRawRows(const QSqlDatabase &conn)
{
    QList<RawRow> rows;
    QSqlQuery q(conn);
    const QString sql = QStringLiteral("SELECT "
                                       "  g.id, "
                                       "  g.kind, "
                                       "  m.track_id, "
                                       "  m.keep_score, "
                                       "  m.recommended, "
                                       "  COALESCE(em.title, ''), "
                                       "  COALESCE(em.artist, ''), "
                                       "  COALESCE(al.title, COALESCE(em.album, '')), "
                                       "  COALESCE(t.album_id, 0), "
                                       "  COALESCE(f.codec, ''), "
                                       "  COALESCE(f.sample_rate, 0), "
                                       "  COALESCE(f.bit_depth, 0), "
                                       "  COALESCE(f.bitrate, 0), "
                                       "  COALESCE(f.duration_ms, 0), "
                                       "  COALESCE(f.path, '') "
                                       "FROM duplicate_groups g "
                                       "JOIN duplicate_members m ON m.group_id = g.id "
                                       "JOIN tracks t ON t.id = m.track_id "
                                       "JOIN files f ON f.id = t.file_id "
                                       "LEFT JOIN albums al ON al.id = t.album_id "
                                       "LEFT JOIN effective_metadata em ON em.track_id = t.id "
                                       "ORDER BY g.id ASC, m.keep_score DESC, m.track_id ASC;");

    if (!q.exec(sql)) {
        qCWarning(lcUi, "Failed to query duplicate rows: %s", qPrintable(q.lastError().text()));
        return rows;
    }

    while (q.next()) {
        RawRow row;
        row.groupId = q.value(0).toLongLong();
        const QString kindStr = q.value(1).toString();
        row.kind = butler::duplicateKindFromString(kindStr).value_or(butler::DuplicateKind::Exact);
        row.trackId = q.value(2).toLongLong();
        row.keepScore = q.value(3).toDouble();
        row.recommended = (q.value(4).toInt() == 1);
        row.title = q.value(5).toString();
        row.artist = q.value(6).toString();
        row.album = q.value(7).toString();
        row.albumId = q.value(8).toLongLong();
        const QString codec = q.value(9).toString();
        const int sampleRate = q.value(10).toInt();
        const int bitDepth = q.value(11).toInt();
        const int bitrate = q.value(12).toInt();
        const qint64 durationMs = q.value(13).toLongLong();
        row.path = q.value(14).toString();

        if (row.title.isEmpty()) {
            row.title = QFileInfo(row.path).fileName();
        }
        if (row.artist.isEmpty()) {
            row.artist = QStringLiteral("Unknown Artist");
        }
        if (row.album.isEmpty()) {
            row.album = QStringLiteral("Unknown Album");
        }

        row.format = formatAudioSummary(codec, sampleRate, bitDepth, bitrate);
        row.durationText = formatDuration(durationMs);
        rows.append(row);
    }
    return rows;
}

QList<DuplicateGroupEntry> assembleGroups(const QList<RawRow> &rows)
{
    QList<DuplicateGroupEntry> groups;
    QHash<qint64, qsizetype> groupIndexMap;

    for (const auto &row : rows) {
        auto it = groupIndexMap.find(row.groupId);
        if (it == groupIndexMap.end()) {
            DuplicateGroupEntry grp;
            grp.groupId = row.groupId;
            grp.kind = row.kind;
            groupIndexMap.insert(row.groupId, groups.size());
            groups.append(grp);
            it = groupIndexMap.find(row.groupId);
        }
        const DuplicateMemberEntry mem {
            .trackId = row.trackId,
            .title = row.title,
            .artist = row.artist,
            .album = row.album,
            .albumId = row.albumId,
            .format = row.format,
            .durationText = row.durationText,
            .path = row.path,
            .recommended = row.recommended,
            .keepScore = row.keepScore,
        };
        std::next(groups.begin(), it.value())->members.append(mem);
    }
    return groups;
}

struct SectionBucket {
    QList<qint64> albumIds;
    QList<DuplicateGroupEntry> groups;
};

QList<SectionBucket> partitionIntoBuckets(const QList<DuplicateGroupEntry> &groups)
{
    QList<SectionBucket> buckets;
    QHash<QString, qsizetype> bucketIndexMap;

    for (const auto &grp : groups) {
        QSet<qint64> idSet;
        for (const auto &mem : grp.members) {
            idSet.insert(mem.albumId);
        }
        QList<qint64> albumIds = idSet.values();
        std::ranges::sort(albumIds);

        QString key;
        for (const qint64 id : albumIds) {
            key += QString::number(id) + QLatin1Char(',');
        }

        auto it = bucketIndexMap.find(key);
        if (it == bucketIndexMap.end()) {
            SectionBucket bucket;
            bucket.albumIds = albumIds;
            bucketIndexMap.insert(key, buckets.size());
            buckets.append(bucket);
            it = bucketIndexMap.find(key);
        }
        std::next(buckets.begin(), it.value())->groups.append(grp);
    }
    return buckets;
}

DuplicateSectionEntry buildSectionEntry(int sectionIndex, const SectionBucket &bucket)
{
    DuplicateSectionEntry section;
    section.sectionIndex = sectionIndex;
    section.albumIds = bucket.albumIds;
    section.groups = bucket.groups;

    QHash<qint64, double> albumScores;
    QHash<qint64, QString> albumTitles;
    QHash<qint64, QString> albumFolders;
    QHash<qint64, QString> albumFormats;

    for (const auto &grp : section.groups) {
        for (const auto &mem : grp.members) {
            albumScores.insert(mem.albumId, albumScores.value(mem.albumId) + mem.keepScore);
            if (!albumTitles.contains(mem.albumId) || albumTitles.value(mem.albumId).isEmpty()) {
                albumTitles.insert(mem.albumId, mem.album);
            }
            if (!albumFolders.contains(mem.albumId)) {
                albumFolders.insert(mem.albumId, QFileInfo(mem.path).path());
            }
            if (!albumFormats.contains(mem.albumId)) {
                albumFormats.insert(mem.albumId, mem.format);
            }
        }
    }

    for (const qint64 albId : bucket.albumIds) {
        const DuplicateAlbumEntry alb {
            .albumId = albId,
            .title = albumTitles.value(albId, QStringLiteral("Unknown Album")),
            .folder = albumFolders.value(albId, QString()),
            .format = albumFormats.value(albId, QString()),
            .totalKeepScore = albumScores.value(albId, 0.0),
        };
        section.albums.append(alb);
    }

    if (section.albums.size() == 1) {
        section.recommendedAlbumId = section.albums.first().albumId;
        section.title = section.albums.first().title;
    } else if (!section.albums.isEmpty()) {
        qint64 bestId = section.albums.first().albumId;
        double bestScore = section.albums.first().totalKeepScore;
        QStringList titles;
        for (const auto &alb : section.albums) {
            titles.append(alb.title);
            if (alb.totalKeepScore > bestScore
                || (alb.totalKeepScore == bestScore && alb.albumId < bestId)) {
                bestScore = alb.totalKeepScore;
                bestId = alb.albumId;
            }
        }
        section.recommendedAlbumId = bestId;
        section.title = titles.join(QStringLiteral(" ↔ "));
    }

    return section;
}

QList<DuplicateSectionEntry> loadSections(library::Database &db)
{
    auto connRes = db.connection();
    if (!connRes.ok()) {
        return { };
    }
    const auto &conn = connRes.value();

    const QList<RawRow> rows = fetchRawRows(conn);
    const QList<DuplicateGroupEntry> groups = assembleGroups(rows);
    const QList<SectionBucket> buckets = partitionIntoBuckets(groups);

    QList<DuplicateSectionEntry> sections;
    sections.reserve(buckets.size());
    int sectionIndex = 0;
    for (const auto &bucket : buckets) {
        sections.append(buildSectionEntry(sectionIndex++, bucket));
    }
    return sections;
}

int computeRecommendedExtraFiles(const QList<DuplicateSectionEntry> &sections)
{
    int count = 0;
    for (const auto &section : sections) {
        for (const auto &grp : section.groups) {
            if (grp.kind != butler::DuplicateKind::Suspect && grp.members.size() > 1) {
                count += static_cast<int>(grp.members.size() - 1);
            }
        }
    }
    return count;
}

qint64 findKeepTrackForGroup(const DuplicateGroupEntry &grp, qint64 targetAlbumId)
{
    qint64 keepTrackId = 0;
    double highestScore = -1.0;
    bool foundInAlbum = false;

    for (const auto &mem : grp.members) {
        if (mem.albumId == targetAlbumId) {
            if (!foundInAlbum || mem.keepScore > highestScore
                || (mem.keepScore == highestScore && mem.trackId < keepTrackId)) {
                foundInAlbum = true;
                keepTrackId = mem.trackId;
                highestScore = mem.keepScore;
            }
        }
    }

    if (!foundInAlbum) {
        for (const auto &mem : grp.members) {
            if (mem.recommended) {
                keepTrackId = mem.trackId;
                break;
            }
        }
        if (keepTrackId == 0 && !grp.members.isEmpty()) {
            keepTrackId = grp.members.first().trackId;
        }
    }

    return keepTrackId;
}

} // namespace

DuplicateSectionModel::DuplicateSectionModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int DuplicateSectionModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_sections.size());
}

QVariant DuplicateSectionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_sections.size()) {
        return { };
    }
    const auto &sec = m_sections.at(index.row());
    switch (role) {
    case SectionIndexRole:
        return sec.sectionIndex;
    case TitleRole:
        return sec.title;
    case GroupCountRole:
        return static_cast<int>(sec.groups.size());
    case AlbumsRole: {
        QVariantList list;
        for (const auto &alb : sec.albums) {
            QVariantMap map;
            map.insert(QStringLiteral("albumId"), alb.albumId);
            map.insert(QStringLiteral("title"), alb.title);
            map.insert(QStringLiteral("albumName"), alb.title);
            map.insert(QStringLiteral("folder"), alb.folder);
            map.insert(QStringLiteral("format"), alb.format);
            map.insert(QStringLiteral("isRecommended"), alb.albumId == sec.recommendedAlbumId);
            map.insert(QStringLiteral("keepScore"), alb.totalKeepScore);
            list.append(map);
        }
        return list;
    }
    case RecommendedAlbumIdRole:
        return sec.recommendedAlbumId;
    case GroupsRole: {
        QVariantList list;
        for (const auto &grp : sec.groups) {
            QVariantMap map;
            map.insert(QStringLiteral("groupId"), grp.groupId);
            map.insert(QStringLiteral("kind"), static_cast<int>(grp.kind));
            QVariantList membersList;
            for (const auto &mem : grp.members) {
                QVariantMap memMap;
                memMap.insert(QStringLiteral("trackId"), mem.trackId);
                memMap.insert(QStringLiteral("title"), mem.title);
                memMap.insert(QStringLiteral("artist"), mem.artist);
                memMap.insert(QStringLiteral("album"), mem.album);
                memMap.insert(QStringLiteral("albumId"), mem.albumId);
                memMap.insert(QStringLiteral("format"), mem.format);
                memMap.insert(QStringLiteral("durationText"), mem.durationText);
                memMap.insert(QStringLiteral("path"), mem.path);
                memMap.insert(QStringLiteral("recommended"), mem.recommended);
                membersList.append(memMap);
            }
            map.insert(QStringLiteral("members"), membersList);
            list.append(map);
        }
        return list;
    }
    default:
        return { };
    }
}

QHash<int, QByteArray> DuplicateSectionModel::roleNames() const
{
    return {
        { SectionIndexRole, "sectionIndex" },
        { TitleRole, "title" },
        { GroupCountRole, "groupCount" },
        { AlbumsRole, "albums" },
        { RecommendedAlbumIdRole, "recommendedAlbumId" },
        { GroupsRole, "groups" },
    };
}

void DuplicateSectionModel::setSections(QList<DuplicateSectionEntry> sections)
{
    beginResetModel();
    m_sections = std::move(sections);
    endResetModel();
}

const QList<DuplicateSectionEntry> &DuplicateSectionModel::sections() const
{
    return m_sections;
}

int DuplicateSectionModel::count() const
{
    return static_cast<int>(m_sections.size());
}

DuplicateController::DuplicateController(
    library::Database &db, const core::Clock &clock, butler::FileTrash &trash, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_clock(clock)
    , m_trash(trash)
{
    connect(&m_refreshWatcher, &QFutureWatcher<QList<DuplicateSectionEntry>>::finished, this,
        &DuplicateController::onRefreshFinished);
    connect(&m_actionWatcher, &QFutureWatcher<ActionResult>::finished, this,
        &DuplicateController::onActionFinished);
}

DuplicateController::~DuplicateController()
{
    m_refreshWatcher.cancel();
    m_refreshWatcher.waitForFinished();
    m_actionWatcher.cancel();
    m_actionWatcher.waitForFinished();
}

DuplicateSectionModel *DuplicateController::sectionModel()
{
    return &m_sectionModel;
}

const DuplicateSectionModel *DuplicateController::sectionModel() const
{
    return &m_sectionModel;
}

int DuplicateController::sectionCount() const
{
    return m_sectionCount;
}

int DuplicateController::groupCount() const
{
    return m_groupCount;
}

int DuplicateController::recommendedExtraFiles() const
{
    return m_recommendedExtraFiles;
}

bool DuplicateController::isBusy() const
{
    return m_busy;
}

QString DuplicateController::lastError() const
{
    return m_lastError;
}

void DuplicateController::refresh()
{
    if (m_refreshWatcher.isRunning()) {
        return;
    }
    auto future = QtConcurrent::run([&db = m_db]() { return loadSections(db); });
    m_refreshWatcher.setFuture(future);
}

void DuplicateController::onRefreshFinished()
{
    auto sections = m_refreshWatcher.result();
    m_sectionCount = static_cast<int>(sections.size());

    int totalGroups = 0;
    for (const auto &sec : sections) {
        totalGroups += static_cast<int>(sec.groups.size());
    }
    m_groupCount = totalGroups;
    m_recommendedExtraFiles = computeRecommendedExtraFiles(sections);

    m_sectionModel.setSections(std::move(sections));
    emit sectionsChanged();
}

void DuplicateController::keepAlbum(int sectionIndex, qint64 albumId)
{
    if (m_busy) {
        return;
    }
    const auto &sections = m_sectionModel.sections();
    if (sectionIndex < 0 || sectionIndex >= sections.size()) {
        return;
    }
    const auto &section = sections.at(sectionIndex);

    struct ResolveTask {
        qint64 groupId = 0;
        qint64 keepTrackId = 0;
    };
    QList<ResolveTask> tasks;
    tasks.reserve(section.groups.size());

    for (const auto &grp : section.groups) {
        const qint64 keepTrackId = findKeepTrackForGroup(grp, albumId);
        if (keepTrackId > 0) {
            tasks.append(ResolveTask { .groupId = grp.groupId, .keepTrackId = keepTrackId });
        }
    }

    if (tasks.isEmpty()) {
        return;
    }

    m_busy = true;
    emit busyChanged();

    auto future = QtConcurrent::run(
        [&db = m_db, &trash = m_trash, &clock = m_clock, tasks]() -> ActionResult {
            ActionResult result;
            const butler::DuplicateResolver resolver(db, trash, clock);
            for (const auto &task : tasks) {
                auto res = resolver.resolve(task.groupId, task.keepTrackId);
                if (!res.ok()) {
                    result.error = res.error();
                } else {
                    result.failedPaths.append(res.value().failedPaths);
                }
            }
            return result;
        });

    m_actionWatcher.setFuture(future);
}

void DuplicateController::keepTrack(qint64 groupId, qint64 trackId)
{
    if (m_busy) {
        return;
    }
    m_busy = true;
    emit busyChanged();

    auto future = QtConcurrent::run(
        [&db = m_db, &trash = m_trash, &clock = m_clock, groupId, trackId]() -> ActionResult {
            ActionResult result;
            const butler::DuplicateResolver resolver(db, trash, clock);
            auto res = resolver.resolve(groupId, trackId);
            if (!res.ok()) {
                result.error = res.error();
            } else {
                result.failedPaths = res.value().failedPaths;
            }
            return result;
        });

    m_actionWatcher.setFuture(future);
}

void DuplicateController::dismissSection(int sectionIndex)
{
    if (m_busy) {
        return;
    }
    const auto &sections = m_sectionModel.sections();
    if (sectionIndex < 0 || sectionIndex >= sections.size()) {
        return;
    }
    const auto &section = sections.at(sectionIndex);

    QList<qint64> groupIds;
    groupIds.reserve(section.groups.size());
    for (const auto &grp : section.groups) {
        groupIds.append(grp.groupId);
    }

    if (groupIds.isEmpty()) {
        return;
    }

    m_busy = true;
    emit busyChanged();

    auto future = QtConcurrent::run(
        [&db = m_db, &trash = m_trash, &clock = m_clock, groupIds]() -> ActionResult {
            ActionResult result;
            const butler::DuplicateResolver resolver(db, trash, clock);
            for (const qint64 gid : groupIds) {
                auto res = resolver.dismiss(gid);
                if (!res.ok()) {
                    result.error = res.error();
                }
            }
            return result;
        });

    m_actionWatcher.setFuture(future);
}

void DuplicateController::dismissGroup(qint64 groupId)
{
    if (m_busy) {
        return;
    }
    m_busy = true;
    emit busyChanged();

    auto future = QtConcurrent::run(
        [&db = m_db, &trash = m_trash, &clock = m_clock, groupId]() -> ActionResult {
            ActionResult result;
            const butler::DuplicateResolver resolver(db, trash, clock);
            auto res = resolver.dismiss(groupId);
            if (!res.ok()) {
                result.error = res.error();
            }
            return result;
        });

    m_actionWatcher.setFuture(future);
}

void DuplicateController::keepAllRecommended()
{
    if (m_busy) {
        return;
    }
    const auto &sections = m_sectionModel.sections();
    if (sections.isEmpty()) {
        return;
    }

    struct ResolveTask {
        qint64 groupId = 0;
        qint64 keepTrackId = 0;
    };
    QList<ResolveTask> tasks;

    for (const auto &section : sections) {
        const qint64 recAlbumId = section.recommendedAlbumId;
        for (const auto &grp : section.groups) {
            if (grp.kind == butler::DuplicateKind::Suspect) {
                continue;
            }
            const qint64 keepTrackId = findKeepTrackForGroup(grp, recAlbumId);
            if (keepTrackId > 0) {
                tasks.append(ResolveTask { .groupId = grp.groupId, .keepTrackId = keepTrackId });
            }
        }
    }

    if (tasks.isEmpty()) {
        return;
    }

    m_busy = true;
    emit busyChanged();

    auto future = QtConcurrent::run(
        [&db = m_db, &trash = m_trash, &clock = m_clock, tasks]() -> ActionResult {
            ActionResult result;
            const butler::DuplicateResolver resolver(db, trash, clock);
            for (const auto &task : tasks) {
                auto res = resolver.resolve(task.groupId, task.keepTrackId);
                if (!res.ok()) {
                    result.error = res.error();
                } else {
                    result.failedPaths.append(res.value().failedPaths);
                }
            }
            return result;
        });

    m_actionWatcher.setFuture(future);
}

void DuplicateController::onActionFinished()
{
    const auto result = m_actionWatcher.result();
    m_busy = false;
    emit busyChanged();

    if (result.error.has_value()) {
        m_lastError = result.error->message;
    } else if (!result.failedPaths.isEmpty()) {
        const int n = static_cast<int>(result.failedPaths.size());
        m_lastError = tr("%n file(s) could not be moved to the trash", "", n) + QStringLiteral(": ")
            + result.failedPaths.first();
    } else {
        m_lastError.clear();
    }
    emit lastErrorChanged();
    emit libraryModified();
    refresh();
}

} // namespace linernotes::ui
