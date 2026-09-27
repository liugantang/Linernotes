// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <common/TestSupport.h>
#include <player/MpvHandle.h>

using linernotes::player::MpvHandle;

namespace {

class TstMpvHandle : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void initializesAndSetsProperties();
    void readsAudioDeviceList();
    void loadFileAndLifecycleEvents();
    void loadingCorruptFileEndsWithError();
    void doesNotStartBuiltinLuaScripts();
};

void TstMpvHandle::initTestCase()
{
    qRegisterMetaType<MpvHandle::EndFileReason>();
}

void TstMpvHandle::initializesAndSetsProperties()
{
    MpvHandle handle({
        { QStringLiteral("ao"), QStringLiteral("null") },
        { QStringLiteral("replaygain"), QStringLiteral("album") },
    });
    QVERIFY(handle.isValid());
    QCOMPARE(handle.property(QStringLiteral("gapless-audio")).toString(), QStringLiteral("weak"));
    QCOMPARE(handle.property(QStringLiteral("replaygain")).toString(), QStringLiteral("album"));

    QVERIFY(handle.setProperty(QStringLiteral("volume"), 75.0));
    QCOMPARE(handle.property(QStringLiteral("volume")).toDouble(), 75.0);
}

void TstMpvHandle::readsAudioDeviceList()
{
    if (!qEnvironmentVariableIsEmpty("LINERNOTES_NO_AUDIO_SERVER")) {
        QSKIP("LINERNOTES_NO_AUDIO_SERVER set: audio device enumeration may deadlock in "
              "libpipewire without audio server");
    }

    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());

    const QVariant devListVar = handle.property(QStringLiteral("audio-device-list"));
    QVERIFY(devListVar.isValid());
    QCOMPARE(devListVar.metaType().id(), QMetaType::QVariantList);

    const QVariantList devList = devListVar.toList();
    QVERIFY(!devList.isEmpty());
    QVERIFY(devList.first().toMap().contains(QStringLiteral("name")));
}

void TstMpvHandle::loadFileAndLifecycleEvents()
{
    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());

    handle.observeProperty(QStringLiteral("time-pos"));
    handle.observeProperty(QStringLiteral("duration"));

    QSignalSpy startSpy(&handle, &MpvHandle::startFile);
    QSignalSpy loadedSpy(&handle, &MpvHandle::fileLoaded);
    QSignalSpy endSpy(&handle, &MpvHandle::endFile);
    QSignalSpy propSpy(&handle, &MpvHandle::propertyChanged);

    const QString path = linernotes::test::fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    QVERIFY(QFile::exists(path));
    QVERIFY(handle.command({ QStringLiteral("loadfile"), path, QStringLiteral("replace") }));

    QTRY_COMPARE_WITH_TIMEOUT(startSpy.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(loadedSpy.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(endSpy.count(), 1, 5000);

    const auto endReason = endSpy.at(0).at(1).value<MpvHandle::EndFileReason>();
    QCOMPARE(endReason, MpvHandle::EndFileReason::Eof);
    QVERIFY(propSpy.count() > 0);
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
    QVERIFY(!endSpy.at(0).at(2).toString().isEmpty());
}

void TstMpvHandle::doesNotStartBuiltinLuaScripts()
{
#ifdef Q_OS_LINUX
    MpvHandle handle({ { QStringLiteral("ao"), QStringLiteral("null") } });
    QVERIFY(handle.isValid());
    QTest::qWait(50);

    const QDir taskDir(QStringLiteral("/proc/self/task"));
    const QStringList taskEntries = taskDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &tid : taskEntries) {
        QFile commFile(taskDir.filePath(tid + QStringLiteral("/comm")));
        if (commFile.open(QIODevice::ReadOnly)) {
            const QString comm = QString::fromUtf8(commFile.readAll()).trimmed();
            QVERIFY2(!comm.startsWith(QStringLiteral("lua/")),
                qPrintable(QStringLiteral("Found Lua thread: %1").arg(comm)));
        }
    }
#else
    QSKIP("Linux only test for /proc/self/task");
#endif
}

} // namespace

QTEST_MAIN(TstMpvHandle)
#include "tst_MpvHandle.moc"
