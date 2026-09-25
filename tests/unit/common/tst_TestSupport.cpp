// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QByteArray>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QTest>

#include <common/TestSupport.h>

namespace {

class TstTestSupport : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void fixturePathJoinsRelativePath();
    void fixturePathWithEmptyRelativePath();

private:
    bool m_hadOriginalFixturesEnv = false;
    QByteArray m_originalFixturesEnv;
};

void TstTestSupport::init()
{
    m_hadOriginalFixturesEnv = qEnvironmentVariableIsSet("LINERNOTES_TEST_FIXTURES");
    if (m_hadOriginalFixturesEnv) {
        m_originalFixturesEnv = qgetenv("LINERNOTES_TEST_FIXTURES");
    }
}

void TstTestSupport::cleanup()
{
    if (m_hadOriginalFixturesEnv) {
        qputenv("LINERNOTES_TEST_FIXTURES", m_originalFixturesEnv);
    } else {
        qunsetenv("LINERNOTES_TEST_FIXTURES");
    }
}

void TstTestSupport::fixturePathJoinsRelativePath()
{
    qputenv("LINERNOTES_TEST_FIXTURES", "/tmp/mock_fixtures");

    const QString result = linernotes::test::fixturePath(QStringLiteral("audio/sample.mp3"));
    QCOMPARE(result, QStringLiteral("/tmp/mock_fixtures/audio/sample.mp3"));

    const QString nested = linernotes::test::fixturePath(QStringLiteral("nested/dir/file.json"));
    QCOMPARE(nested, QStringLiteral("/tmp/mock_fixtures/nested/dir/file.json"));
}

void TstTestSupport::fixturePathWithEmptyRelativePath()
{
    qputenv("LINERNOTES_TEST_FIXTURES", "/tmp/mock_fixtures");

    const QString result = linernotes::test::fixturePath(QString());
    QCOMPARE(result, QStringLiteral("/tmp/mock_fixtures"));
}

} // namespace

QTEST_GUILESS_MAIN(TstTestSupport)

#include "tst_TestSupport.moc"
