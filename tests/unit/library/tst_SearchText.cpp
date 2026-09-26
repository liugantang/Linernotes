// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTest>

#include <library/SearchText.h>

namespace {

using namespace linernotes::library::search;

class TstSearchText : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void normalizeTest_data();
    void normalizeTest();

    void indexTextTest_data();
    void indexTextTest();

    void romanizedTest_data();
    void romanizedTest();

    void buildMatchQuery_data();
    void buildMatchQuery();

private:
    QSqlDatabase m_memDb;
};

void TstSearchText::initTestCase()
{
    m_memDb = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), QStringLiteral("tst_search_text_db"));
    m_memDb.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(m_memDb.open());

    QSqlQuery q(m_memDb);
    QVERIFY(q.exec(QStringLiteral(
        "CREATE VIRTUAL TABLE t USING fts5(content, tokenize='unicode61 remove_diacritics 2');")));
}

void TstSearchText::cleanupTestCase()
{
    if (m_memDb.isOpen()) {
        m_memDb.close();
    }
}

void TstSearchText::normalizeTest_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("fullwidth-and-case") << QStringLiteral("ＡＢＣ１") << QStringLiteral("abc1");
    QTest::newRow("uppercase-latin")
        << QStringLiteral("HeLLo WoRLD") << QStringLiteral("hello world");
    QTest::newRow("chinese-characters") << QStringLiteral("林晓风") << QStringLiteral("林晓风");
    QTest::newRow("fullwidth-space-and-symbols")
        << QStringLiteral("Ｈｅｌｌｏ　１２３！") << QStringLiteral("hello 123!");
    QTest::newRow("empty") << QString() << QString();
}

void TstSearchText::normalizeTest()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);

    QCOMPARE(normalize(input), expected);
}

void TstSearchText::indexTextTest_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QStringList>("mustContain");
    QTest::addColumn<QStringList>("mustNotContain");

    QTest::newRow("cjk-spaced-and-simplified-variant")
        << QStringLiteral("林晓风")
        << QStringList { QStringLiteral("林 晓 风"), QStringLiteral("林 曉 風") }
        << QStringList { QStringLiteral("lin") };

    QTest::newRow("mixed-latin-cjk")
        << QStringLiteral("Hello世界") << QStringList { QStringLiteral("hello 世 界") }
        << QStringList { QStringLiteral("hello世界") };

    QTest::newRow("traditional-input-adds-simplified")
        << QStringLiteral("晚風裡的歌")
        << QStringList { QStringLiteral("晚 風 裡 的 歌"), QStringLiteral("晚 风 里 的 歌") }
        << QStringList { };

    QTest::newRow("pure-latin-no-extra-variants")
        << QStringLiteral("Rock & Roll 1999") << QStringList { QStringLiteral("rock & roll 1999") }
        << QStringList { QStringLiteral("Rock") };
}

void TstSearchText::indexTextTest()
{
    QFETCH(QString, input);
    QFETCH(QStringList, mustContain);
    QFETCH(QStringList, mustNotContain);

    const QString actual = indexText(input);
    for (const auto &item : mustContain) {
        QVERIFY2(actual.contains(item),
            qPrintable(QStringLiteral("Expected '%1' to contain '%2'").arg(actual, item)));
    }
    for (const auto &item : mustNotContain) {
        QVERIFY2(!actual.contains(item),
            qPrintable(QStringLiteral("Expected '%1' NOT to contain '%2'").arg(actual, item)));
    }
}

void TstSearchText::romanizedTest_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QStringList>("mustContain");
    QTest::addColumn<bool>("shouldBeEmpty");

    QTest::newRow("chinese-hanzi") << QStringLiteral("林晓风")
                                   << QStringList { QStringLiteral("lin xiao feng"),
                                          QStringLiteral("linxiaofeng"), QStringLiteral("lxf") }
                                   << false;

    QTest::newRow("japanese-kana")
        << QStringLiteral("さくら") << QStringList { QStringLiteral("sakura") } << false;

    QTest::newRow("pure-latin-returns-empty")
        << QStringLiteral("Hotel California") << QStringList { } << true;

    QTest::newRow("empty-returns-empty") << QString() << QStringList { } << true;
}

void TstSearchText::romanizedTest()
{
    QFETCH(QString, input);
    QFETCH(QStringList, mustContain);
    QFETCH(bool, shouldBeEmpty);

    const QString actual = romanized(input);
    if (shouldBeEmpty) {
        QVERIFY2(
            actual.isEmpty(), qPrintable(QStringLiteral("Expected empty, got: %1").arg(actual)));
    } else {
        QVERIFY(!actual.isEmpty());
        for (const auto &item : mustContain) {
            QVERIFY2(actual.contains(item),
                qPrintable(QStringLiteral("Expected '%1' to contain '%2'").arg(actual, item)));
        }
    }
}

void TstSearchText::buildMatchQuery_data()
{
    QTest::addColumn<QString>("userInput");
    QTest::addColumn<QString>("expectedExpr");

    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("spaces-only") << QStringLiteral("   \t\n  ") << QString();
    QTest::newRow("pure-punctuation") << QStringLiteral("- * : \" ^") << QString();

    QTest::newRow("single-latin-word-prefix")
        << QStringLiteral("Hello") << QStringLiteral("\"hello\"*");
    QTest::newRow("multiple-latin-words")
        << QStringLiteral("Jay Chou") << QStringLiteral("\"jay\" AND \"chou\"*");

    QTest::newRow("cjk-single-word") << QStringLiteral("林晓风") << QStringLiteral("\"林 晓 风\"");
    QTest::newRow("cjk-multiple-words")
        << QStringLiteral("林 晓 风") << QStringLiteral("\"林\" AND \"晓\" AND \"风\"");

    QTest::newRow("mixed-cjk-latin-prefix")
        << QStringLiteral("林晓风 live") << QStringLiteral("\"林 晓 风\" AND \"live\"*");
    QTest::newRow("mixed-word-inline")
        << QStringLiteral("Hello世界") << QStringLiteral("\"hello\" AND \"世 界\"");

    QTest::newRow("special-char-quotes")
        << QStringLiteral("\"林晓风\"") << QStringLiteral("\"林 晓 风\"");
    QTest::newRow("special-char-fts-keywords")
        << QStringLiteral("AND OR NOT") << QStringLiteral("\"and\" AND \"or\" AND \"not\"*");
    QTest::newRow("special-char-near")
        << QStringLiteral("NEAR(5)") << QStringLiteral("\"near\" AND \"5\"*");
    QTest::newRow("special-char-dash-colon") << QStringLiteral(
        "track:01 - live*") << QStringLiteral("\"track\" AND \"01\" AND \"live\"*");
}

void TstSearchText::buildMatchQuery()
{
    QFETCH(QString, userInput);
    QFETCH(QString, expectedExpr);

    const QString actual = linernotes::library::search::buildMatchQuery(userInput);
    QCOMPARE(actual, expectedExpr);

    // Verify on real FTS5 table: MUST NOT produce syntax error!
    if (!actual.isEmpty()) {
        QSqlQuery q(m_memDb);
        q.prepare(QStringLiteral("SELECT * FROM t WHERE t MATCH ?;"));
        q.bindValue(0, actual);
        QVERIFY2(q.exec(),
            qPrintable(QStringLiteral("FTS5 query failed for expr '%1': %2")
                    .arg(actual, q.lastError().text())));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstSearchText)

#include "tst_SearchText.moc"
