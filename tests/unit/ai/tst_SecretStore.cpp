// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QString>
#include <QTest>

#include <ai/SecretStore.h>

using linernotes::ai::MemorySecretStore;
using linernotes::core::Result;

namespace {

class TstSecretStore : public QObject {
    Q_OBJECT

private slots:
    void memoryStoreWriteReadRemove();
    void memoryStoreCallbackIsAsync();
};

void TstSecretStore::memoryStoreWriteReadRemove()
{
    MemorySecretStore store;

    // 1. Read non-existent account -> empty string
    bool readCalled = false;
    QString readValue;
    store.read(QStringLiteral("non_existent"), this, [&](const Result<QString> &res) {
        readCalled = true;
        QVERIFY(res.ok());
        if (res.ok()) {
            readValue = res.value();
        }
    });
    QTRY_COMPARE(readCalled, true);
    QCOMPARE(readValue, QString());

    // 2. Write secret
    bool writeCalled = false;
    store.write(
        QStringLiteral("srv1"), QStringLiteral("secret-123"), this, [&](const Result<void> &res) {
            writeCalled = true;
            QVERIFY(res.ok());
        });
    QTRY_COMPARE(writeCalled, true);

    // 3. Read back -> returns "secret-123"
    readCalled = false;
    readValue.clear();
    store.read(QStringLiteral("srv1"), this, [&](const Result<QString> &res) {
        readCalled = true;
        QVERIFY(res.ok());
        if (res.ok()) {
            readValue = res.value();
        }
    });
    QTRY_COMPARE(readCalled, true);
    QCOMPARE(readValue, QStringLiteral("secret-123"));

    // 4. Remove secret
    bool removeCalled = false;
    store.remove(QStringLiteral("srv1"), this, [&](const Result<void> &res) {
        removeCalled = true;
        QVERIFY(res.ok());
    });
    QTRY_COMPARE(removeCalled, true);

    // 5. Read after remove -> empty string
    readCalled = false;
    readValue.clear();
    store.read(QStringLiteral("srv1"), this, [&](const Result<QString> &res) {
        readCalled = true;
        QVERIFY(res.ok());
        if (res.ok()) {
            readValue = res.value();
        }
    });
    QTRY_COMPARE(readCalled, true);
    QCOMPARE(readValue, QString());
}

void TstSecretStore::memoryStoreCallbackIsAsync()
{
    MemorySecretStore store;

    bool called = false;
    store.write(QStringLiteral("srv1"), QStringLiteral("val"), this, [&](const Result<void> &res) {
        Q_UNUSED(res);
        called = true;
    });

    // Immediately after write() returns, callback must NOT have executed yet
    QCOMPARE(called, false);

    // When event loop runs, callback is executed
    QTRY_COMPARE(called, true);
}

} // namespace

QTEST_GUILESS_MAIN(TstSecretStore)

#include "tst_SecretStore.moc"
