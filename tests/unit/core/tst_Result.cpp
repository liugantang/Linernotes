// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QString>
#include <QTest>

#include <core/Result.h>

#include <memory>

namespace {

using linernotes::core::Error;
using linernotes::core::Result;

class TstResult : public QObject {
    Q_OBJECT

private slots:
    void valueConstructionAndOk();
    void errorConstructionAndError();
    void moveOnlyValueExtraction();
    void resultVoidOkAndError();
    void errorToStringWithAndWithoutDetail();
};

void TstResult::valueConstructionAndOk()
{
    const Result<int> res(42);
    QVERIFY(res.ok());
    QVERIFY(static_cast<bool>(res));
    QCOMPARE(res.value(), 42);

    const Result<QString> strRes(QStringLiteral("hello"));
    QVERIFY(strRes.ok());
    QCOMPARE(strRes.value(), QStringLiteral("hello"));
}

void TstResult::errorConstructionAndError()
{
    const Error err {
        .code = QStringLiteral("db.open"),
        .message = QStringLiteral("Failed to open"),
        .detail = QStringLiteral("/tmp/music.db"),
    };
    const Result<int> res(err);
    QVERIFY(!res.ok());
    QVERIFY(!static_cast<bool>(res));
    QCOMPARE(res.error().code, QStringLiteral("db.open"));
    QCOMPARE(res.error().message, QStringLiteral("Failed to open"));
    QCOMPARE(res.error().detail, QStringLiteral("/tmp/music.db"));
}

void TstResult::moveOnlyValueExtraction()
{
    auto ptr = std::make_unique<int>(123);
    Result<std::unique_ptr<int>> res(std::move(ptr));
    QVERIFY(res.ok());

    std::unique_ptr<int> moved = std::move(res).value();
    QVERIFY(moved != nullptr);
    QCOMPARE(*moved, 123);
}

void TstResult::resultVoidOkAndError()
{
    const Result<void> okRes;
    QVERIFY(okRes.ok());
    QVERIFY(static_cast<bool>(okRes));

    const Error err {
        .code = QStringLiteral("db.migration"),
        .message = QStringLiteral("Migration failed"),
        .detail = QStringLiteral("0001_init"),
    };
    const Result<void> errRes(err);
    QVERIFY(!errRes.ok());
    QVERIFY(!static_cast<bool>(errRes));
    QCOMPARE(errRes.error().code, QStringLiteral("db.migration"));
    QCOMPARE(errRes.error().message, QStringLiteral("Migration failed"));
    QCOMPARE(errRes.error().detail, QStringLiteral("0001_init"));
}

void TstResult::errorToStringWithAndWithoutDetail()
{
    const Error errNoDetail {
        .code = QStringLiteral("db.open"),
        .message = QStringLiteral("File not found"),
        .detail = QString(),
    };
    QCOMPARE(errNoDetail.toString(), QStringLiteral("db.open: File not found"));

    const Error errWithDetail {
        .code = QStringLiteral("db.open"),
        .message = QStringLiteral("File not found"),
        .detail = QStringLiteral("/path/to/missing.db"),
    };
    QCOMPARE(
        errWithDetail.toString(), QStringLiteral("db.open: File not found (/path/to/missing.db)"));
}

} // namespace

QTEST_GUILESS_MAIN(TstResult)

#include "tst_Result.moc"
