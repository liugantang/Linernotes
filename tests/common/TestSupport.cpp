// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TestSupport.h"

#include <QDir>
#include <QProcessEnvironment>
#include <QString>
#include <QtLogging>

namespace linernotes::test {

QString fixturePath(const QString &relative)
{
    if (!qEnvironmentVariableIsSet("LINERNOTES_TEST_FIXTURES")) {
        qFatal(
            "LINERNOTES_TEST_FIXTURES environment variable is not set. Please ensure it points to "
            "the tests/fixtures directory.");
    }

    const QString baseDir = qEnvironmentVariable("LINERNOTES_TEST_FIXTURES");
    if (baseDir.isEmpty()) {
        qFatal("LINERNOTES_TEST_FIXTURES environment variable is empty. Please ensure it points to "
               "the tests/fixtures directory.");
    }

    return QDir::cleanPath(QDir(baseDir).filePath(relative));
}

} // namespace linernotes::test
