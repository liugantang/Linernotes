// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <common/TestSupport.h>
#include <player/MpvHandle.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

using linernotes::player::MpvHandle;

namespace {

class TstMpvHandle : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void initializesWithDefaultOptions();
    void extraOptionsOverrideDefaults();
    void invalidExtraOptionIsNonFatal();

    void propertyRoundTrip_data();
    void propertyRoundTrip();

    void readsStructuredProperty();
    void readingUnknownPropertyReturnsInvalid();
    void commandFailsForUnknownCommand();

    void loadFileEmitsLifecycleEvents();
    void emitsAudioReconfigured();
    void observedPropertiesEmitChanges();
    void observeSamePropertyTwiceRegistersOnce();
    void loadingCorruptFileEndsWithError();
    void destroysCleanlyWhilePlaying();
};

void TstMpvHandle::initTestCase()
{
    qRegisterMetaType<MpvHandle::EndFileReason>();
}

void TstMpvHandle::initializesWithDefaultOptions()
{
    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());
    QVERIFY(handle.errorString().isEmpty());
    const QVariant vid = handle.property(QStringLiteral("vid"));
    QVERIFY(vid == QVariant(false) || vid.toString() == QStringLiteral("no")
        || vid.toString() == QStringLiteral("false"));
    QCOMPARE(handle.property(QStringLiteral("gapless-audio")).toString(), QStringLiteral("weak"));
    QCOMPARE(handle.property(QStringLiteral("replaygain")).toString(), QStringLiteral("track"));
}

void TstMpvHandle::extraOptionsOverrideDefaults()
{
    MpvHandle handle({
        { QStringLiteral("ao"), QStringLiteral("null") },
        { QStringLiteral("replaygain"), QStringLiteral("album") },
    });
    QVERIFY(handle.isValid());
    QCOMPARE(handle.property(QStringLiteral("replaygain")).toString(), QStringLiteral("album"));
}

void TstMpvHandle::invalidExtraOptionIsNonFatal()
{
    MpvHandle handle({
        { QStringLiteral("ao"), QStringLiteral("null") },
        { QStringLiteral("nonexistent-extra-option-xyz"), QStringLiteral("123") },
    });
    QVERIFY(handle.isValid());
}

void TstMpvHandle::propertyRoundTrip_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QVariant>("value");

    QTest::newRow("volume") << QStringLiteral("volume") << QVariant(75.0);
    QTest::newRow("pause") << QStringLiteral("pause") << QVariant(true);
    QTest::newRow("mute") << QStringLiteral("mute") << QVariant(true);
}

void TstMpvHandle::propertyRoundTrip()
{
    QFETCH(QString, name);
    QFETCH(QVariant, value);

    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());
    QVERIFY(handle.setProperty(name, value));
    QCOMPARE(handle.property(name), value);
}

void TstMpvHandle::readsStructuredProperty()
{
    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());

    const QVariant devListVar = handle.property(QStringLiteral("audio-device-list"));
    QVERIFY(devListVar.isValid());
    QCOMPARE(devListVar.metaType().id(), QMetaType::QVariantList);

    const QVariantList devList = devListVar.toList();
    QVERIFY(!devList.isEmpty());

    const QVariantMap firstDev = devList.first().toMap();
    QVERIFY(firstDev.contains(QStringLiteral("name")));
}

void TstMpvHandle::readingUnknownPropertyReturnsInvalid()
{
    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());
    QVERIFY(!handle.property(QStringLiteral("nonexistent-property-xyz")).isValid());
}

void TstMpvHandle::commandFailsForUnknownCommand()
{
    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());
    QVERIFY(!handle.command({ QStringLiteral("nonexistent-command-xyz") }));
}

void TstMpvHandle::loadFileEmitsLifecycleEvents()
{
    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());

    QSignalSpy startSpy(&handle, &MpvHandle::startFile);
    QSignalSpy loadedSpy(&handle, &MpvHandle::fileLoaded);
    QSignalSpy endSpy(&handle, &MpvHandle::endFile);

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    QVERIFY(QFile::exists(path));
    QVERIFY(handle.command({ QStringLiteral("loadfile"), path, QStringLiteral("replace") }));

    QTRY_COMPARE_WITH_TIMEOUT(startSpy.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(loadedSpy.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(endSpy.count(), 1, 5000);

    const auto endReason = endSpy.at(0).at(1).value<MpvHandle::EndFileReason>();
    QCOMPARE(endReason, MpvHandle::EndFileReason::Eof);
    QVERIFY(endSpy.at(0).at(2).toString().isEmpty());
}

void TstMpvHandle::emitsAudioReconfigured()
{
    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());

    QSignalSpy reconfigSpy(&handle, &MpvHandle::audioReconfigured);

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    QVERIFY(QFile::exists(path));
    QVERIFY(handle.command({ QStringLiteral("loadfile"), path, QStringLiteral("replace") }));

    QTRY_VERIFY_WITH_TIMEOUT(reconfigSpy.count() >= 1, 5000);
}

void TstMpvHandle::observedPropertiesEmitChanges()
{
    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());

    handle.observeProperty(QStringLiteral("time-pos"));
    handle.observeProperty(QStringLiteral("duration"));
    handle.observeProperty(QStringLiteral("pause"));
    handle.observeProperty(QStringLiteral("volume"));
    handle.observeProperty(QStringLiteral("idle-active"));
    handle.observeProperty(QStringLiteral("playlist-pos"));

    QSignalSpy propSpy(&handle, &MpvHandle::propertyChanged);
    QSignalSpy endSpy(&handle, &MpvHandle::endFile);

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    QVERIFY(QFile::exists(path));
    QVERIFY(handle.command({ QStringLiteral("loadfile"), path, QStringLiteral("replace") }));

    QTRY_COMPARE_WITH_TIMEOUT(endSpy.count(), 1, 5000);

    bool durationMatched = false;
    bool timePosChanged = false;
    for (const auto &signal : std::as_const(propSpy)) {
        const QString name = signal.at(0).toString();
        const QVariant &val = signal.at(1);
        if (name == QStringLiteral("duration") && val.isValid()) {
            const double d = val.toDouble();
            if (std::abs(d - 1.0) <= 0.1) {
                durationMatched = true;
            }
        }
        if (name == QStringLiteral("time-pos") && val.isValid()) {
            timePosChanged = true;
        }
    }
    QVERIFY(durationMatched);
    QVERIFY(timePosChanged);

    QTRY_VERIFY_WITH_TIMEOUT(handle.property(QStringLiteral("idle-active")).toBool(), 2000);

    propSpy.clear();
    QVERIFY(handle.setProperty(QStringLiteral("volume"), 65.0));
    QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(std::as_const(propSpy),
                                 [](const auto &signal) {
                                     return signal.at(0).toString() == QStringLiteral("volume")
                                         && std::abs(signal.at(1).toDouble() - 65.0) < 0.001;
                                 }),
        2000);
}

void TstMpvHandle::observeSamePropertyTwiceRegistersOnce()
{
    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());

    handle.observeProperty(QStringLiteral("volume"));
    handle.observeProperty(QStringLiteral("volume"));

    QSignalSpy propSpy(&handle, &MpvHandle::propertyChanged);

    QTRY_VERIFY_WITH_TIMEOUT(propSpy.count() >= 1, 2000);
    propSpy.clear();

    QVERIFY(handle.setProperty(QStringLiteral("volume"), 42.0));
    QTRY_COMPARE_WITH_TIMEOUT(propSpy.count(), 1, 2000);
    QCOMPARE(propSpy.at(0).at(0).toString(), QStringLiteral("volume"));
    QCOMPARE(propSpy.at(0).at(1).toDouble(), 42.0);
}

void TstMpvHandle::loadingCorruptFileEndsWithError()
{
    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());

    QSignalSpy endSpy(&handle, &MpvHandle::endFile);

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/corrupt.flac"));
    QVERIFY(QFile::exists(path));
    QVERIFY(handle.command({ QStringLiteral("loadfile"), path, QStringLiteral("replace") }));

    QTRY_COMPARE_WITH_TIMEOUT(endSpy.count(), 1, 5000);

    const auto endReason = endSpy.at(0).at(1).value<MpvHandle::EndFileReason>();
    QCOMPARE(endReason, MpvHandle::EndFileReason::Error);
    const QString error = endSpy.at(0).at(2).toString();
    QVERIFY(!error.isEmpty());
}

void TstMpvHandle::destroysCleanlyWhilePlaying()
{
    {
        auto handle = std::make_unique<MpvHandle>(
            MpvHandle::OptionList { { QStringLiteral("ao"), QStringLiteral("null") } });
        QVERIFY(handle->isValid());
        const QString path
            = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
        QVERIFY(handle->command({ QStringLiteral("loadfile"), path, QStringLiteral("replace") }));
        QTest::qWait(50);
    }
}

} // namespace

QTEST_MAIN(TstMpvHandle)
#include "tst_MpvHandle.moc"
