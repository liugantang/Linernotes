#include <QDir>
#include <QFile>
#include <QObject>
#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <core/CoreSettings.h>
#include <core/Settings.h>

namespace {

using namespace Qt::StringLiterals;

constexpr aimusic::core::SettingKey<bool> kTestBool { u"test/bool", true };
constexpr aimusic::core::SettingKey<int> kTestInt { u"test/int", 42 };
constexpr aimusic::core::SettingKey<double> kTestDouble { u"test/double", 3.14 };

aimusic::core::SettingKey<QString> makeTestStringKey()
{
    return aimusic::core::SettingKey<QString> { u"test/string", u"default_str"_s };
}

aimusic::core::SettingKey<QStringList> makeTestListKey()
{
    return aimusic::core::SettingKey<QStringList> { u"test/list",
        QStringList { u"alpha"_s, u"beta"_s } };
}

class TstSettings : public QObject {
    Q_OBJECT

private slots:
    void defaultValuesWhenKeyNotPresent();
    void writeAndReadSupportedTypes();
    void persistenceAcrossInstances();
    void resetRestoresDefaultAndEmitsSignal();
    void unchangedValueDoesNotEmitChanged();
    void typeMismatchFallsBackToDefault();
    void coreSettingsLogLevel();
};

void TstSettings::defaultValuesWhenKeyNotPresent()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    const aimusic::core::Settings settings(iniPath);

    QCOMPARE(settings.value(kTestBool), true);
    QCOMPARE(settings.value(kTestInt), 42);
    QCOMPARE(settings.value(kTestDouble), 3.14);
    QCOMPARE(settings.value(makeTestStringKey()), QStringLiteral("default_str"));
    QCOMPARE(settings.value(makeTestListKey()),
        (QStringList { QStringLiteral("alpha"), QStringLiteral("beta") }));
}

void TstSettings::writeAndReadSupportedTypes()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    aimusic::core::Settings settings(iniPath);

    settings.setValue(kTestBool, false);
    QCOMPARE(settings.value(kTestBool), false);

    settings.setValue(kTestInt, 100);
    QCOMPARE(settings.value(kTestInt), 100);

    settings.setValue(kTestDouble, 2.75);
    QCOMPARE(settings.value(kTestDouble), 2.75);

    const auto kTestString = makeTestStringKey();
    settings.setValue(kTestString, QStringLiteral("updated_value"));
    QCOMPARE(settings.value(kTestString), QStringLiteral("updated_value"));

    const auto kTestList = makeTestListKey();
    const QStringList updatedList
        = { QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third") };
    settings.setValue(kTestList, updatedList);
    QCOMPARE(settings.value(kTestList), updatedList);
}

void TstSettings::persistenceAcrossInstances()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    const QStringList sampleList = { QStringLiteral("one"), QStringLiteral("two") };
    const auto kTestString = makeTestStringKey();
    const auto kTestList = makeTestListKey();

    {
        aimusic::core::Settings s1(iniPath);
        s1.setValue(kTestBool, false);
        s1.setValue(kTestInt, 2026);
        s1.setValue(kTestDouble, 99.9);
        s1.setValue(kTestString, QStringLiteral("persisted_text"));
        s1.setValue(kTestList, sampleList);
        s1.sync();
    }

    {
        const aimusic::core::Settings s2(iniPath);
        QCOMPARE(s2.value(kTestBool), false);
        QCOMPARE(s2.value(kTestInt), 2026);
        QCOMPARE(s2.value(kTestDouble), 99.9);
        QCOMPARE(s2.value(kTestString), QStringLiteral("persisted_text"));
        QCOMPARE(s2.value(kTestList), sampleList);
    }
}

void TstSettings::resetRestoresDefaultAndEmitsSignal()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    aimusic::core::Settings settings(iniPath);

    settings.setValue(kTestInt, 999);
    QCOMPARE(settings.value(kTestInt), 999);

    QSignalSpy spy(&settings, &aimusic::core::Settings::changed);

    settings.reset(kTestInt);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("test/int"));
    QCOMPARE(settings.value(kTestInt), 42);

    // Resetting an absent key must not emit signal
    settings.reset(kTestInt);
    QCOMPARE(spy.count(), 0);
}

void TstSettings::unchangedValueDoesNotEmitChanged()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    aimusic::core::Settings settings(iniPath);

    QSignalSpy spy(&settings, &aimusic::core::Settings::changed);

    // Initial writes emit signals
    settings.setValue(kTestBool, false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("test/bool"));

    // Writing the same value should not emit signals
    settings.setValue(kTestBool, false);
    QCOMPARE(spy.count(), 0);

    // For int
    settings.setValue(kTestInt, 77);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("test/int"));
    settings.setValue(kTestInt, 77);
    QCOMPARE(spy.count(), 0);

    // For double
    settings.setValue(kTestDouble, 6.28);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("test/double"));
    settings.setValue(kTestDouble, 6.28);
    QCOMPARE(spy.count(), 0);

    // For string
    const auto kTestString = makeTestStringKey();
    settings.setValue(kTestString, QStringLiteral("sample"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("test/string"));
    settings.setValue(kTestString, QStringLiteral("sample"));
    QCOMPARE(spy.count(), 0);

    // For list
    const auto kTestList = makeTestListKey();
    const QStringList listVal = { QStringLiteral("a"), QStringLiteral("b") };
    settings.setValue(kTestList, listVal);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("test/list"));
    settings.setValue(kTestList, listVal);
    QCOMPARE(spy.count(), 0);
}

void TstSettings::typeMismatchFallsBackToDefault()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));

    // Write corrupted / type-mismatched entries directly into the INI file
    {
        QSettings rawSettings(iniPath, QSettings::IniFormat);
        rawSettings.setValue(QStringLiteral("test/bool"), QStringLiteral("not_a_bool"));
        rawSettings.setValue(QStringLiteral("test/int"), QStringLiteral("not_a_number"));
        rawSettings.setValue(QStringLiteral("test/double"), QStringLiteral("not_a_double"));
        rawSettings.sync();
    }

    const aimusic::core::Settings settings(iniPath);
    QCOMPARE(settings.value(kTestBool), true);
    QCOMPARE(settings.value(kTestInt), 42);
    QCOMPARE(settings.value(kTestDouble), 3.14);
}

void TstSettings::coreSettingsLogLevel()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));
    aimusic::core::Settings settings(iniPath);

    QCOMPARE(settings.value(aimusic::core::kLogLevel), QStringLiteral("info"));

    settings.setValue(aimusic::core::kLogLevel, QStringLiteral("debug"));
    QCOMPARE(settings.value(aimusic::core::kLogLevel), QStringLiteral("debug"));
}

} // namespace

QTEST_GUILESS_MAIN(TstSettings)

#include "tst_Settings.moc"
