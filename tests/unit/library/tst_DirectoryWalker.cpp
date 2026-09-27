// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <library/DirectoryWalker.h>

namespace {

using linernotes::library::DirectoryWalker;
using linernotes::library::WalkOptions;

class TstDirectoryWalker : public QObject {
    Q_OBJECT

private slots:
    void audioExtensionsAndIsAudioFile();
    void walksRecursivelyAndExcludes();
    void symlinkLoopsAndFileSymlinks();
    void cancellationStopsTraversal();
    void listDirectoriesCollectsValidDirectories();

private:
    static void createFile(const QString &path, const QByteArray &content = "audio_data")
    {
        const QFileInfo fi(path);
        QDir().mkpath(fi.absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(content);
            f.close();
        }
    }
};

void TstDirectoryWalker::audioExtensionsAndIsAudioFile()
{
    const auto &exts = DirectoryWalker::audioExtensions();
    QVERIFY(exts.contains(QStringLiteral("mp3")));
    QVERIFY(exts.contains(QStringLiteral("flac")));
    QVERIFY(exts.contains(QStringLiteral("wav")));

    QVERIFY(DirectoryWalker::isAudioFile(QStringLiteral("song.MP3")));
    QVERIFY(DirectoryWalker::isAudioFile(QStringLiteral("song.fLaC")));
    QVERIFY(!DirectoryWalker::isAudioFile(QStringLiteral("cover.jpg")));
    QVERIFY(!DirectoryWalker::isAudioFile(QStringLiteral("file_no_extension")));
}

void TstDirectoryWalker::walksRecursivelyAndExcludes()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root = tempDir.path();
    createFile(root + QStringLiteral("/song1.mp3"));
    createFile(root + QStringLiteral("/sub/song2.FLAC"));
    createFile(root + QStringLiteral("/sub/ignored.wav"));
    createFile(root + QStringLiteral("/.hidden_dir/hidden.mp3"));
    createFile(root + QStringLiteral("/cover.jpg"));

    WalkOptions options;
    options.excludes = { QStringLiteral("*.wav") };

    const auto files = DirectoryWalker::walk(root, root, options);
    QCOMPARE(files.size(), 2);
    QCOMPARE(files.at(0).path, QDir::cleanPath(root + QStringLiteral("/song1.mp3")));
    QCOMPARE(files.at(1).path, QDir::cleanPath(root + QStringLiteral("/sub/song2.FLAC")));
}

void TstDirectoryWalker::symlinkLoopsAndFileSymlinks()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root = tempDir.path();
    const QString realFile = root + QStringLiteral("/real/track.mp3");
    createFile(realFile);

    const QString fileSymlink = root + QStringLiteral("/link_track.mp3");
    QVERIFY(QFile::link(realFile, fileSymlink));

    // Create a directory symlink loop pointing back to root
    const QString dirSymlink = root + QStringLiteral("/real/loop");
    QVERIFY(QFile::link(root, dirSymlink));

    const auto files = DirectoryWalker::walk(root, root, { });
    QCOMPARE(files.size(), 2);
    QCOMPARE(files.at(0).path, QDir::cleanPath(fileSymlink));
    QCOMPARE(files.at(1).path, QDir::cleanPath(realFile));
}

void TstDirectoryWalker::cancellationStopsTraversal()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root = tempDir.path();
    createFile(root + QStringLiteral("/dir1/song1.mp3"));
    createFile(root + QStringLiteral("/dir2/song2.mp3"));
    createFile(root + QStringLiteral("/dir3/song3.mp3"));

    int callCount = 0;
    WalkOptions options;
    options.isCancelled = [&callCount]() { return ++callCount >= 2; };

    bool cancelled = false;
    const auto files = DirectoryWalker::walk(root, root, options, &cancelled);
    QVERIFY(cancelled);
    QVERIFY(files.size() < 3);
}

void TstDirectoryWalker::listDirectoriesCollectsValidDirectories()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root = tempDir.path();
    const QString sub1 = root + QStringLiteral("/sub1");
    const QString sub2 = root + QStringLiteral("/sub2/nested");
    const QString hiddenSub = root + QStringLiteral("/.hidden_sub");
    const QString excludedSub = root + QStringLiteral("/excluded_sub");

    QDir().mkpath(sub1);
    QDir().mkpath(sub2);
    QDir().mkpath(hiddenSub);
    QDir().mkpath(excludedSub);

    const auto dirs = DirectoryWalker::listDirectories(
        root, { QStringLiteral("excluded_sub/*"), QStringLiteral("excluded_sub") });

    QCOMPARE(dirs.size(), 4);
    QCOMPARE(dirs.at(0), QDir::cleanPath(root));
    QCOMPARE(dirs.at(1), QDir::cleanPath(sub1));
    QCOMPARE(dirs.at(2), QDir::cleanPath(root + QStringLiteral("/sub2")));
    QCOMPARE(dirs.at(3), QDir::cleanPath(sub2));
}

} // namespace

QTEST_GUILESS_MAIN(TstDirectoryWalker)

#include "tst_DirectoryWalker.moc"
