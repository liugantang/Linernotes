// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <core/SingleInstance.h>

using linernotes::core::SingleInstance;

namespace {

class TstSingleInstance : public QObject {
    Q_OBJECT

private slots:
    void testSingleInstanceRolesAndCommunication();
    void testDifferentKeysBothPrimary();
};

void TstSingleInstance::testSingleInstanceRolesAndCommunication()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString key = tempDir.path();

    SingleInstance primary(key);
    const auto r1 = primary.start();
    QVERIFY(r1.ok());
    QCOMPARE(r1.value(), SingleInstance::Role::Primary);

    SingleInstance secondary(key);
    const auto r2 = secondary.start();
    QVERIFY(r2.ok());
    QCOMPARE(r2.value(), SingleInstance::Role::Secondary);

    QSignalSpy spy(&primary, &SingleInstance::messageReceived);
    const QStringList sentArgs = { QStringLiteral("a"), QStringLiteral("b c") };
    const auto sendRes = secondary.sendMessage(sentArgs);
    QVERIFY(sendRes.ok());

    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.count(), 1);
    const auto receivedArgs = spy.at(0).at(0).toStringList();
    QCOMPARE(receivedArgs, sentArgs);
}

void TstSingleInstance::testDifferentKeysBothPrimary()
{
    const QTemporaryDir tempDir1;
    QVERIFY(tempDir1.isValid());
    const QTemporaryDir tempDir2;
    QVERIFY(tempDir2.isValid());

    SingleInstance inst1(tempDir1.path());
    const auto r1 = inst1.start();
    QVERIFY(r1.ok());
    QCOMPARE(r1.value(), SingleInstance::Role::Primary);

    SingleInstance inst2(tempDir2.path());
    const auto r2 = inst2.start();
    QVERIFY(r2.ok());
    QCOMPARE(r2.value(), SingleInstance::Role::Primary);
}

} // namespace

QTEST_GUILESS_MAIN(TstSingleInstance)

#include "tst_SingleInstance.moc"
