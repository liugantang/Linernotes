// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <butler/DuplicateFinder.h>
#include <core/Result.h>

#include <cstdint>
#include <optional>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {
class FileTrash;
}

namespace linernotes::ui {

struct DuplicateAlbumEntry {
    qint64 albumId = 0;
    QString title;
    QString folder;
    QString format;
    double totalKeepScore = 0.0;
    bool operator==(const DuplicateAlbumEntry &) const = default;
};

struct DuplicateMemberEntry {
    qint64 trackId = 0;
    QString title;
    QString artist;
    QString album;
    qint64 albumId = 0;
    QString format;
    QString durationText;
    QString path;
    bool recommended = false;
    double keepScore = 0.0;
    bool operator==(const DuplicateMemberEntry &) const = default;
};

struct DuplicateGroupEntry {
    qint64 groupId = 0;
    butler::DuplicateKind kind = butler::DuplicateKind::Exact;
    QList<DuplicateMemberEntry> members;
    bool operator==(const DuplicateGroupEntry &) const = default;
};

struct DuplicateSectionEntry {
    int sectionIndex = 0;
    QString title;
    QList<qint64> albumIds;
    QList<DuplicateAlbumEntry> albums;
    qint64 recommendedAlbumId = 0;
    QList<DuplicateGroupEntry> groups;
    bool operator==(const DuplicateSectionEntry &) const = default;
};

class DuplicateSectionModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DuplicateSectionModel)

public:
    enum Roles : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt 模型角色需与 int
                                 // 互转
        SectionIndexRole = Qt::UserRole + 1,
        TitleRole,
        GroupCountRole,
        AlbumsRole,
        RecommendedAlbumIdRole,
        GroupsRole,
    };

    explicit DuplicateSectionModel(QObject *parent = nullptr);
    ~DuplicateSectionModel() override = default;

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setSections(QList<DuplicateSectionEntry> sections);
    [[nodiscard]] const QList<DuplicateSectionEntry> &sections() const;
    [[nodiscard]] int count() const;

private:
    QList<DuplicateSectionEntry> m_sections;
};

class DuplicateController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DuplicateController)

public:
    enum class DuplicateKind : std::uint8_t {
        Exact = static_cast<std::uint8_t>(butler::DuplicateKind::Exact),
        SameRecording = static_cast<std::uint8_t>(butler::DuplicateKind::SameRecording),
        Suspect = static_cast<std::uint8_t>(butler::DuplicateKind::Suspect),
    };
    Q_ENUM(DuplicateKind)

    Q_PROPERTY(linernotes::ui::DuplicateSectionModel *sectionModel READ sectionModel CONSTANT)
    Q_PROPERTY(linernotes::ui::DuplicateSectionModel *model READ sectionModel CONSTANT)
    Q_PROPERTY(int sectionCount READ sectionCount NOTIFY sectionsChanged)
    Q_PROPERTY(int groupCount READ groupCount NOTIFY sectionsChanged)
    Q_PROPERTY(int recommendedExtraFiles READ recommendedExtraFiles NOTIFY sectionsChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    DuplicateController(library::Database &db, const core::Clock &clock, butler::FileTrash &trash,
        QObject *parent = nullptr);
    ~DuplicateController() override;

    [[nodiscard]] DuplicateSectionModel *sectionModel();
    [[nodiscard]] const DuplicateSectionModel *sectionModel() const;
    [[nodiscard]] int sectionCount() const;
    [[nodiscard]] int groupCount() const;
    [[nodiscard]] int recommendedExtraFiles() const;
    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] QString lastError() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void keepAlbum(int sectionIndex, qint64 albumId);
    Q_INVOKABLE void keepTrack(qint64 groupId, qint64 trackId);
    Q_INVOKABLE void dismissSection(int sectionIndex);
    Q_INVOKABLE void dismissGroup(qint64 groupId);
    Q_INVOKABLE void keepAllRecommended();

signals:
    void sectionsChanged();
    void busyChanged();
    void lastErrorChanged();
    void libraryModified();

private:
    struct ActionResult {
        std::optional<core::Error> error;
        QStringList failedPaths;
    };

    void onRefreshFinished();
    void onActionFinished();

    library::Database &m_db;
    const core::Clock &m_clock;
    butler::FileTrash &m_trash;

    DuplicateSectionModel m_sectionModel;
    int m_sectionCount = 0;
    int m_groupCount = 0;
    int m_recommendedExtraFiles = 0;
    bool m_busy = false;
    QString m_lastError;

    QFutureWatcher<QList<DuplicateSectionEntry>> m_refreshWatcher;
    QFutureWatcher<ActionResult> m_actionWatcher;
};

} // namespace linernotes::ui
