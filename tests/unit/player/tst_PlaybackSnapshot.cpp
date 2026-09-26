// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <player/PlaybackSnapshot.h>

using linernotes::player::PlaybackSnapshot;
using linernotes::player::PlaybackStateStore;
using linernotes::player::PlayMode;

Q_DECLARE_METATYPE(linernotes::player::PlaybackSnapshot)
Q_DECLARE_METATYPE(linernotes::player::PlayMode)

namespace {

class TstPlaybackSnapshot : public QObject {
    Q_OBJECT

private slots:
    void toJsonAndFromJsonRoundTrip_data();
    void toJsonAndFromJsonRoundTrip();
    void missingFieldsUseDefaults();
    void invalidVersionOrTypesReturnNullopt_data();
    void invalidVersionOrTypesReturnNullopt();
    void boundaryValueCorrections();
    void stateStoreSaveAndLoad();
    void stateStoreNonExistentFile();
    void stateStoreCorruptedFile();
};

void TstPlaybackSnapshot::toJsonAndFromJsonRoundTrip_data()
{
    QTest::addColumn<PlaybackSnapshot>("snapshot");

    {
        PlaybackSnapshot snap;
        snap.items = {
            { .source = QStringLiteral("/home/user/music/track1.flac"), .trackId = 1 },
            { .source = QStringLiteral("/home/user/music/track2.mp3"), .trackId = 2 },
        };
        snap.currentIndex = 1;
        snap.position = 45.5;
        snap.mode = PlayMode::Sequential;
        snap.volume = 75;
        snap.muted = false;
        snap.audioDevice = QStringLiteral("auto");
        QTest::newRow("standard sequential snapshot") << snap;
    }

    {
        PlaybackSnapshot snap;
        snap.items = {
            { .source = QStringLiteral("/音乐/周杰伦 - 晴天.flac"), .trackId = 100 },
            { .source = QStringLiteral("/音乐/陈奕迅 - 十年.flac"), .trackId = 101 },
        };
        snap.currentIndex = 0;
        snap.position = 120.25;
        snap.mode = PlayMode::RepeatAll;
        snap.volume = 80;
        snap.muted = true;
        snap.audioDevice = QStringLiteral("alsa/default");
        QTest::newRow("chinese paths repeat all muted") << snap;
    }

    {
        PlaybackSnapshot snap;
        snap.items = { };
        snap.currentIndex = -1;
        snap.position = 0.0;
        snap.mode = PlayMode::RepeatOne;
        snap.volume = 0;
        snap.muted = false;
        snap.audioDevice = QStringLiteral("auto");
        QTest::newRow("empty queue repeat one") << snap;
    }

    {
        PlaybackSnapshot snap;
        snap.items = {
            { .source = QStringLiteral("/music/song.ogg"), .trackId = -1 },
        };
        snap.currentIndex = 0;
        snap.position = 10.0;
        snap.mode = PlayMode::Shuffle;
        snap.volume = 100;
        snap.muted = false;
        snap.audioDevice = QStringLiteral("pulse/hdmi");
        QTest::newRow("shuffle single track max volume") << snap;
    }
}

void TstPlaybackSnapshot::toJsonAndFromJsonRoundTrip()
{
    QFETCH(PlaybackSnapshot, snapshot);

    const QJsonObject json = snapshot.toJson();
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 1);

    const auto restored = PlaybackSnapshot::fromJson(json);
    QVERIFY(restored.has_value());
    if (!restored) {
        return;
    }
    QCOMPARE(restored.value(), snapshot);
}

void TstPlaybackSnapshot::missingFieldsUseDefaults()
{
    QJsonObject json;
    json.insert(QStringLiteral("version"), 1);

    const auto result = PlaybackSnapshot::fromJson(json);
    QVERIFY(result.has_value());
    if (!result) {
        return;
    }

    const PlaybackSnapshot &snap = *result;
    QVERIFY(snap.items.isEmpty());
    QCOMPARE(snap.currentIndex, -1);
    QCOMPARE(snap.position, 0.0);
    QCOMPARE(snap.mode, PlayMode::Sequential);
    QCOMPARE(snap.volume, 100);
    QCOMPARE(snap.muted, false);
    QCOMPARE(snap.audioDevice, QStringLiteral("auto"));
}

void TstPlaybackSnapshot::invalidVersionOrTypesReturnNullopt_data()
{
    QTest::addColumn<QJsonObject>("json");

    // Missing version
    {
        QJsonObject j;
        QTest::newRow("missing version") << j;
    }

    // Version not 1
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 0);
        QTest::newRow("version 0") << j;
    }
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 2);
        QTest::newRow("version 2") << j;
    }
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), QStringLiteral("1"));
        QTest::newRow("version string") << j;
    }

    // Items not array
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        j.insert(QStringLiteral("items"), QStringLiteral("not-an-array"));
        QTest::newRow("items is string") << j;
    }

    // Item element not object
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        QJsonArray arr;
        arr.append(QStringLiteral("string-item"));
        j.insert(QStringLiteral("items"), arr);
        QTest::newRow("items contains string element") << j;
    }

    // Item element missing source
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        QJsonArray arr;
        QJsonObject itemObj;
        itemObj.insert(QStringLiteral("trackId"), 1);
        arr.append(itemObj);
        j.insert(QStringLiteral("items"), arr);
        QTest::newRow("item missing source") << j;
    }

    // Item element source not string
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        QJsonArray arr;
        QJsonObject itemObj;
        itemObj.insert(QStringLiteral("source"), 12345);
        arr.append(itemObj);
        j.insert(QStringLiteral("items"), arr);
        QTest::newRow("item source is number") << j;
    }

    // Item element trackId not number
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        QJsonArray arr;
        QJsonObject itemObj;
        itemObj.insert(QStringLiteral("source"), QStringLiteral("/a.flac"));
        itemObj.insert(QStringLiteral("trackId"), QStringLiteral("1"));
        arr.append(itemObj);
        j.insert(QStringLiteral("items"), arr);
        QTest::newRow("item trackId is string") << j;
    }

    // CurrentIndex not number
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        j.insert(QStringLiteral("currentIndex"), QStringLiteral("0"));
        QTest::newRow("currentIndex is string") << j;
    }

    // Position not number
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        j.insert(QStringLiteral("position"), QStringLiteral("1.5"));
        QTest::newRow("position is string") << j;
    }

    // Mode invalid string
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        j.insert(QStringLiteral("mode"), QStringLiteral("InvalidMode"));
        QTest::newRow("invalid mode string") << j;
    }

    // Mode invalid int
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        j.insert(QStringLiteral("mode"), 99);
        QTest::newRow("invalid mode integer") << j;
    }

    // Volume not number
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        j.insert(QStringLiteral("volume"), QStringLiteral("80"));
        QTest::newRow("volume is string") << j;
    }

    // Muted not bool
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        j.insert(QStringLiteral("muted"), QStringLiteral("true"));
        QTest::newRow("muted is string") << j;
    }

    // AudioDevice not string
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        j.insert(QStringLiteral("audioDevice"), 123);
        QTest::newRow("audioDevice is number") << j;
    }
}

void TstPlaybackSnapshot::invalidVersionOrTypesReturnNullopt()
{
    QFETCH(QJsonObject, json);
    const auto result = PlaybackSnapshot::fromJson(json);
    QCOMPARE(result, std::optional<PlaybackSnapshot>(std::nullopt));
}

void TstPlaybackSnapshot::boundaryValueCorrections()
{
    // 1. Out of bounds currentIndex
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        QJsonArray items;
        QJsonObject it;
        it.insert(QStringLiteral("source"), QStringLiteral("/a.flac"));
        items.append(it);
        j.insert(QStringLiteral("items"), items);
        j.insert(QStringLiteral("currentIndex"), 5);

        const auto snap = PlaybackSnapshot::fromJson(j);
        QVERIFY(snap.has_value());
        if (!snap) {
            return;
        }
        QCOMPARE(snap->currentIndex, -1);
    }

    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        j.insert(QStringLiteral("currentIndex"), 0); // items empty

        const auto snap = PlaybackSnapshot::fromJson(j);
        QVERIFY(snap.has_value());
        if (!snap) {
            return;
        }
        QCOMPARE(snap->currentIndex, -1);
    }

    // 2. Negative position
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        j.insert(QStringLiteral("position"), -12.5);

        const auto snap = PlaybackSnapshot::fromJson(j);
        QVERIFY(snap.has_value());
        if (!snap) {
            return;
        }
        QCOMPARE(snap->position, 0.0);
    }

    // 3. Volume clamping
    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        j.insert(QStringLiteral("volume"), 150);

        const auto snap = PlaybackSnapshot::fromJson(j);
        QVERIFY(snap.has_value());
        if (!snap) {
            return;
        }
        QCOMPARE(snap->volume, 100);
    }

    {
        QJsonObject j;
        j.insert(QStringLiteral("version"), 1);
        j.insert(QStringLiteral("volume"), -20);

        const auto snap = PlaybackSnapshot::fromJson(j);
        QVERIFY(snap.has_value());
        if (!snap) {
            return;
        }
        QCOMPARE(snap->volume, 0);
    }
}

void TstPlaybackSnapshot::stateStoreSaveAndLoad()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString filePath = tempDir.filePath(QStringLiteral("snapshot.json"));
    PlaybackStateStore store(filePath);

    PlaybackSnapshot snap;
    snap.items = {
        { .source = QStringLiteral("/path/to/song.flac"), .trackId = 42 },
    };
    snap.currentIndex = 0;
    snap.position = 33.3;
    snap.mode = PlayMode::RepeatAll;
    snap.volume = 60;
    snap.muted = true;
    snap.audioDevice = QStringLiteral("pulse/default");

    const bool saved = store.save(snap);
    QVERIFY(saved);
    QVERIFY(QFile::exists(filePath));

    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    if (!loaded) {
        return;
    }
    QCOMPARE(loaded.value(), snap);
}

void TstPlaybackSnapshot::stateStoreNonExistentFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString filePath = tempDir.filePath(QStringLiteral("nonexistent.json"));
    PlaybackStateStore store(filePath);

    const auto loaded = store.load();
    QCOMPARE(loaded, std::optional<PlaybackSnapshot>(std::nullopt));
}

void TstPlaybackSnapshot::stateStoreCorruptedFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString filePath = tempDir.filePath(QStringLiteral("corrupted.json"));
    PlaybackStateStore store(filePath);

    // 1. Not valid JSON
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not valid json at all {{{{");
        file.close();

        const auto loaded = store.load();
        QCOMPARE(loaded, std::optional<PlaybackSnapshot>(std::nullopt));
    }

    // 2. Valid JSON but invalid snapshot (version 99)
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("{\"version\": 99}");
        file.close();

        const auto loaded = store.load();
        QCOMPARE(loaded, std::optional<PlaybackSnapshot>(std::nullopt));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstPlaybackSnapshot)

#include "tst_PlaybackSnapshot.moc"
