// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <player/PlaybackSnapshot.h>

using linernotes::player::PlaybackSnapshot;
using linernotes::player::PlaybackStateStore;
using linernotes::player::PlayMode;

namespace {

class TstPlaybackSnapshot : public QObject {
    Q_OBJECT

private slots:
    void toJsonAndFromJsonRoundTrip();
    void missingFieldsUseDefaults();
    void invalidJsonReturnsNullopt();
    void boundaryValueCorrections();
    void stateStoreSaveLoadAndErrors();
};

void TstPlaybackSnapshot::toJsonAndFromJsonRoundTrip()
{
    PlaybackSnapshot snap;
    snap.items = {
        { .source = QStringLiteral("/music/track1.flac"), .trackId = 1 },
        { .source = QStringLiteral("/音乐/周杰伦 - 晴天.flac"), .trackId = 100 },
    };
    snap.currentIndex = 1;
    snap.position = 45.5;
    snap.mode = PlayMode::RepeatAll;
    snap.volume = 75;
    snap.muted = true;
    snap.audioDevice = QStringLiteral("auto");

    const QJsonObject json = snap.toJson();
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 1);

    const auto restored = PlaybackSnapshot::fromJson(json);
    if (!restored.has_value()) {
        QFAIL("restored snapshot is nullopt");
        return;
    }
    QCOMPARE(*restored, snap);
}

void TstPlaybackSnapshot::missingFieldsUseDefaults()
{
    QJsonObject json;
    json.insert(QStringLiteral("version"), 1);

    const auto result = PlaybackSnapshot::fromJson(json);
    if (!result.has_value()) {
        QFAIL("result is nullopt");
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

void TstPlaybackSnapshot::invalidJsonReturnsNullopt()
{
    // Missing version
    QJsonObject j1;
    QCOMPARE(PlaybackSnapshot::fromJson(j1), std::nullopt);

    // Unsupported version
    QJsonObject j2;
    j2.insert(QStringLiteral("version"), 99);
    QCOMPARE(PlaybackSnapshot::fromJson(j2), std::nullopt);

    // Items not array
    QJsonObject j3;
    j3.insert(QStringLiteral("version"), 1);
    j3.insert(QStringLiteral("items"), QStringLiteral("invalid"));
    QCOMPARE(PlaybackSnapshot::fromJson(j3), std::nullopt);
}

void TstPlaybackSnapshot::boundaryValueCorrections()
{
    // Out of bounds currentIndex clamped to -1
    QJsonObject j;
    j.insert(QStringLiteral("version"), 1);
    j.insert(QStringLiteral("currentIndex"), 5);
    j.insert(QStringLiteral("position"), -10.0);
    j.insert(QStringLiteral("volume"), 150);

    const auto snap = PlaybackSnapshot::fromJson(j);
    if (!snap.has_value()) {
        QFAIL("snap is nullopt");
        return;
    }
    QCOMPARE(snap->currentIndex, -1);
    QCOMPARE(snap->position, 0.0);
    QCOMPARE(snap->volume, 100);
}

void TstPlaybackSnapshot::stateStoreSaveLoadAndErrors()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString filePath = tempDir.filePath(QStringLiteral("snapshot.json"));
    PlaybackStateStore store(filePath);

    // Non-existent file
    QCOMPARE(store.load(), std::nullopt);

    // Save and load
    PlaybackSnapshot snap;
    snap.items = { { .source = QStringLiteral("/song.flac"), .trackId = 42 } };
    snap.currentIndex = 0;
    snap.position = 33.3;

    QVERIFY(store.save(snap));
    QVERIFY(QFile::exists(filePath));

    const auto loaded = store.load();
    if (!loaded.has_value()) {
        QFAIL("loaded is nullopt");
        return;
    }
    QCOMPARE(*loaded, snap);

    // Corrupted file
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("corrupted json {{{{");
    file.close();
    QCOMPARE(store.load(), std::nullopt);
}

} // namespace

QTEST_GUILESS_MAIN(TstPlaybackSnapshot)

#include "tst_PlaybackSnapshot.moc"
