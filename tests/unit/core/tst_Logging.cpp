// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QtLogging>

#include <core/Logging.h>

namespace {

class TstLogging : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void writesLogsToFile();
    void fileRollingRespectsLimits();
    void minimumLevelFiltersOutLowerLevels();

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

} // namespace

QTEST_GUILESS_MAIN(TstLogging)

#include "tst_Logging.moc"
