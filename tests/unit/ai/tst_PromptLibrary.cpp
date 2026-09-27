// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QFile>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <ai/ChatTypes.h>
#include <ai/Errors.h>
#include <ai/PromptLibrary.h>
#include <core/Result.h>

using linernotes::ai::PromptLibrary;
using linernotes::ai::RenderedPrompt;
using linernotes::ai::Role;
using linernotes::ai::toMessages;
namespace errc = linernotes::ai::errc;

namespace {

void writePromptFile(const QString &dirPath, const QString &relativePath, const QString &content)
{
    const QString fullPath = QDir(dirPath).filePath(relativePath);
    const QFileInfo fileInfo(fullPath);
    QDir().mkpath(fileInfo.dir().path());

    QFile file(fullPath);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Text);
    Q_ASSERT(opened);
    Q_UNUSED(opened);
    file.write(content.toUtf8());
    file.close();
}

class TstPromptLibrary : public QObject {
    Q_OBJECT

private slots:
    void parseAndRender();
    void missingVariablesReported();
    void invalidTemplates_data();
    void invalidTemplates();
    void userDirOverridesBuiltin();
    void rejectsBadId();
    void builtinPingLoads();
};

void TstPromptLibrary::parseAndRender()
{
    const char *templateText = R"(---
version: 2
description: Clean up metadata
---
=== system ===
You are a helpful music metadata assistant.

=== user ===
Title: {{ title }}, Artist: {{artist}}
)";

    const auto parseRes = PromptLibrary::parse(
        QStringLiteral("cleanup/normalize"), QString::fromUtf8(templateText));
    QVERIFY(parseRes.ok());
    if (!parseRes.ok()) {
        return;
    }
    const auto &tmpl = parseRes.value();
    QCOMPARE(tmpl.id, QStringLiteral("cleanup/normalize"));
    QCOMPARE(tmpl.version, 2);
    QCOMPARE(tmpl.description, QStringLiteral("Clean up metadata"));
    QCOMPARE(tmpl.system, QStringLiteral("You are a helpful music metadata assistant."));
    QCOMPARE(tmpl.user, QStringLiteral("Title: {{ title }}, Artist: {{artist}}"));

    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("title"), QStringLiteral("Song with {{nested}}"));
    vars.insert(QStringLiteral("artist"), QStringLiteral("ArtistName"));
    vars.insert(QStringLiteral("extra_unused"), QStringLiteral("IgnoredValue"));

    const auto renderRes = PromptLibrary::renderTemplate(tmpl, vars);
    QVERIFY(renderRes.ok());
    if (!renderRes.ok()) {
        return;
    }
    const auto &rendered = renderRes.value();
    QCOMPARE(rendered.id, QStringLiteral("cleanup/normalize"));
    QCOMPARE(rendered.version, 2);
    QCOMPARE(rendered.system, QStringLiteral("You are a helpful music metadata assistant."));
    QCOMPARE(rendered.user, QStringLiteral("Title: Song with {{nested}}, Artist: ArtistName"));

    // toMessages with system and user
    const auto messages = toMessages(rendered);
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).role, Role::System);
    QCOMPARE(messages.at(0).content, QStringLiteral("You are a helpful music metadata assistant."));
    QCOMPARE(messages.at(1).role, Role::User);
    QCOMPARE(
        messages.at(1).content, QStringLiteral("Title: Song with {{nested}}, Artist: ArtistName"));

    // toMessages without system
    RenderedPrompt renderedNoSystem;
    renderedNoSystem.id = QStringLiteral("common/ping");
    renderedNoSystem.version = 1;
    renderedNoSystem.user = QStringLiteral("Ping user prompt");
    const auto singleMessageList = toMessages(renderedNoSystem);
    QCOMPARE(singleMessageList.size(), 1);
    QCOMPARE(singleMessageList.at(0).role, Role::User);
    QCOMPARE(singleMessageList.at(0).content, QStringLiteral("Ping user prompt"));
}

void TstPromptLibrary::missingVariablesReported()
{
    const char *templateText = R"(---
version: 1
---
=== system ===
System prompt with {{system_var}}
=== user ===
Track: {{title}}, Artist: {{artist}}, Album: {{album}}
)";

    const auto parseRes
        = PromptLibrary::parse(QStringLiteral("meta/track"), QString::fromUtf8(templateText));
    QVERIFY(parseRes.ok());
    if (!parseRes.ok()) {
        return;
    }

    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("system_var"), QStringLiteral("sys"));
    vars.insert(QStringLiteral("album"), QStringLiteral("My Album"));
    // title and artist are missing

    const auto renderRes = PromptLibrary::renderTemplate(parseRes.value(), vars);
    QVERIFY(!renderRes.ok());
    QCOMPARE(renderRes.error().code, QString(errc::kPromptInvalid));
    QVERIFY(renderRes.error().detail.contains(QStringLiteral("title")));
    QVERIFY(renderRes.error().detail.contains(QStringLiteral("artist")));
}

void TstPromptLibrary::invalidTemplates_data()
{
    QTest::addColumn<QString>("text");

    const char *missingVersion = R"(---
description: No version key
---
=== user ===
Hello
)";
    QTest::newRow("missing_version") << QString::fromUtf8(missingVersion);

    const char *nonPositiveVersion = R"(---
version: 0
---
=== user ===
Hello
)";
    QTest::newRow("version_zero") << QString::fromUtf8(nonPositiveVersion);

    const char *negativeVersion = R"(---
version: -5
---
=== user ===
Hello
)";
    QTest::newRow("negative_version") << QString::fromUtf8(negativeVersion);

    const char *missingUserSection = R"(---
version: 1
---
=== system ===
Only system section
)";
    QTest::newRow("missing_user_section") << QString::fromUtf8(missingUserSection);
}

void TstPromptLibrary::invalidTemplates()
{
    QFETCH(QString, text);
    const auto parseRes = PromptLibrary::parse(QStringLiteral("test/template"), text);
    QVERIFY(!parseRes.ok());
    QCOMPARE(parseRes.error().code, QString(errc::kPromptInvalid));
}

void TstPromptLibrary::userDirOverridesBuiltin()
{
    const QTemporaryDir userDir;
    QVERIFY(userDir.isValid());
    const QTemporaryDir builtinDir;
    QVERIFY(builtinDir.isValid());

    const QString userOverrideContent = QString::fromUtf8(R"(---
version: 1
---
=== user ===
User override content
)");

    const QString builtinNormalizeContent = QString::fromUtf8(R"(---
version: 1
---
=== user ===
Builtin normalize content
)");

    const QString builtinTagContent = QString::fromUtf8(R"(---
version: 1
---
=== user ===
Genre tag content
)");

    writePromptFile(userDir.path(), QStringLiteral("cleanup/normalize.md"), userOverrideContent);
    writePromptFile(
        builtinDir.path(), QStringLiteral("cleanup/normalize.md"), builtinNormalizeContent);
    writePromptFile(builtinDir.path(), QStringLiteral("tag/genre.md"), builtinTagContent);

    const PromptLibrary lib({ userDir.path(), builtinDir.path() });

    // 1. High priority directory override
    const auto overrideRes = lib.load(QStringLiteral("cleanup/normalize"));
    QVERIFY(overrideRes.ok());
    if (!overrideRes.ok()) {
        return;
    }
    const QString expectedOverridePath
        = QDir(userDir.path()).filePath(QStringLiteral("cleanup/normalize.md"));
    QCOMPARE(overrideRes.value().path, expectedOverridePath);
    QCOMPARE(overrideRes.value().user, QStringLiteral("User override content"));

    // 2. Fallback to lower priority directory
    const auto fallbackRes = lib.load(QStringLiteral("tag/genre"));
    QVERIFY(fallbackRes.ok());
    if (!fallbackRes.ok()) {
        return;
    }
    const QString expectedFallbackPath
        = QDir(builtinDir.path()).filePath(QStringLiteral("tag/genre.md"));
    QCOMPARE(fallbackRes.value().path, expectedFallbackPath);
    QCOMPARE(fallbackRes.value().user, QStringLiteral("Genre tag content"));

    // 3. Not found in either directory
    const auto missingRes = lib.load(QStringLiteral("unknown/template"));
    QVERIFY(!missingRes.ok());
    QCOMPARE(missingRes.error().code, QString(errc::kPromptNotFound));
}

void TstPromptLibrary::rejectsBadId()
{
    const char *validText = R"(---
version: 1
---
=== user ===
Hello
)";

    const QStringList badIds = {
        QStringLiteral("../secret"),
        QStringLiteral("a/b/c"),
        QStringLiteral("Cleanup/X"),
        QStringLiteral("simple"),
        QStringLiteral("/leading/slash"),
        QStringLiteral("trailing/slash/"),
    };

    const PromptLibrary lib({ QStringLiteral("/nonexistent") });

    for (const auto &badId : badIds) {
        const auto parseRes = PromptLibrary::parse(badId, QString::fromUtf8(validText));
        QVERIFY(!parseRes.ok());
        QCOMPARE(parseRes.error().code, QString(errc::kPromptInvalid));

        const auto loadRes = lib.load(badId);
        QVERIFY(!loadRes.ok());
        QCOMPARE(loadRes.error().code, QString(errc::kPromptInvalid));
    }
}

void TstPromptLibrary::builtinPingLoads()
{
    const PromptLibrary lib({ QStringLiteral(":/prompts") });
    const auto res = lib.load(QStringLiteral("common/ping"));
    QVERIFY(res.ok());
    if (!res.ok()) {
        return;
    }
    const auto &tmpl = res.value();
    QCOMPARE(tmpl.id, QStringLiteral("common/ping"));
    QCOMPARE(tmpl.version, 1);
    QCOMPARE(tmpl.user, QStringLiteral("请只回复 OK 两个字母。"));
    QVERIFY(tmpl.system.isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstPromptLibrary)

#include "tst_PromptLibrary.moc"
