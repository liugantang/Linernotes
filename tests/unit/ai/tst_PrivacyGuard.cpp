// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <ai/AiEnums.h>
#include <ai/PrivacyGuard.h>
#include <core/Settings.h>

using linernotes::ai::DataCategory;
using linernotes::ai::PrivacyGuard;
using linernotes::core::Settings;

namespace {

class TstPrivacyGuard : public QObject {
    Q_OBJECT

private slots:
    void defaultsAndPersist();
    void isLocalService_data();
    void isLocalService();
    void blockedOnlyForCloud();
};

void TstPrivacyGuard::defaultsAndPersist()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));

    {
        Settings settings(iniPath);
        PrivacyGuard guard(settings);

        // 1. Defaults check: play_history = true, moments = false, location = false
        QCOMPARE(guard.isAllowed(DataCategory::PlayHistory), true);
        QCOMPARE(guard.isAllowed(DataCategory::Moments), false);
        QCOMPARE(guard.isAllowed(DataCategory::Location), false);

        QSignalSpy spy(&guard, &PrivacyGuard::changed);

        // 2. Setting same value should NOT emit changed
        guard.setAllowed(DataCategory::Moments, false);
        QCOMPARE(spy.count(), 0);

        // 3. Changing value should emit changed once
        guard.setAllowed(DataCategory::Moments, true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(guard.isAllowed(DataCategory::Moments), true);

        // Setting same value again should NOT emit changed
        guard.setAllowed(DataCategory::Moments, true);
        QCOMPARE(spy.count(), 1);

        // Change location
        guard.setAllowed(DataCategory::Location, true);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(guard.isAllowed(DataCategory::Location), true);

        // Change play_history
        guard.setAllowed(DataCategory::PlayHistory, false);
        QCOMPARE(spy.count(), 3);
        QCOMPARE(guard.isAllowed(DataCategory::PlayHistory), false);
    }

    // 4. Persistence check: Re-instantiate Settings & PrivacyGuard from same ini
    {
        Settings settings(iniPath);
        PrivacyGuard guard(settings);

        QCOMPARE(guard.isAllowed(DataCategory::PlayHistory), false);
        QCOMPARE(guard.isAllowed(DataCategory::Moments), true);
        QCOMPARE(guard.isAllowed(DataCategory::Location), true);
    }
}

void TstPrivacyGuard::isLocalService_data()
{
    QTest::addColumn<QUrl>("url");
    QTest::addColumn<bool>("expected");

    QTest::newRow("localhost") << QUrl(QStringLiteral("http://localhost:11434")) << true;
    QTest::newRow("127.0.0.1") << QUrl(QStringLiteral("http://127.0.0.1:8080")) << true;
    QTest::newRow("ipv6 loopback") << QUrl(QStringLiteral("http://[::1]:8080")) << true;
    QTest::newRow("192.168.1.20") << QUrl(QStringLiteral("http://192.168.1.20:11434")) << true;
    QTest::newRow("10.0.0.5") << QUrl(QStringLiteral("http://10.0.0.5")) << true;
    QTest::newRow("openai") << QUrl(QStringLiteral("https://api.openai.com")) << false;
    QTest::newRow("deepseek") << QUrl(QStringLiteral("https://api.deepseek.com")) << false;
    QTest::newRow("public ip") << QUrl(QStringLiteral("http://8.8.8.8")) << false;
}

void TstPrivacyGuard::isLocalService()
{
    QFETCH(QUrl, url);
    QFETCH(bool, expected);

    QCOMPARE(PrivacyGuard::isLocalService(url), expected);
}

void TstPrivacyGuard::blockedOnlyForCloud()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));

    Settings settings(iniPath);
    PrivacyGuard guard(settings);

    // Default: PlayHistory = true, Moments = false, Location = false
    const QUrl cloudUrl(QStringLiteral("https://api.openai.com/v1"));
    const QUrl localUrl(QStringLiteral("http://127.0.0.1:8080"));
    const QUrl localhostUrl(QStringLiteral("http://localhost:11434"));

    const QSet<DataCategory> categories = { DataCategory::Moments, DataCategory::PlayHistory };

    // Cloud URL: Moments is blocked, PlayHistory is allowed
    const auto cloudBlocked = guard.blocked(categories, cloudUrl);
    QCOMPARE(cloudBlocked.size(), 1);
    QCOMPARE(cloudBlocked.at(0), DataCategory::Moments);

    // Local URL: nothing is blocked
    const auto localBlocked = guard.blocked(categories, localUrl);
    QVERIFY(localBlocked.isEmpty());

    const auto localhostBlocked = guard.blocked(categories, localhostUrl);
    QVERIFY(localhostBlocked.isEmpty());

    // isAllowedFor check
    QCOMPARE(guard.isAllowedFor(DataCategory::Moments, cloudUrl), false);
    QCOMPARE(guard.isAllowedFor(DataCategory::PlayHistory, cloudUrl), true);
    QCOMPARE(guard.isAllowedFor(DataCategory::Moments, localUrl), true);
    QCOMPARE(guard.isAllowedFor(DataCategory::PlayHistory, localUrl), true);
}

} // namespace

QTEST_GUILESS_MAIN(TstPrivacyGuard)

#include "tst_PrivacyGuard.moc"
