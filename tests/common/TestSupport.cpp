#include "TestSupport.h"

#include <QDir>
#include <QProcessEnvironment>
#include <QString>
#include <QtLogging>

namespace aimusic::test {

QString fixturePath(const QString &relative)
{
    if (!qEnvironmentVariableIsSet("AIMUSIC_TEST_FIXTURES")) {
        qFatal("AIMUSIC_TEST_FIXTURES environment variable is not set. Please ensure it points to "
               "the tests/fixtures directory.");
    }

    const QString baseDir = qEnvironmentVariable("AIMUSIC_TEST_FIXTURES");
    if (baseDir.isEmpty()) {
        qFatal("AIMUSIC_TEST_FIXTURES environment variable is empty. Please ensure it points to "
               "the tests/fixtures directory.");
    }

    return QDir::cleanPath(QDir(baseDir).filePath(relative));
}

} // namespace aimusic::test
