// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 AiMusic contributors

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <core/Paths.h>
#include <core/Version.h>

namespace {

class TstPaths : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void underRootPathsCorrect();
    void standardPathsMatchConventions();
    void ensureCreatedCreatesAllDirectories();
    void fromEnvironmentWithAndWithoutEnvVar();

private:
    bool m_hadOriginalAimusicHome = false;
    QByteArray m_originalAimusicHome;
};

void TstPaths::initTestCase()
{
    QCoreApplication::setOrganizationName(QString());
    QCoreApplication::setApplicationName(aimusic::core::applicationName());

    m_hadOriginalAimusicHome = qEnvironmentVariableIsSet("AIMUSIC_HOME");
    if (m_hadOriginalAimusicHome) {
        m_originalAimusicHome = qgetenv("AIMUSIC_HOME");
    }
}

void TstPaths::cleanupTestCase()
{
    if (m_hadOriginalAimusicHome) {
        qputenv("AIMUSIC_HOME", m_originalAimusicHome);
    } else {
        qunsetenv("AIMUSIC_HOME");
    }
}

void TstPaths::init()
{
    qunsetenv("AIMUSIC_HOME");
}

void TstPaths::cleanup()
{
    qunsetenv("AIMUSIC_HOME");
}

void TstPaths::underRootPathsCorrect()
{
    const QString root = QStringLiteral("/custom/app_root");
    const auto paths = aimusic::core::Paths::underRoot(root);

    QCOMPARE(paths.configDir(), QStringLiteral("/custom/app_root/config"));
    QCOMPARE(paths.dataDir(), QStringLiteral("/custom/app_root/data"));
    QCOMPARE(paths.cacheDir(), QStringLiteral("/custom/app_root/cache"));
    QCOMPARE(paths.logDir(), QStringLiteral("/custom/app_root/logs"));

    // Trailing slash handled cleanly
    const auto pathsTrailing = aimusic::core::Paths::underRoot(QStringLiteral("/custom/app_root/"));
    QCOMPARE(pathsTrailing.configDir(), QStringLiteral("/custom/app_root/config"));
    QCOMPARE(pathsTrailing.dataDir(), QStringLiteral("/custom/app_root/data"));
    QCOMPARE(pathsTrailing.cacheDir(), QStringLiteral("/custom/app_root/cache"));
    QCOMPARE(pathsTrailing.logDir(), QStringLiteral("/custom/app_root/logs"));
}

void TstPaths::standardPathsMatchConventions()
{
    const auto stdPaths = aimusic::core::Paths::standard();

    QVERIFY(!stdPaths.configDir().isEmpty());
    QVERIFY(!stdPaths.dataDir().isEmpty());
    QVERIFY(!stdPaths.cacheDir().isEmpty());
    QCOMPARE(stdPaths.logDir(), QDir(stdPaths.dataDir()).filePath(QStringLiteral("logs")));

#ifdef Q_OS_LINUX
    // On Linux with organizationName="" and applicationName="AiMusic",
    // standard config/data/cache paths end with /AiMusic.
    QVERIFY(stdPaths.configDir().endsWith(QStringLiteral("/AiMusic")));
    QVERIFY(stdPaths.dataDir().endsWith(QStringLiteral("/AiMusic")));
    QVERIFY(stdPaths.cacheDir().endsWith(QStringLiteral("/AiMusic")));
#endif
}

void TstPaths::ensureCreatedCreatesAllDirectories()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root = QDir(tempDir.path()).filePath(QStringLiteral("test_root"));
    const auto paths = aimusic::core::Paths::underRoot(root);

    QVERIFY(!QDir(paths.configDir()).exists());
    QVERIFY(!QDir(paths.dataDir()).exists());
    QVERIFY(!QDir(paths.cacheDir()).exists());
    QVERIFY(!QDir(paths.logDir()).exists());

    QVERIFY(paths.ensureCreated());

    QVERIFY(QDir(paths.configDir()).exists());
    QVERIFY(QDir(paths.dataDir()).exists());
    QVERIFY(QDir(paths.cacheDir()).exists());
    QVERIFY(QDir(paths.logDir()).exists());

    // Second call returns true when directories already exist
    QVERIFY(paths.ensureCreated());
}

void TstPaths::fromEnvironmentWithAndWithoutEnvVar()
{
    // 1. Without AIMUSIC_HOME
    qunsetenv("AIMUSIC_HOME");
    const auto pathsStandard = aimusic::core::Paths::fromEnvironment();
    const auto expectedStandard = aimusic::core::Paths::standard();
    QCOMPARE(pathsStandard.configDir(), expectedStandard.configDir());
    QCOMPARE(pathsStandard.dataDir(), expectedStandard.dataDir());
    QCOMPARE(pathsStandard.cacheDir(), expectedStandard.cacheDir());
    QCOMPARE(pathsStandard.logDir(), expectedStandard.logDir());

    // 2. With AIMUSIC_HOME set
    const QString customHome = QStringLiteral("/tmp/custom_aimusic_home");
    qputenv("AIMUSIC_HOME", customHome.toUtf8());
    const auto pathsCustom = aimusic::core::Paths::fromEnvironment();
    const auto expectedCustom = aimusic::core::Paths::underRoot(customHome);
    QCOMPARE(pathsCustom.configDir(), expectedCustom.configDir());
    QCOMPARE(pathsCustom.dataDir(), expectedCustom.dataDir());
    QCOMPARE(pathsCustom.cacheDir(), expectedCustom.cacheDir());
    QCOMPARE(pathsCustom.logDir(), expectedCustom.logDir());

    // 3. Clear AIMUSIC_HOME again
    qunsetenv("AIMUSIC_HOME");
    const auto pathsRestored = aimusic::core::Paths::fromEnvironment();
    QCOMPARE(pathsRestored.configDir(), expectedStandard.configDir());
}

} // namespace

QTEST_GUILESS_MAIN(TstPaths)

#include "tst_Paths.moc"
