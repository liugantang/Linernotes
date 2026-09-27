// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PromptLibrary.h"

#include "AiLogging.h"
#include "Errors.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace linernotes::ai {

namespace {

bool isValidPromptId(const QString &id)
{
    static const QRegularExpression s_promptIdRegex(
        QString::fromUtf8(R"(^[a-z0-9_-]+/[a-z0-9_-]+$)"));
    return s_promptIdRegex.match(id).hasMatch();
}

QString stripBlankLines(const QStringList &lines)
{
    qsizetype start = 0;
    while (start < lines.size() && lines.at(start).trimmed().isEmpty()) {
        ++start;
    }
    qsizetype end = lines.size() - 1;
    while (end >= start && lines.at(end).trimmed().isEmpty()) {
        --end;
    }
    if (start > end) {
        return { };
    }
    QStringList result;
    result.reserve(end - start + 1);
    for (qsizetype i = start; i <= end; ++i) {
        result.append(lines.at(i));
    }
    return result.join(QLatin1Char('\n'));
}

QStringList splitLines(const QString &text)
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return normalized.split(QLatin1Char('\n'));
}

struct HeaderParseResult {
    int version = 0;
    QString description;
    qsizetype bodyStartIndex = -1;
};

core::Result<HeaderParseResult> parseHeader(const QString &id, const QStringList &lines)
{
    qsizetype lineIndex = 0;
    while (lineIndex < lines.size() && lines.at(lineIndex).trimmed().isEmpty()) {
        ++lineIndex;
    }
    if (lineIndex >= lines.size() || lines.at(lineIndex).trimmed() != QStringLiteral("---")) {
        return core::Error {
            .code = errc::kPromptInvalid,
            .message = QStringLiteral("Missing frontmatter opening delimiter"),
            .detail = id,
        };
    }
    ++lineIndex;

    qsizetype headerEndIndex = -1;
    for (qsizetype i = lineIndex; i < lines.size(); ++i) {
        if (lines.at(i).trimmed() == QStringLiteral("---")) {
            headerEndIndex = i;
            break;
        }
    }
    if (headerEndIndex == -1) {
        return core::Error {
            .code = errc::kPromptInvalid,
            .message = QStringLiteral("Missing frontmatter closing delimiter"),
            .detail = id,
        };
    }

    int version = 0;
    QString description;
    bool hasVersion = false;

    for (qsizetype i = lineIndex; i < headerEndIndex; ++i) {
        const QString &line = lines.at(i);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const qsizetype colonIdx = trimmed.indexOf(QLatin1Char(':'));
        if (colonIdx <= 0) {
            continue;
        }
        const QString key = trimmed.left(colonIdx).trimmed();
        const QString val = trimmed.mid(colonIdx + 1).trimmed();
        if (key == QStringLiteral("version")) {
            bool ok = false;
            const int parsedVer = val.toInt(&ok);
            if (ok && parsedVer > 0) {
                version = parsedVer;
                hasVersion = true;
            } else {
                return core::Error {
                    .code = errc::kPromptInvalid,
                    .message
                    = QStringLiteral("Invalid version in frontmatter (must be a positive integer)"),
                    .detail = QStringLiteral("id: %1, version: %2").arg(id, val),
                };
            }
        } else if (key == QStringLiteral("description")) {
            description = val;
        }
    }

    if (!hasVersion) {
        return core::Error {
            .code = errc::kPromptInvalid,
            .message = QStringLiteral("Missing required version in frontmatter"),
            .detail = id,
        };
    }

    HeaderParseResult result;
    result.version = version;
    result.description = description;
    result.bodyStartIndex = headerEndIndex + 1;
    return result;
}

struct BodyParseResult {
    QString system;
    QString user;
};

core::Result<BodyParseResult> parseBody(
    const QString &id, const QStringList &lines, qsizetype startIndex)
{
    enum class Section : std::uint8_t {
        None,
        System,
        User,
    };

    Section currentSection = Section::None;
    bool hasUserSection = false;
    QStringList systemLines;
    QStringList userLines;

    for (qsizetype i = startIndex; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        const QString trimmed = line.trimmed();
        if (trimmed == QStringLiteral("=== system ===")) {
            currentSection = Section::System;
        } else if (trimmed == QStringLiteral("=== user ===")) {
            currentSection = Section::User;
            hasUserSection = true;
        } else {
            if (currentSection == Section::System) {
                systemLines.append(line);
            } else if (currentSection == Section::User) {
                userLines.append(line);
            }
        }
    }

    if (!hasUserSection) {
        return core::Error {
            .code = errc::kPromptInvalid,
            .message = QStringLiteral("Missing required user section ('=== user ===')"),
            .detail = id,
        };
    }

    BodyParseResult result;
    result.system = stripBlankLines(systemLines);
    result.user = stripBlankLines(userLines);
    return result;
}

const QRegularExpression &variableRegex()
{
    static const QRegularExpression s_variableRegex(
        QString::fromUtf8(R"(\{\{\s*([A-Za-z_][A-Za-z0-9_]*)\s*\}\})"));
    return s_variableRegex;
}

void collectMissingVariables(
    const QString &text, const QHash<QString, QString> &vars, QStringList &missingVars)
{
    const auto &regex = variableRegex();
    auto it = regex.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        const QString varName = match.captured(1);
        if (!vars.contains(varName) && !missingVars.contains(varName)) {
            missingVars.append(varName);
        }
    }
}

QString substituteVariables(const QString &text, const QHash<QString, QString> &vars)
{
    const auto &regex = variableRegex();
    QString result;
    result.reserve(text.size());
    qsizetype lastPos = 0;
    auto it = regex.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        result.append(text.sliced(lastPos, match.capturedStart() - lastPos));
        const QString varName = match.captured(1);
        result.append(vars.value(varName));
        lastPos = match.capturedEnd();
    }
    result.append(text.sliced(lastPos));
    return result;
}

QString candidatePath(const QString &dir, const QString &relativePath)
{
    return QDir(dir).filePath(relativePath);
}

core::Result<PromptTemplate> readTemplate(const QString &id, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return core::Error {
            .code = errc::kPromptInvalid,
            .message = QStringLiteral("Failed to open prompt template file"),
            .detail = path,
        };
    }

    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    auto tmplRes = PromptLibrary::parse(id, text);
    if (!tmplRes.ok()) {
        return tmplRes.error();
    }

    PromptTemplate tmpl = tmplRes.value();
    tmpl.path = path;
    return tmpl;
}

void warnIfOverrideOutdated(const PromptTemplate &used, const QStringList &dirs,
    qsizetype startIndex, const QString &relativePath)
{
    for (qsizetype j = startIndex; j < dirs.size(); ++j) {
        const QString lowerCandidate = candidatePath(dirs.at(j), relativePath);
        if (!QFile::exists(lowerCandidate)) {
            continue;
        }
        const auto lowerTmplRes = readTemplate(used.id, lowerCandidate);
        if (lowerTmplRes.ok()) {
            const int lowerVersion = lowerTmplRes.value().version;
            if (lowerVersion > used.version) {
                qCWarning(lcAi,
                    "Overridden prompt template '%s' (version %d at %s) is older than "
                    "lower-priority version (%d at %s)",
                    qUtf8Printable(used.id), used.version, qUtf8Printable(used.path), lowerVersion,
                    qUtf8Printable(lowerCandidate));
            }
            break;
        }
    }
}

} // namespace

PromptLibrary::PromptLibrary(QStringList dirs)
    : m_dirs(std::move(dirs))
{
}

core::Result<PromptTemplate> PromptLibrary::parse(const QString &id, const QString &text)
{
    if (!isValidPromptId(id)) {
        return core::Error {
            .code = errc::kPromptInvalid,
            .message = QStringLiteral("Invalid prompt template ID"),
            .detail = id,
        };
    }

    const QStringList lines = splitLines(text);
    const auto headerRes = parseHeader(id, lines);
    if (!headerRes.ok()) {
        return headerRes.error();
    }

    const auto &header = headerRes.value();
    const auto bodyRes = parseBody(id, lines, header.bodyStartIndex);
    if (!bodyRes.ok()) {
        return bodyRes.error();
    }

    const auto &body = bodyRes.value();
    PromptTemplate tmpl;
    tmpl.id = id;
    tmpl.version = header.version;
    tmpl.description = header.description;
    tmpl.system = body.system;
    tmpl.user = body.user;
    tmpl.path = QString();
    return tmpl;
}

core::Result<RenderedPrompt> PromptLibrary::renderTemplate(
    const PromptTemplate &tmpl, const QHash<QString, QString> &vars)
{
    QStringList missingVars;
    collectMissingVariables(tmpl.system, vars, missingVars);
    collectMissingVariables(tmpl.user, vars, missingVars);

    if (!missingVars.isEmpty()) {
        return core::Error {
            .code = errc::kPromptInvalid,
            .message = QStringLiteral("Missing required prompt variables"),
            .detail
            = QStringLiteral("Missing variables: %1").arg(missingVars.join(QStringLiteral(", "))),
        };
    }

    RenderedPrompt rendered;
    rendered.id = tmpl.id;
    rendered.version = tmpl.version;
    rendered.system = substituteVariables(tmpl.system, vars);
    rendered.user = substituteVariables(tmpl.user, vars);
    return rendered;
}

core::Result<PromptTemplate> PromptLibrary::load(const QString &id) const
{
    if (!isValidPromptId(id)) {
        return core::Error {
            .code = errc::kPromptInvalid,
            .message = QStringLiteral("Invalid prompt template ID"),
            .detail = id,
        };
    }

    const QString relativePath = id + QStringLiteral(".md");
    qsizetype foundDirIndex = -1;
    QString foundPath;

    for (qsizetype i = 0; i < m_dirs.size(); ++i) {
        const QString candidate = candidatePath(m_dirs.at(i), relativePath);
        if (QFile::exists(candidate)) {
            foundDirIndex = i;
            foundPath = candidate;
            break;
        }
    }

    if (foundDirIndex == -1) {
        return core::Error {
            .code = errc::kPromptNotFound,
            .message = QStringLiteral("Prompt template not found"),
            .detail = id,
        };
    }

    auto tmplRes = readTemplate(id, foundPath);
    if (!tmplRes.ok()) {
        return tmplRes.error();
    }

    const PromptTemplate &tmpl = tmplRes.value();
    warnIfOverrideOutdated(tmpl, m_dirs, foundDirIndex + 1, relativePath);

    return tmpl;
}

core::Result<RenderedPrompt> PromptLibrary::render(
    const QString &id, const QHash<QString, QString> &vars) const
{
    const auto tmplRes = load(id);
    if (!tmplRes.ok()) {
        return tmplRes.error();
    }
    return renderTemplate(tmplRes.value(), vars);
}

QList<ChatMessage> toMessages(const RenderedPrompt &prompt)
{
    QList<ChatMessage> messages;
    if (!prompt.system.isEmpty()) {
        messages.append(makeMessage(Role::System, prompt.system));
    }
    messages.append(makeMessage(Role::User, prompt.user));
    return messages;
}

} // namespace linernotes::ai
