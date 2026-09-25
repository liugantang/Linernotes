// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

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
    bool m_hadOriginalLinernotesHome = false;
    QByteArray m_originalLinernotesHome;
};

void TstPaths::initTestCase()
{
    QCoreApplication::setOrganizationName(QString());
    QCoreApplication::setApplicationName(linernotes::core::applicationName());

    m_hadOriginalLinernotesHome = qEnvironmentVariableIsSet("LINERNOTES_HOME");
    if (m_hadOriginalLinernotesHome) {
        m_originalLinernotesHome = qgetenv("LINERNOTES_HOME");
    }
}

void TstPaths::cleanupTestCase()
{
    if (m_hadOriginalLinernotesHome) {
        qputenv("LINERNOTES_HOME", m_originalLinernotesHome);
    } else {
        qunsetenv("LINERNOTES_HOME");
    }
}

void TstPaths::init()
{
    qunsetenv("LINERNOTES_HOME");
}

void TstPaths::cleanup()
{
    qunsetenv("LINERNOTES_HOME");
}

void TstPaths::underRootPathsCorrect()
{
    const QString root = QStringLiteral("/custom/app_root");
    const auto paths = linernotes::core::Paths::underRoot(root);

    QCOMPARE(paths.configDir(), QStringLiteral("/custom/app_root/config"));
    QCOMPARE(paths.dataDir(), QStringLiteral("/custom/app_root/data"));
    QCOMPARE(paths.cacheDir(), QStringLiteral("/custom/app_root/cache"));
    QCOMPARE(paths.logDir(), QStringLiteral("/custom/app_root/logs"));

    // Trailing slash handled cleanly
    const auto pathsTrailing
        = linernotes::core::Paths::underRoot(QStringLiteral("/custom/app_root/"));
    QCOMPARE(pathsTrailing.configDir(), QStringLiteral("/custom/app_root/config"));
    QCOMPARE(pathsTrailing.dataDir(), QStringLiteral("/custom/app_root/data"));
    QCOMPARE(pathsTrailing.cacheDir(), QStringLiteral("/custom/app_root/cache"));
    QCOMPARE(pathsTrailing.logDir(), QStringLiteral("/custom/app_root/logs"));
}

void TstPaths::standardPathsMatchConventions()
{
    const auto stdPaths = linernotes::core::Paths::standard();

    QVERIFY(!stdPaths.configDir().isEmpty());
    QVERIFY(!stdPaths.dataDir().isEmpty());
    QVERIFY(!stdPaths.cacheDir().isEmpty());
    QCOMPARE(stdPaths.logDir(), QDir(stdPaths.dataDir()).filePath(QStringLiteral("logs")));

#ifdef Q_OS_LINUX
    // On Linux with organizationName="" and applicationName="Linernotes",
    // standard config/data/cache paths end with /Linernotes.
    QVERIFY(stdPaths.configDir().endsWith(QStringLiteral("/Linernotes")));
    QVERIFY(stdPaths.dataDir().endsWith(QStringLiteral("/Linernotes")));
    QVERIFY(stdPaths.cacheDir().endsWith(QStringLiteral("/Linernotes")));
#endif
}

void TstPaths::ensureCreatedCreatesAllDirectories()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root = QDir(tempDir.path()).filePath(QStringLiteral("test_root"));
    const auto paths = linernotes::core::Paths::underRoot(root);

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
    // 1. Without LINERNOTES_HOME
    qunsetenv("LINERNOTES_HOME");
    const auto pathsStandard = linernotes::core::Paths::fromEnvironment();
    const auto expectedStandard = linernotes::core::Paths::standard();
    QCOMPARE(pathsStandard.configDir(), expectedStandard.configDir());
    QCOMPARE(pathsStandard.dataDir(), expectedStandard.dataDir());
    QCOMPARE(pathsStandard.cacheDir(), expectedStandard.cacheDir());
    QCOMPARE(pathsStandard.logDir(), expectedStandard.logDir());

    // 2. With LINERNOTES_HOME set
    const QString customHome = QStringLiteral("/tmp/custom_linernotes_home");
    qputenv("LINERNOTES_HOME", customHome.toUtf8());
    const auto pathsCustom = linernotes::core::Paths::fromEnvironment();
    const auto expectedCustom = linernotes::core::Paths::underRoot(customHome);
    QCOMPARE(pathsCustom.configDir(), expectedCustom.configDir());
    QCOMPARE(pathsCustom.dataDir(), expectedCustom.dataDir());
    QCOMPARE(pathsCustom.cacheDir(), expectedCustom.cacheDir());
    QCOMPARE(pathsCustom.logDir(), expectedCustom.logDir());

    // 3. Clear LINERNOTES_HOME again
    qunsetenv("LINERNOTES_HOME");
    const auto pathsRestored = linernotes::core::Paths::fromEnvironment();
    QCOMPARE(pathsRestored.configDir(), expectedStandard.configDir());
}

} // namespace

QTEST_GUILESS_MAIN(TstPaths)

#include "tst_Paths.moc"
