// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>

#include <ai/SseParser.h>

using linernotes::ai::SseParser;

namespace {

class TstSseParser : public QObject {
    Q_OBJECT

private slots:
    void singleCompleteEvent();
    void splitAcrossChunks();
    void splitUtf8AcrossChunks();
    void commentsIgnored();
    void multilineDataJoinedWithNewline();
    void multipleEventsInSingleFeed();
    void noDataEventIgnored();
    void stripSingleLeadingSpace();
};

void TstSseParser::singleCompleteEvent()
{
    SseParser parser;
    const auto events = parser.feed("data: hello\n\n");
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).event, QString());
    QCOMPARE(events.at(0).data, QByteArray("hello"));
}

void TstSseParser::splitAcrossChunks()
{
    SseParser parser;
    QVERIFY(parser.feed("data: ").isEmpty());
    QVERIFY(parser.feed("hel").isEmpty());
    QVERIFY(parser.feed("lo\r").isEmpty());
    const auto events = parser.feed("\n\r\n");
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).event, QString());
    QCOMPARE(events.at(0).data, QByteArray("hello"));
}

void TstSseParser::splitUtf8AcrossChunks()
{
    SseParser parser;
    // "音" in UTF-8 is 0xE9 0x9F 0xB3
    const QByteArray byte1("\xe9", 1);
    const QByteArray byte23("\x9f\xb3", 2);

    QVERIFY(parser.feed(QByteArray("data: " + byte1)).isEmpty());
    const auto events = parser.feed(QByteArray(byte23 + "\n\n"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).data, QByteArray("\xe9\x9f\xb3"));
    QCOMPARE(QString::fromUtf8(events.at(0).data), QString::fromUtf8("\xe9\x9f\xb3"));
}

void TstSseParser::commentsIgnored()
{
    SseParser parser;
    const auto events
        = parser.feed(": this is a comment\n:another comment\ndata: payload\n:yet another\n\n");
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).data, QByteArray("payload"));
}

void TstSseParser::multilineDataJoinedWithNewline()
{
    SseParser parser;
    const auto events = parser.feed("data: line1\r\ndata: line2\r\ndata: line3\r\n\r\n");
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).data, QByteArray("line1\nline2\nline3"));
}

void TstSseParser::multipleEventsInSingleFeed()
{
    SseParser parser;
    const auto events
        = parser.feed("data: first\n\ndata: second\n\nevent: custom\ndata: third\n\n");
    QCOMPARE(events.size(), 3);
    QCOMPARE(events.at(0).event, QString());
    QCOMPARE(events.at(0).data, QByteArray("first"));
    QCOMPARE(events.at(1).event, QString());
    QCOMPARE(events.at(1).data, QByteArray("second"));
    QCOMPARE(events.at(2).event, QStringLiteral("custom"));
    QCOMPARE(events.at(2).data, QByteArray("third"));
}

void TstSseParser::noDataEventIgnored()
{
    SseParser parser;
    const auto events = parser.feed("event: ping\n\n");
    QCOMPARE(events.size(), 0);
}

void TstSseParser::stripSingleLeadingSpace()
{
    SseParser parser;
    // data: foo -> foo
    const auto e1 = parser.feed("data: foo\n\n");
    QCOMPARE(e1.size(), 1);
    QCOMPARE(e1.at(0).data, QByteArray("foo"));

    // data:foo -> foo
    const auto e2 = parser.feed("data:foo\n\n");
    QCOMPARE(e2.size(), 1);
    QCOMPARE(e2.at(0).data, QByteArray("foo"));

    // data:  foo ->  foo
    const auto e3 = parser.feed("data:  foo\n\n");
    QCOMPARE(e3.size(), 1);
    QCOMPARE(e3.at(0).data, QByteArray(" foo"));
}

} // namespace

QTEST_GUILESS_MAIN(TstSseParser)

#include "tst_SseParser.moc"
