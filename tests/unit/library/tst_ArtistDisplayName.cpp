// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/ArtistNamePreference.h>
#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/EnumNames.h>
#include <library/LibraryEnums.h>
#include <library/LibraryQuery.h>
#include <library/Migrator.h>

#include <algorithm>
#include <memory>
#include <optional>

namespace {

using linernotes::library::ArtistFilter;
using linernotes::library::ArtistNamePreference;
using linernotes::library::ArtistRow;
using linernotes::library::ArtistSortKey;
using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::LibraryQuery;
using linernotes::library::Migrator;

struct TestDbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                                 "first_seen_at, scanned_at) "
                                 "VALUES (?, ?, 1024, 1000, 200000, 1000, 1000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, NULL, NULL, 0, 1000);"));
        q.addBindValue(fileId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertRawTag(
        const QSqlDatabase &db, qint64 trackId, const QString &key, const QString &value)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
            "VALUES (?, 'id3v2', 0, ?, 0, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(key);
        q.addBindValue(value);
        return q.exec();
    }

    static bool updateTagsReadAt(const QSqlDatabase &db, qint64 trackId, qint64 timestamp = 1000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE tracks SET tags_read_at = ? WHERE id = ?;"));
        q.addBindValue(timestamp);
        q.addBindValue(trackId);
        return q.exec();
    }

    static qint64 insertTrackWithArtist(const QSqlDatabase &db, qint64 rootId, const QString &path,
        const QString &title, const QString &artistName)
    {
        const qint64 fileId = insertFile(db, rootId, path);
        if (fileId <= 0) {
            return -1;
        }
        const qint64 trackId = insertTrack(db, fileId);
        if (trackId <= 0) {
            return -1;
        }
        if (!insertRawTag(db, trackId, QStringLiteral("TITLE"), title)) {
            return -1;
        }
        if (!insertRawTag(db, trackId, QStringLiteral("ARTIST"), artistName)) {
            return -1;
        }
        if (!updateTagsReadAt(db, trackId)) {
            return -1;
        }
        EntityLinker linker(db);
        if (!linker.linkTrack(trackId).ok()) {
            return -1;
        }
        return trackId;
    }

    static qint64 getArtistId(const QSqlDatabase &db, const QString &name)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT id FROM artists WHERE name = ?;"));
        q.addBindValue(name);
        if (q.exec() && q.next()) {
            return q.value(0).toLongLong();
        }
        return -1;
    }

    static bool insertArtistAlias(const QSqlDatabase &db, qint64 artistId, const QString &alias,
        const QString &locale, const QString &kind = QStringLiteral("translation"),
        const QString &source = QStringLiteral("rule"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO artist_aliases (artist_id, alias, locale, kind, source, created_at) "
            "VALUES (?, ?, ?, ?, ?, 1000);"));
        q.addBindValue(artistId);
        q.addBindValue(alias);
        q.addBindValue(locale);
        q.addBindValue(kind);
        q.addBindValue(source);
        return q.exec();
    }
};

class TstArtistDisplayName : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void enumNamesMapping();
    void artistDisplayNameAndOrder();
    void singleArtistQuery();

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;
    std::unique_ptr<Database> m_db;
    QSqlDatabase m_conn;
    qint64 m_zhouId = -1;
    qint64 m_aimerId = -1;
};

void TstArtistDisplayName::init()
{
    m_tempDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tempDir->isValid());

    m_db = std::make_unique<Database>(m_tempDir->filePath(QStringLiteral("artist_pref_test.db")));
    QVERIFY(m_db->open(Migrator()).ok());
    const auto connRes = m_db->connection();
    QVERIFY(connRes.ok());
    m_conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(m_conn);
    QVERIFY(rootId > 0);

    const qint64 t1 = TestDbHelper::insertTrackWithArtist(m_conn, rootId,
        QStringLiteral("/music/1.mp3"), QStringLiteral("Song 1"), QStringLiteral("周杰倫"));
    QVERIFY(t1 > 0);

    const qint64 t2 = TestDbHelper::insertTrackWithArtist(m_conn, rootId,
        QStringLiteral("/music/2.mp3"), QStringLiteral("Song 2"), QStringLiteral("Aimer"));
    QVERIFY(t2 > 0);

    m_zhouId = TestDbHelper::getArtistId(m_conn, QStringLiteral("周杰倫"));
    QVERIFY(m_zhouId > 0);

    m_aimerId = TestDbHelper::getArtistId(m_conn, QStringLiteral("Aimer"));
    QVERIFY(m_aimerId > 0);

    QVERIFY(TestDbHelper::insertArtistAlias(
        m_conn, m_zhouId, QStringLiteral("周杰伦"), QStringLiteral("zh_Hans")));
    QVERIFY(TestDbHelper::insertArtistAlias(
        m_conn, m_zhouId, QStringLiteral("Jay Chou"), QStringLiteral("en")));
}

void TstArtistDisplayName::cleanup()
{
    m_conn = QSqlDatabase();
    m_db.reset();
    m_tempDir.reset();
    m_zhouId = -1;
    m_aimerId = -1;
}

void TstArtistDisplayName::enumNamesMapping()
{
    QCOMPARE(linernotes::library::artistNamePreferenceToString(ArtistNamePreference::Original),
        QStringLiteral("original"));
    QCOMPARE(
        linernotes::library::artistNamePreferenceToString(ArtistNamePreference::SimplifiedChinese),
        QStringLiteral("simplified_chinese"));
    QCOMPARE(linernotes::library::artistNamePreferenceToString(ArtistNamePreference::English),
        QStringLiteral("english"));

    QCOMPARE(linernotes::library::artistNamePreferenceFromString(QStringLiteral("original")),
        std::make_optional(ArtistNamePreference::Original));
    QCOMPARE(
        linernotes::library::artistNamePreferenceFromString(QStringLiteral("simplified_chinese")),
        std::make_optional(ArtistNamePreference::SimplifiedChinese));
    QCOMPARE(linernotes::library::artistNamePreferenceFromString(QStringLiteral("english")),
        std::make_optional(ArtistNamePreference::English));
    QCOMPARE(linernotes::library::artistNamePreferenceFromString(QStringLiteral("unknown")),
        std::nullopt);
}

void TstArtistDisplayName::artistDisplayNameAndOrder()
{
    const LibraryQuery query(m_conn);

    // 1. Original preference
    {
        ArtistFilter filter;
        filter.namePreference = ArtistNamePreference::Original;
        const auto res = query.artists(filter, ArtistSortKey::Name, Qt::AscendingOrder, 0, 10);
        QVERIFY(res.ok());
        const auto &rows = res.value();
        QCOMPARE(rows.size(), 2);

        auto zhouIt = std::ranges::find_if(
            rows, [this](const ArtistRow &r) { return r.artistId == m_zhouId; });
        auto aimerIt = std::ranges::find_if(
            rows, [this](const ArtistRow &r) { return r.artistId == m_aimerId; });
        QVERIFY(zhouIt != rows.end());
        QVERIFY(aimerIt != rows.end());

        QCOMPARE(zhouIt->name, QStringLiteral("周杰倫"));
        QCOMPARE(zhouIt->originalName, QStringLiteral("周杰倫"));
        QCOMPARE(aimerIt->name, QStringLiteral("Aimer"));
        QCOMPARE(aimerIt->originalName, QStringLiteral("Aimer"));
    }

    // 2. Simplified Chinese preference
    {
        ArtistFilter filter;
        filter.namePreference = ArtistNamePreference::SimplifiedChinese;
        const auto res = query.artists(filter, ArtistSortKey::Name, Qt::AscendingOrder, 0, 10);
        QVERIFY(res.ok());
        const auto &rows = res.value();
        QCOMPARE(rows.size(), 2);

        auto zhouIt = std::ranges::find_if(
            rows, [this](const ArtistRow &r) { return r.artistId == m_zhouId; });
        auto aimerIt = std::ranges::find_if(
            rows, [this](const ArtistRow &r) { return r.artistId == m_aimerId; });
        QVERIFY(zhouIt != rows.end());
        QVERIFY(aimerIt != rows.end());

        QCOMPARE(zhouIt->name, QStringLiteral("周杰伦"));
        QCOMPARE(zhouIt->originalName, QStringLiteral("周杰倫"));
        QCOMPARE(aimerIt->name, QStringLiteral("Aimer"));
        QCOMPARE(aimerIt->originalName, QStringLiteral("Aimer"));
    }

    // 3. English preference
    {
        ArtistFilter filter;
        filter.namePreference = ArtistNamePreference::English;
        const auto res = query.artists(filter, ArtistSortKey::Name, Qt::AscendingOrder, 0, 10);
        QVERIFY(res.ok());
        const auto &rows = res.value();
        QCOMPARE(rows.size(), 2);

        // Under English preference sorted ASC, "Aimer" comes before "Jay Chou"
        QCOMPARE(rows.at(0).artistId, m_aimerId);
        QCOMPARE(rows.at(0).name, QStringLiteral("Aimer"));
        QCOMPARE(rows.at(0).originalName, QStringLiteral("Aimer"));

        QCOMPARE(rows.at(1).artistId, m_zhouId);
        QCOMPARE(rows.at(1).name, QStringLiteral("Jay Chou"));
        QCOMPARE(rows.at(1).originalName, QStringLiteral("周杰倫"));
    }
}

void TstArtistDisplayName::singleArtistQuery()
{
    const LibraryQuery query(m_conn);

    // Query 周杰倫 with 3 preferences
    {
        const auto resOrig = query.artist(m_zhouId, ArtistNamePreference::Original);
        QVERIFY(resOrig.ok());
        const auto resOrigRow = resOrig.value().value_or(ArtistRow { });
        QVERIFY(resOrig.value().has_value());
        QCOMPARE(resOrigRow.name, QStringLiteral("周杰倫"));
        QCOMPARE(resOrigRow.originalName, QStringLiteral("周杰倫"));

        const auto resZh = query.artist(m_zhouId, ArtistNamePreference::SimplifiedChinese);
        QVERIFY(resZh.ok());
        const auto resZhRow = resZh.value().value_or(ArtistRow { });
        QVERIFY(resZh.value().has_value());
        QCOMPARE(resZhRow.name, QStringLiteral("周杰伦"));
        QCOMPARE(resZhRow.originalName, QStringLiteral("周杰倫"));

        const auto resEn = query.artist(m_zhouId, ArtistNamePreference::English);
        QVERIFY(resEn.ok());
        const auto resEnRow = resEn.value().value_or(ArtistRow { });
        QVERIFY(resEn.value().has_value());
        QCOMPARE(resEnRow.name, QStringLiteral("Jay Chou"));
        QCOMPARE(resEnRow.originalName, QStringLiteral("周杰倫"));
    }

    // Query Aimer with 3 preferences
    {
        const auto resOrig = query.artist(m_aimerId, ArtistNamePreference::Original);
        QVERIFY(resOrig.ok());
        const auto resOrigRow = resOrig.value().value_or(ArtistRow { });
        QVERIFY(resOrig.value().has_value());
        QCOMPARE(resOrigRow.name, QStringLiteral("Aimer"));
        QCOMPARE(resOrigRow.originalName, QStringLiteral("Aimer"));

        const auto resZh = query.artist(m_aimerId, ArtistNamePreference::SimplifiedChinese);
        QVERIFY(resZh.ok());
        const auto resZhRow = resZh.value().value_or(ArtistRow { });
        QVERIFY(resZh.value().has_value());
        QCOMPARE(resZhRow.name, QStringLiteral("Aimer"));
        QCOMPARE(resZhRow.originalName, QStringLiteral("Aimer"));

        const auto resEn = query.artist(m_aimerId, ArtistNamePreference::English);
        QVERIFY(resEn.ok());
        const auto resEnRow = resEn.value().value_or(ArtistRow { });
        QVERIFY(resEn.value().has_value());
        QCOMPARE(resEnRow.name, QStringLiteral("Aimer"));
        QCOMPARE(resEnRow.originalName, QStringLiteral("Aimer"));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistDisplayName)
#include "tst_ArtistDisplayName.moc"
