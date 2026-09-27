// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

#include <ai/ChatTypes.h>
#include <ai/Errors.h>
#include <ai/LlmClient.h>
#include <ai/LlmReply.h>
#include <common/FakeLlmServer.h>
#include <common/TestSupport.h>

#include <memory>

namespace {

using linernotes::ai::ChatRequest;
using linernotes::ai::ChatResponse;
using linernotes::ai::LlmClient;
using linernotes::ai::LlmReply;
using linernotes::ai::makeMessage;
using linernotes::ai::Role;
using linernotes::ai::ServiceConfig;
using linernotes::test::FakeLlmServer;
using linernotes::test::fixturePath;
namespace errc = linernotes::ai::errc;

const char *const kSkPattern = R"(sk-[A-Za-z0-9_-]{16,})";
const char *const kBearerPattern = R"(bearer\s+\S)";
const char *const kAuthPattern = R"("authorization")";

void populateFixtureRows()
{
    QTest::addColumn<QString>("fileName");

    const QString dirPath = fixturePath(QStringLiteral("llm"));
    const QDir dir(dirPath);
    const QStringList files = dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name);

    if (files.isEmpty()) {
        QFAIL("No fixture files found in tests/fixtures/llm");
        return;
    }

    for (const QString &file : files) {
        QTest::newRow(qPrintable(file)) << file;
    }
}

Role parseRole(const QString &roleStr)
{
    if (roleStr == QStringLiteral("system")) {
        return Role::System;
    }
    if (roleStr == QStringLiteral("assistant")) {
        return Role::Assistant;
    }
    if (roleStr == QStringLiteral("tool")) {
        return Role::Tool;
    }
    return Role::User;
}

ChatRequest buildChatRequest(const QJsonObject &reqObj)
{
    ChatRequest req;
    const QJsonArray msgsArr = reqObj.value(QStringLiteral("messages")).toArray();
    for (const auto &mVal : msgsArr) {
        if (!mVal.isObject()) {
            continue;
        }
        const QJsonObject mObj = mVal.toObject();
        const Role role = parseRole(mObj.value(QStringLiteral("role")).toString());
        req.messages.append(makeMessage(role, mObj.value(QStringLiteral("content")).toString()));
    }

    if (req.messages.isEmpty()) {
        req.messages.append(makeMessage(Role::User, QStringLiteral("hi")));
    }
    return req;
}

void verifyReplayResult(int status, const LlmReply &reply)
{
    if (status >= 200 && status < 300) {
        QVERIFY(reply.result().ok());
        const ChatResponse &resp = reply.result().value();
        const bool hasContent = !resp.content.isEmpty();
        const bool hasToolCalls = !resp.toolCalls.isEmpty();
        QVERIFY(hasContent || hasToolCalls);
    } else {
        QVERIFY(!reply.result().ok());
        const QString &errCode = reply.result().error().code;
        if (status == 401 || status == 403) {
            QCOMPARE(errCode, errc::kAuth);
        } else if (status == 429) {
            QCOMPARE(errCode, errc::kRateLimited);
        } else {
            QCOMPARE(errCode, errc::kHttp);
        }
    }
}

class TstLlmFixtures : public QObject {
    Q_OBJECT

private slots:
    void noSecrets_data();
    void noSecrets();
    void replays_data();
    void replays();
};

void TstLlmFixtures::noSecrets_data()
{
    populateFixtureRows();
}

void TstLlmFixtures::noSecrets()
{
    QFETCH(QString, fileName);

    const QString filePath = fixturePath(QStringLiteral("llm/") + fileName);
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly));

    const QByteArray rawContent = file.readAll();
    const QString text = QString::fromUtf8(rawContent);

    const QRegularExpression rx1(QString::fromUtf8(kSkPattern));
    const QRegularExpression rx2(
        QString::fromUtf8(kBearerPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression rx3(
        QString::fromUtf8(kAuthPattern), QRegularExpression::CaseInsensitiveOption);

    QVERIFY(!rx1.match(text).hasMatch());
    QVERIFY(!rx2.match(text).hasMatch());
    QVERIFY(!rx3.match(text).hasMatch());

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(rawContent, &parseErr);
    QCOMPARE(parseErr.error, QJsonParseError::NoError);
    QVERIFY(doc.isObject());

    const QJsonObject root = doc.object();
    QVERIFY(root.contains(QStringLiteral("request")));
    const QJsonObject reqObj = root.value(QStringLiteral("request")).toObject();

    QVERIFY(!reqObj.contains(QStringLiteral("authorization")));
    QVERIFY(!reqObj.contains(QStringLiteral("Authorization")));
    QVERIFY(!reqObj.contains(QStringLiteral("api_key")));
    QVERIFY(!reqObj.contains(QStringLiteral("apiKey")));
}

void TstLlmFixtures::replays_data()
{
    populateFixtureRows();
}

void TstLlmFixtures::replays()
{
    QFETCH(QString, fileName);

    const FakeLlmServer::Fixture fixture = FakeLlmServer::loadFixture(fileName);

    FakeLlmServer server;
    server.enqueue(fixture.response);

    QNetworkAccessManager nam;
    LlmClient client(nam);

    const QString model = fixture.request.value(QStringLiteral("model")).toString();
    const ServiceConfig config {
        .baseUrl = server.baseUrl(),
        .apiKey = QString(),
        .model = model.isEmpty() ? QStringLiteral("test-model") : model,
        .timeoutMs = 5000,
    };

    const ChatRequest req = buildChatRequest(fixture.request);
    const bool isStream = fixture.request.value(QStringLiteral("stream")).toBool(false);

    std::unique_ptr<LlmReply> reply;
    if (isStream) {
        reply = client.stream(config, req);
    } else {
        reply = client.complete(config, req);
    }

    QSignalSpy spy(reply.get(), &LlmReply::finished);
    QVERIFY(spy.wait(2000));
    QVERIFY(reply->isFinished());

    const int status = fixture.response.status;
    QCOMPARE(reply->httpStatus(), status);

    verifyReplayResult(status, *reply);
}

} // namespace

QTEST_GUILESS_MAIN(TstLlmFixtures)

#include "tst_LlmFixtures.moc"
