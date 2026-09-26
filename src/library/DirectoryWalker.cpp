// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "DirectoryWalker.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <library/LibraryLogging.h>

#include <algorithm>
#include <vector>

namespace linernotes::library {

namespace {

QString cleanDirectoryPath(const QString &path)
{
    QString cleaned = QDir::cleanPath(path);
    if (cleaned.length() > 1 && cleaned.endsWith(u'/')) {
        cleaned.chop(1);
    }
    return cleaned;
}

QList<QRegularExpression> compileExcludes(const QStringList &excludes)
{
    QList<QRegularExpression> compiled;
    compiled.reserve(excludes.size());
    for (const auto &pat : excludes) {
        if (!pat.isEmpty()) {
            const QString regexStr = QRegularExpression::wildcardToRegularExpression(
                pat, QRegularExpression::NonPathWildcardConversion);
            compiled.append(QRegularExpression(regexStr));
        }
    }
    return compiled;
}

QString getRelPath(const QString &cleanRoot, const QString &absPath)
{
    if (absPath == cleanRoot) {
        return { };
    }
    if (cleanRoot == QStringLiteral("/")) {
        return absPath.startsWith(u'/') ? absPath.sliced(1) : absPath;
    }
    if (absPath.startsWith(cleanRoot + u'/')) {
        return absPath.sliced(cleanRoot.length() + 1);
    }
    return QDir(cleanRoot).relativeFilePath(absPath);
}

bool isDirExcluded(const QString &relPath, const QList<QRegularExpression> &compiledExcludes)
{
    if (compiledExcludes.isEmpty() || relPath.isEmpty()) {
        return false;
    }
    const QString relWithSlash = relPath + u'/';
    return std::ranges::any_of(compiledExcludes, [&](const auto &re) {
        return re.match(relPath).hasMatch() || re.match(relWithSlash).hasMatch();
    });
}

bool isFileExcluded(const QString &relPath, const QList<QRegularExpression> &compiledExcludes)
{
    if (compiledExcludes.isEmpty() || relPath.isEmpty()) {
        return false;
    }
    return std::ranges::any_of(
        compiledExcludes, [&](const auto &re) { return re.match(relPath).hasMatch(); });
}

void processFileEntry(const QFileInfo &entry, const QString &cleanRoot,
    const QList<QRegularExpression> &compiledExcludes, QList<WalkedFile> &result)
{
    if (!DirectoryWalker::isAudioFile(entry.fileName())) {
        return;
    }
    const QString relPath = getRelPath(cleanRoot, entry.absoluteFilePath());
    if (isFileExcluded(relPath, compiledExcludes)) {
        return;
    }
    WalkedFile wf;
    wf.path = entry.absoluteFilePath();
    wf.size = entry.size();
    wf.mtimeMs = entry.lastModified().toMSecsSinceEpoch();
    result.append(std::move(wf));
}

void processEntry(const QFileInfo &entry, const QString &cleanRoot,
    const QList<QRegularExpression> &compiledExcludes, std::vector<QString> &dirsToVisit,
    QList<WalkedFile> &result)
{
    if (entry.fileName().startsWith(u'.')) {
        return;
    }

    if (entry.isSymLink()) {
        if (entry.isDir()) {
            return; // Do not follow directory symlinks (avoids loops)
        }
        if (entry.isFile()) {
            processFileEntry(entry, cleanRoot, compiledExcludes, result);
        }
        return;
    }

    if (entry.isDir()) {
        const QString relPath = getRelPath(cleanRoot, entry.absoluteFilePath());
        if (!isDirExcluded(relPath, compiledExcludes)) {
            dirsToVisit.push_back(entry.absoluteFilePath());
        }
        return;
    }

    if (entry.isFile()) {
        processFileEntry(entry, cleanRoot, compiledExcludes, result);
    }
}

} // namespace

const QStringList &DirectoryWalker::audioExtensions()
{
    static const QStringList s_extensions = { QStringLiteral("mp3"), QStringLiteral("flac"),
        QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("m4a"),
        QStringLiteral("m4b"), QStringLiteral("mp4"), QStringLiteral("aac"), QStringLiteral("alac"),
        QStringLiteral("wav"), QStringLiteral("wv"), QStringLiteral("ape"), QStringLiteral("wma"),
        QStringLiteral("asf"), QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("dsf"),
        QStringLiteral("dff"), QStringLiteral("mpc"), QStringLiteral("tta") };
    return s_extensions;
}

bool DirectoryWalker::isAudioFile(const QString &fileName)
{
    static const QSet<QString> s_extSet = []() {
        const auto &exts = audioExtensions();
        return QSet<QString>(exts.cbegin(), exts.cend());
    }();

    const auto dotIdx = fileName.lastIndexOf(u'.');
    if (dotIdx < 0 || dotIdx == fileName.length() - 1) {
        return false;
    }
    const QString ext = fileName.sliced(dotIdx + 1).toLower();
    return s_extSet.contains(ext);
}

QList<WalkedFile> DirectoryWalker::walk(
    const QString &root, const QString &dir, const WalkOptions &options, bool *cancelled)
{
    if (cancelled != nullptr) {
        *cancelled = false;
    }

    const QString cleanRoot = cleanDirectoryPath(root);
    const QString cleanDir = cleanDirectoryPath(dir);

    const bool isSubdirOfRoot = (cleanDir == cleanRoot)
        || (cleanRoot == QStringLiteral("/") ? cleanDir.startsWith(u'/')
                                             : cleanDir.startsWith(cleanRoot + u'/'));
    if (!isSubdirOfRoot) {
        return { };
    }

    const QFileInfo rootDirInfo(cleanDir);
    if (!rootDirInfo.exists() || !rootDirInfo.isDir()) {
        return { };
    }

    const auto compiledExcludes = compileExcludes(options.excludes);

    if (cleanDir != cleanRoot) {
        const QString startRel = getRelPath(cleanRoot, cleanDir);
        if (isDirExcluded(startRel, compiledExcludes)) {
            return { };
        }
    }

    QList<WalkedFile> result;
    std::vector<QString> dirsToVisit;
    dirsToVisit.push_back(cleanDir);

    while (!dirsToVisit.empty()) {
        if (options.isCancelled && options.isCancelled()) {
            if (cancelled != nullptr) {
                *cancelled = true;
            }
            break;
        }

        const QString currentDir = std::move(dirsToVisit.back());
        dirsToVisit.pop_back();

        const QFileInfo currentDirInfo(currentDir);
        if (!currentDirInfo.isReadable()) {
            qCWarning(lcLibrary) << "Cannot read directory (permission denied):" << currentDir;
            continue;
        }

        const QDir dirObj(currentDir);
        const QFileInfoList entries = dirObj.entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::Name);

        for (const auto &entry : entries) {
            processEntry(entry, cleanRoot, compiledExcludes, dirsToVisit, result);
        }
    }

    std::ranges::sort(
        result, [](const WalkedFile &a, const WalkedFile &b) { return a.path < b.path; });

    return result;
}

} // namespace linernotes::library
