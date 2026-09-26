// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>

#include <library/Errors.h>
#include <library/LibraryQuery.h>
#include <library/SmartRule.h>

namespace {

using namespace linernotes::library;

class TstSmartRule : public QObject {
    Q_OBJECT

private slots:
    void roundtrip();
    void rejectInvalid_data();
    void rejectInvalid();
};

void TstSmartRule::roundtrip()
{
    SmartRule rule;
    rule.match = SmartRule::Match::All;
    rule.conditions = {
        SmartCondition {
            .field = SmartField::Title,
            .op = SmartOp::Contains,
            .value = QStringLiteral("Rock"),
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::Year,
            .op = SmartOp::Greater,
            .value = 2000,
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::Rating,
            .op = SmartOp::Between,
            .value = 3,
            .value2 = 5,
        },
        SmartCondition {
            .field = SmartField::Favorite,
            .op = SmartOp::IsTrue,
            .value = { },
            .value2 = { },
        },
        SmartCondition {
            .field = SmartField::DateAdded,
            .op = SmartOp::InLastDays,
            .value = 30,
            .value2 = { },
        },
    };
    rule.sortKey = TrackSortKey::Year;
    rule.sortOrder = Qt::DescendingOrder;
    rule.limit = 50;

    const QString json = rule.toJson();
    const auto res = SmartRule::fromJson(json);

    QVERIFY(res.ok());
    QCOMPARE(res.value(), rule);
}

void TstSmartRule::rejectInvalid_data()
{
    QTest::addColumn<QString>("json");

    QTest::newRow("malformed_json") << QStringLiteral("not a json string");
    QTest::newRow("root_array") << QStringLiteral("[]");
    QTest::newRow("wrong_version")
        << QStringLiteral(R"({"version": 2, "match": "all", "conditions": []})");
    QTest::newRow("invalid_match")
        << QStringLiteral(R"({"version": 1, "match": "maybe", "conditions": []})");
    QTest::newRow("unknown_field") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "unknown", "op": "contains", "value": "x"}]})");
    QTest::newRow("unknown_op") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "title", "op": "magic", "value": "x"}]})");
    QTest::newRow("field_op_mismatch") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "title", "op": "between", "value": 1, "value2": 2}]})");
    QTest::newRow("missing_value") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "title", "op": "contains"}]})");
    QTest::newRow("missing_between_value2") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [{"field": "year", "op": "between", "value": 2000}]})");
    QTest::newRow("limit_zero") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [], "limit": 0})");
    QTest::newRow("limit_negative")
        << QStringLiteral(R"({"version": 1, "match": "all", "conditions": [], "limit": -5})");
    QTest::newRow("unknown_sort_key") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [], "sortKey": "unknownKey"})");
    QTest::newRow("reject_playlist_order_sort_key") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [], "sortKey": "playlistOrder"})");
    QTest::newRow("invalid_sort_order") << QStringLiteral(
        R"({"version": 1, "match": "all", "conditions": [], "sortOrder": "sideways"})");
}

void TstSmartRule::rejectInvalid()
{
    QFETCH(QString, json);

    const auto res = SmartRule::fromJson(json);
    QVERIFY(!res.ok());
    QCOMPARE(res.error().code, QString(errc::kPlaylistRuleInvalid));
}

} // namespace

QTEST_APPLESS_MAIN(TstSmartRule)
#include "tst_SmartRule.moc"
