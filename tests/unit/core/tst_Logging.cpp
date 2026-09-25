// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QtLogging>

#include <core/Logging.h>

#include <array>
#include <thread>
#include <vector>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
Q_LOGGING_CATEGORY(lcTestOff, "linernotes.test.off", QtInfoMsg)
Q_LOGGING_CATEGORY(lcTestRules, "linernotes.test.rules")
} // namespace

namespace {

class TstLogging : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void formatLogLineDataDriven_data();
    void formatLogLineDataDriven();

    void writesLogsToFile();
    void fileRollingRespectsLimits();
    void minimumLevelFiltersOutLowerLevels();
    void categoryFilterPreservesDisabledDebug();
    void categoryFilterPreservesQtLoggingRules();
    void minimumLevelDisablesLevelsAndRestoresOnUninstall();
    void reconfigureMinimumLevelUpdatesCategories();
    void multiThreadedLoggingIntegrity();
    void readOnlyDirectoryDoesNotCrash();
    void reconfigureLoggingUpdatesSink();

private:
    bool m_hadOriginalLoggingRules = false;
    QByteArray m_originalLoggingRules;
};

void TstLogging::initTestCase()
{
    m_hadOriginalLoggingRules = qEnvironmentVariableIsSet("QT_LOGGING_RULES");
    if (m_hadOriginalLoggingRules) {
        m_originalLoggingRules = qgetenv("QT_LOGGING_RULES");
        qunsetenv("QT_LOGGING_RULES");
    }
}

void TstLogging::cleanupTestCase()
{
    if (m_hadOriginalLoggingRules) {
        qputenv("QT_LOGGING_RULES", m_originalLoggingRules);
    } else {
        qunsetenv("QT_LOGGING_RULES");
    }
}

void TstLogging::init()
{
    linernotes::core::uninstallLogging();
    QLoggingCategory::setFilterRules(QString());
}

void TstLogging::cleanup()
{
    linernotes::core::uninstallLogging();
    QLoggingCategory::setFilterRules(QString());
}

void TstLogging::formatLogLineDataDriven_data()
{
    QTest::addColumn<int>("msgType");
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("line");
    QTest::addColumn<QString>("category");
    QTest::addColumn<QString>("message");
    QTest::addColumn<QDateTime>("timestamp");
    QTest::addColumn<QString>("expected");

    const QDateTime tsWithOffset
        = QDateTime::fromString(QStringLiteral("2026-09-25T14:03:12.345+08:00"), Qt::ISODateWithMs);
    const QDateTime tsUtc
        = QDateTime::fromString(QStringLiteral("2026-09-25T06:03:12.345Z"), Qt::ISODateWithMs);

    QTest::newRow("debug_with_file")
        << static_cast<int>(QtDebugMsg) << QStringLiteral("Logging.cpp") << 42
        << QStringLiteral("linernotes.core") << QStringLiteral("debug message") << tsWithOffset
        << QStringLiteral("2026-09-25T14:03:12.345+08:00 [D] linernotes.core: debug message  "
                          "(Logging.cpp:42)");

    QTest::newRow("info_with_fullpath_file")
        << static_cast<int>(QtInfoMsg)
        << QStringLiteral("/home/user/code/Linernotes/src/core/Version.cpp") << 10
        << QStringLiteral("linernotes.core") << QStringLiteral("application started")
        << tsWithOffset
        << QStringLiteral("2026-09-25T14:03:12.345+08:00 [I] linernotes.core: application started  "
                          "(Version.cpp:10)");

    QTest::newRow("warning_without_file")
        << static_cast<int>(QtWarningMsg) << QString() << 0 << QStringLiteral("linernotes.core")
        << QStringLiteral("disk space low") << tsWithOffset
        << QStringLiteral("2026-09-25T14:03:12.345+08:00 [W] linernotes.core: disk space low");

    QTest::newRow("critical_without_file")
        << static_cast<int>(QtCriticalMsg) << QString() << 0 << QStringLiteral("linernotes.core")
        << QStringLiteral("database corruption") << tsWithOffset
        << QStringLiteral("2026-09-25T14:03:12.345+08:00 [C] linernotes.core: database corruption");

    QTest::newRow("fatal_with_file")
        << static_cast<int>(QtFatalMsg) << QStringLiteral("main.cpp") << 99
        << QStringLiteral("linernotes.core") << QStringLiteral("unrecoverable crash")
        << tsWithOffset
        << QStringLiteral("2026-09-25T14:03:12.345+08:00 [F] linernotes.core: unrecoverable crash  "
                          "(main.cpp:99)");

    QTest::newRow("custom_category_utc_timestamp")
        << static_cast<int>(QtInfoMsg) << QStringLiteral("tst_Logging.cpp") << 123
        << QStringLiteral("linernotes.library") << QStringLiteral("scanned 100 tracks") << tsUtc
        << QStringLiteral("2026-09-25T06:03:12.345Z [I] linernotes.library: scanned 100 tracks  "
                          "(tst_Logging.cpp:123)");
}

void TstLogging::formatLogLineDataDriven()
{
    QFETCH(int, msgType);
    QFETCH(QString, file);
    QFETCH(int, line);
    QFETCH(QString, category);
    QFETCH(QString, message);
    QFETCH(QDateTime, timestamp);
    QFETCH(QString, expected);

    const QByteArray fileUtf8 = file.toUtf8();
    const QByteArray categoryUtf8 = category.toUtf8();
    const char *filePtr = file.isEmpty() ? nullptr : fileUtf8.constData();
    const char *categoryPtr = category.isEmpty() ? nullptr : categoryUtf8.constData();

    const QMessageLogContext context(filePtr, line, nullptr, categoryPtr);
    const QString result = linernotes::core::formatLogLine(
        static_cast<QtMsgType>(msgType), context, message, timestamp);
    QCOMPARE(result, expected);
}

void TstLogging::writesLogsToFile()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    linernotes::core::LogConfig config;
    config.directory = tempDir.path();
    config.writeToConsole = false;
    config.minimumLevel = QtDebugMsg;

    linernotes::core::installLogging(config);

    qCDebug(linernotes::core::lcCore) << "First test debug message";
    qCInfo(linernotes::core::lcCore) << "Second test info message";
    qCWarning(linernotes::core::lcCore) << "Third test warning message";

    linernotes::core::uninstallLogging();

    const QString logFilePath = QDir(tempDir.path()).filePath(QStringLiteral("linernotes.log"));
    QVERIFY(QFile::exists(logFilePath));

    QFile file(logFilePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));

    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split(u'\n', Qt::SkipEmptyParts);

    QCOMPARE(lines.size(), 3);
    QVERIFY(lines.at(0).contains(QStringLiteral("[D] linernotes.core: First test debug message")));
    QVERIFY(lines.at(1).contains(QStringLiteral("[I] linernotes.core: Second test info message")));
    QVERIFY(
        lines.at(2).contains(QStringLiteral("[W] linernotes.core: Third test warning message")));
}

void TstLogging::fileRollingRespectsLimits()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    linernotes::core::LogConfig config;
    config.directory = tempDir.path();
    config.baseName = QStringLiteral("linernotes");
    config.maxFileBytes = 200;
    config.maxBackupFiles = 2;
    config.writeToConsole = false;

    linernotes::core::installLogging(config);

    for (int i = 0; i < 40; ++i) {
        qCInfo(linernotes::core::lcCore,
            "Log record %03d with sufficient payload bytes for rolling", i);
    }

    linernotes::core::uninstallLogging();

    const QDir dir(tempDir.path());
    const QString log0 = dir.filePath(QStringLiteral("linernotes.log"));
    const QString log1 = dir.filePath(QStringLiteral("linernotes.1.log"));
    const QString log2 = dir.filePath(QStringLiteral("linernotes.2.log"));
    const QString log3 = dir.filePath(QStringLiteral("linernotes.3.log"));
    const QString log4 = dir.filePath(QStringLiteral("linernotes.4.log"));

    QVERIFY(QFile::exists(log0));
    QVERIFY(QFile::exists(log1));
    QVERIFY(QFile::exists(log2));
    QVERIFY(!QFile::exists(log3));
    QVERIFY(!QFile::exists(log4));
}

void TstLogging::minimumLevelFiltersOutLowerLevels()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    linernotes::core::LogConfig config;
    config.directory = tempDir.path();
    config.minimumLevel = QtWarningMsg;
    config.writeToConsole = false;

    linernotes::core::installLogging(config);

    qCDebug(linernotes::core::lcCore) << "Filtered debug payload";
    qCInfo(linernotes::core::lcCore) << "Filtered info payload";
    qCWarning(linernotes::core::lcCore) << "Accepted warning payload";
    qCCritical(linernotes::core::lcCore) << "Accepted critical payload";

    linernotes::core::uninstallLogging();

    const QString logFilePath = QDir(tempDir.path()).filePath(QStringLiteral("linernotes.log"));
    QVERIFY(QFile::exists(logFilePath));

    QFile file(logFilePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));

    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(!content.contains(QStringLiteral("Filtered debug payload")));
    QVERIFY(!content.contains(QStringLiteral("Filtered info payload")));
    QVERIFY(content.contains(QStringLiteral("Accepted warning payload")));
    QVERIFY(content.contains(QStringLiteral("Accepted critical payload")));

    const QStringList lines = content.split(u'\n', Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);
}

void TstLogging::categoryFilterPreservesDisabledDebug()
{
    // lcTestOff is declared with QtInfoMsg default level, so debug is false by default.
    QVERIFY(!lcTestOff().isDebugEnabled());
    QVERIFY(lcTestOff().isInfoEnabled());

    linernotes::core::LogConfig config;
    config.writeToConsole = false;
    config.minimumLevel = QtDebugMsg;

    linernotes::core::installLogging(config);

    // Even when minimumLevel is QtDebugMsg, categoryFilter should not enable debug for lcTestOff
    QVERIFY(!lcTestOff().isDebugEnabled());
    QVERIFY(lcTestOff().isInfoEnabled());

    linernotes::core::uninstallLogging();

    QVERIFY(!lcTestOff().isDebugEnabled());
    QVERIFY(lcTestOff().isInfoEnabled());
}

void TstLogging::categoryFilterPreservesQtLoggingRules()
{
    QLoggingCategory::setFilterRules(
        QStringLiteral("linernotes.test.rules.debug=false\nlinernotes.test.rules.info=true"));
    QVERIFY(!lcTestRules().isDebugEnabled());

    linernotes::core::LogConfig config;
    config.writeToConsole = false;
    config.minimumLevel = QtDebugMsg;

    linernotes::core::installLogging(config);

    // Filter rules must still take effect
    QVERIFY(!lcTestRules().isDebugEnabled());

    linernotes::core::uninstallLogging();
    QLoggingCategory::setFilterRules(QString());
}

void TstLogging::minimumLevelDisablesLevelsAndRestoresOnUninstall()
{
    // lcCore has info enabled by default
    QVERIFY(linernotes::core::lcCore().isInfoEnabled());

    linernotes::core::LogConfig config;
    config.writeToConsole = false;
    config.minimumLevel = QtWarningMsg;

    linernotes::core::installLogging(config);

    // minimumLevel = Warning disables Debug and Info
    QVERIFY(!linernotes::core::lcCore().isDebugEnabled());
    QVERIFY(!linernotes::core::lcCore().isInfoEnabled());
    QVERIFY(linernotes::core::lcCore().isWarningEnabled());
    QVERIFY(linernotes::core::lcCore().isCriticalEnabled());

    linernotes::core::uninstallLogging();

    // After uninstallation, Qt default behavior is restored
    QVERIFY(linernotes::core::lcCore().isInfoEnabled());
}

void TstLogging::reconfigureMinimumLevelUpdatesCategories()
{
    linernotes::core::LogConfig configWarning;
    configWarning.writeToConsole = false;
    configWarning.minimumLevel = QtWarningMsg;

    linernotes::core::installLogging(configWarning);
    QVERIFY(!linernotes::core::lcCore().isInfoEnabled());

    linernotes::core::LogConfig configInfo;
    configInfo.writeToConsole = false;
    configInfo.minimumLevel = QtInfoMsg;

    linernotes::core::installLogging(configInfo);
    QVERIFY(linernotes::core::lcCore().isInfoEnabled());

    linernotes::core::uninstallLogging();
}

void TstLogging::multiThreadedLoggingIntegrity()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    linernotes::core::LogConfig config;
    config.directory = tempDir.path();
    config.maxFileBytes = qint64 { 10 } * 1024 * 1024;
    config.writeToConsole = false;

    linernotes::core::installLogging(config);

    constexpr int kThreadCount = 4;
    constexpr int kLinesPerThread = 500;

    std::vector<std::jthread> workerThreads;
    workerThreads.reserve(kThreadCount);

    for (int t = 0; t < kThreadCount; ++t) {
        workerThreads.emplace_back([t]() {
            for (int i = 0; i < kLinesPerThread; ++i) {
                qCInfo(linernotes::core::lcCore, "Thread-%d line-%04d payload", t, i);
            }
        });
    }

    workerThreads.clear();

    linernotes::core::uninstallLogging();

    const QDir dir(tempDir.path());
    const QStringList logFiles
        = dir.entryList(QStringList() << QStringLiteral("linernotes*.log"), QDir::Files);

    int totalLines = 0;
    const QRegularExpression lineRegex(QStringLiteral(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}.*\[I\] linernotes\.core: Thread-\d line-\d{4} payload)"));

    for (const QString &fileName : logFiles) {
        QFile file(dir.filePath(fileName));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        while (!file.atEnd()) {
            const QByteArray rawLine = file.readLine();
            const QString lineStr = QString::fromUtf8(rawLine).trimmed();
            if (lineStr.isEmpty()) {
                continue;
            }
            totalLines++;
            QVERIFY2(lineRegex.match(lineStr).hasMatch(),
                qPrintable(QStringLiteral("Line corrupted: %1").arg(lineStr)));
        }
    }

    QCOMPARE(totalLines, kThreadCount * kLinesPerThread);
}

void TstLogging::readOnlyDirectoryDoesNotCrash()
{
#ifdef Q_OS_UNIX
    if (::geteuid() == 0) {
        QSKIP("Running as root; filesystem permissions test skipped.");
    }
#endif

    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString readOnlyPath = QDir(tempDir.path()).filePath(QStringLiteral("readonly_subdir"));
    QDir().mkdir(readOnlyPath);

    const QFileDevice::Permissions readOnlyPerms = QFileDevice::ReadOwner | QFileDevice::ReadUser
        | QFileDevice::ExeOwner | QFileDevice::ExeUser;
    QVERIFY(QFile::setPermissions(readOnlyPath, readOnlyPerms));

    // Test writing to a read-only directory
    linernotes::core::LogConfig config;
    config.directory = readOnlyPath;
    config.writeToConsole = false;

    linernotes::core::installLogging(config);
    qCInfo(linernotes::core::lcCore) << "Message directed to unwriteable directory";
    qCWarning(linernotes::core::lcCore) << "Warning directed to unwriteable directory";
    linernotes::core::uninstallLogging();

    // Restore permissions so QTemporaryDir cleanup succeeds
    const QFileDevice::Permissions restorePerms
        = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
    QFile::setPermissions(readOnlyPath, restorePerms);
}

void TstLogging::reconfigureLoggingUpdatesSink()
{
    const QTemporaryDir tempDir1;
    const QTemporaryDir tempDir2;
    QVERIFY(tempDir1.isValid());
    QVERIFY(tempDir2.isValid());

    linernotes::core::LogConfig config1;
    config1.directory = tempDir1.path();
    config1.writeToConsole = false;

    linernotes::core::installLogging(config1);
    qCInfo(linernotes::core::lcCore) << "Message in dir 1";

    linernotes::core::LogConfig config2;
    config2.directory = tempDir2.path();
    config2.writeToConsole = false;

    linernotes::core::installLogging(config2);
    qCInfo(linernotes::core::lcCore) << "Message in dir 2";

    linernotes::core::uninstallLogging();

    const QString file1 = QDir(tempDir1.path()).filePath(QStringLiteral("linernotes.log"));
    const QString file2 = QDir(tempDir2.path()).filePath(QStringLiteral("linernotes.log"));

    QVERIFY(QFile::exists(file1));
    QVERIFY(QFile::exists(file2));

    QFile f1(file1);
    QVERIFY(f1.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content1 = QString::fromUtf8(f1.readAll());
    QVERIFY(content1.contains(QStringLiteral("Message in dir 1")));
    QVERIFY(!content1.contains(QStringLiteral("Message in dir 2")));

    QFile f2(file2);
    QVERIFY(f2.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content2 = QString::fromUtf8(f2.readAll());
    QVERIFY(content2.contains(QStringLiteral("Message in dir 2")));
    QVERIFY(!content2.contains(QStringLiteral("Message in dir 1")));
}

} // namespace

QTEST_GUILESS_MAIN(TstLogging)

#include "tst_Logging.moc"
