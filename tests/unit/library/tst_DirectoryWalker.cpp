// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <library/DirectoryWalker.h>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {

using linernotes::library::DirectoryWalker;
using linernotes::library::WalkOptions;

class TstDirectoryWalker : public QObject {
    Q_OBJECT

private slots:
    void audioExtensionsAndIsAudioFile();
    void walksRecursivelyAndChecksCaseAndStat();
    void skipsHiddenFilesAndDirs();
    void excludes_data();
    void excludes();
    void excludedDirIsNotEntered();
    void symlinkLoopsAndFileSymlinks();
    void unreadableSubdirIsSkippedWithWarning();
    void subDirWalkWithRootExcludes();
    void cancellationStopsTraversal();
    void listDirectoriesCollectsValidDirectories();

private:
    static void createFile(
        const QString &path, const QByteArray &content = "audio_data", const QDateTime &mtime = { })
    {
        const QFileInfo fi(path);
        QDir().mkpath(fi.absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(content);
            f.flush();
            if (mtime.isValid()) {
                f.setFileTime(mtime, QFileDevice::FileModificationTime);
            }
            f.close();
        }
    }
};

void TstDirectoryWalker::audioExtensionsAndIsAudioFile()
{
    const auto &exts = DirectoryWalker::audioExtensions();
    QVERIFY(exts.contains(QStringLiteral("mp3")));
    QVERIFY(exts.contains(QStringLiteral("flac")));
    QVERIFY(exts.contains(QStringLiteral("ogg")));
    QVERIFY(exts.contains(QStringLiteral("oga")));
    QVERIFY(exts.contains(QStringLiteral("opus")));
    QVERIFY(exts.contains(QStringLiteral("m4a")));
    QVERIFY(exts.contains(QStringLiteral("m4b")));
    QVERIFY(exts.contains(QStringLiteral("mp4")));
    QVERIFY(exts.contains(QStringLiteral("aac")));
    QVERIFY(exts.contains(QStringLiteral("alac")));
    QVERIFY(exts.contains(QStringLiteral("wav")));
    QVERIFY(exts.contains(QStringLiteral("wv")));
    QVERIFY(exts.contains(QStringLiteral("ape")));
    QVERIFY(exts.contains(QStringLiteral("wma")));
    QVERIFY(exts.contains(QStringLiteral("asf")));
    QVERIFY(exts.contains(QStringLiteral("aif")));
    QVERIFY(exts.contains(QStringLiteral("aiff")));
    QVERIFY(exts.contains(QStringLiteral("dsf")));
    QVERIFY(exts.contains(QStringLiteral("dff")));
    QVERIFY(exts.contains(QStringLiteral("mpc")));
    QVERIFY(exts.contains(QStringLiteral("tta")));

    // Case insensitivity
    QVERIFY(DirectoryWalker::isAudioFile(QStringLiteral("song.MP3")));
    QVERIFY(DirectoryWalker::isAudioFile(QStringLiteral("song.fLaC")));
    QVERIFY(DirectoryWalker::isAudioFile(QStringLiteral("track.wav")));

    // Non-audio
    QVERIFY(!DirectoryWalker::isAudioFile(QStringLiteral("cover.jpg")));
    QVERIFY(!DirectoryWalker::isAudioFile(QStringLiteral("notes.txt")));
    QVERIFY(!DirectoryWalker::isAudioFile(QStringLiteral("file_no_extension")));
}

void TstDirectoryWalker::walksRecursivelyAndChecksCaseAndStat()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root = tempDir.path();
    const QDateTime t1 = QDateTime::fromMSecsSinceEpoch(1600000000000LL, QTimeZone::UTC);
    const QDateTime t2 = QDateTime::fromMSecsSinceEpoch(1610000000000LL, QTimeZone::UTC);
    const QDateTime t3 = QDateTime::fromMSecsSinceEpoch(1620000000000LL, QTimeZone::UTC);

    createFile(root + QStringLiteral("/song1.mp3"), "12345", t1);
    createFile(root + QStringLiteral("/song2.FLAC"), "1234567890", t2);
    createFile(root + QStringLiteral("/sub/song3.Ogg"), "123456789012345", t3);
    createFile(root + QStringLiteral("/cover.jpg"), "not audio");
    createFile(root + QStringLiteral("/notes.txt"), "text");

    const auto files = DirectoryWalker::walk(root, root, { });
    QCOMPARE(files.size(), 3);

    // Results are ordered by path
    QCOMPARE(files.at(0).path, QDir::cleanPath(root + QStringLiteral("/song1.mp3")));
    QCOMPARE(files.at(0).size, 5);
    QVERIFY(qAbs(files.at(0).mtimeMs - t1.toMSecsSinceEpoch()) <= 2000);

    QCOMPARE(files.at(1).path, QDir::cleanPath(root + QStringLiteral("/song2.FLAC")));
    QCOMPARE(files.at(1).size, 10);
    QVERIFY(qAbs(files.at(1).mtimeMs - t2.toMSecsSinceEpoch()) <= 2000);

    QCOMPARE(files.at(2).path, QDir::cleanPath(root + QStringLiteral("/sub/song3.Ogg")));
    QCOMPARE(files.at(2).size, 15);
    QVERIFY(qAbs(files.at(2).mtimeMs - t3.toMSecsSinceEpoch()) <= 2000);
}

void TstDirectoryWalker::skipsHiddenFilesAndDirs()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root = tempDir.path();
    createFile(root + QStringLiteral("/.hidden_song.mp3"));
    createFile(root + QStringLiteral("/.hidden_dir/song.flac"));
    createFile(root + QStringLiteral("/visible.mp3"));

    const auto files = DirectoryWalker::walk(root, root, { });
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.at(0).path, QDir::cleanPath(root + QStringLiteral("/visible.mp3")));
}

void TstDirectoryWalker::excludes_data()
{
    QTest::addColumn<QStringList>("excludes");
    QTest::addColumn<QStringList>("expectedRelativeFiles");

    QTest::newRow("podcasts_wildcard") << QStringList { QStringLiteral("Podcasts/*") }
                                       << QStringList {
                                              QStringLiteral("Classical/Live/concert.mp3"),
                                              QStringLiteral("Classical/Studio/album.mp3"),
                                              QStringLiteral("Music/song.flac"),
                                              QStringLiteral("Music/song.wav"),
                                              QStringLiteral("Other/test.mp3"),
                                              QStringLiteral("周杰伦/晴天.mp3"),
                                              QStringLiteral("周杰伦/范特西/夜曲.mp3"),
                                          };

    QTest::newRow("podcasts_dir") << QStringList { QStringLiteral("Podcasts/") }
                                  << QStringList {
                                         QStringLiteral("Classical/Live/concert.mp3"),
                                         QStringLiteral("Classical/Studio/album.mp3"),
                                         QStringLiteral("Music/song.flac"),
                                         QStringLiteral("Music/song.wav"),
                                         QStringLiteral("Other/test.mp3"),
                                         QStringLiteral("周杰伦/晴天.mp3"),
                                         QStringLiteral("周杰伦/范特西/夜曲.mp3"),
                                     };

    QTest::newRow("wav_ext") << QStringList { QStringLiteral("*.wav") }
                             << QStringList {
                                    QStringLiteral("Classical/Live/concert.mp3"),
                                    QStringLiteral("Classical/Studio/album.mp3"),
                                    QStringLiteral("Music/song.flac"),
                                    QStringLiteral("Other/test.mp3"),
                                    QStringLiteral("Podcasts/ep1.mp3"),
                                    QStringLiteral("Podcasts/sub/ep2.mp3"),
                                    QStringLiteral("周杰伦/晴天.mp3"),
                                    QStringLiteral("周杰伦/范特西/夜曲.mp3"),
                                };

    QTest::newRow("live_glob") << QStringList { QStringLiteral("**/Live/*") }
                               << QStringList {
                                      QStringLiteral("Classical/Studio/album.mp3"),
                                      QStringLiteral("Music/song.flac"),
                                      QStringLiteral("Music/song.wav"),
                                      QStringLiteral("Other/test.mp3"),
                                      QStringLiteral("Podcasts/ep1.mp3"),
                                      QStringLiteral("Podcasts/sub/ep2.mp3"),
                                      QStringLiteral("周杰伦/晴天.mp3"),
                                      QStringLiteral("周杰伦/范特西/夜曲.mp3"),
                                  };

    QTest::newRow("chinese_dir") << QStringList { QStringLiteral("周杰伦/*") }
                                 << QStringList {
                                        QStringLiteral("Classical/Live/concert.mp3"),
                                        QStringLiteral("Classical/Studio/album.mp3"),
                                        QStringLiteral("Music/song.flac"),
                                        QStringLiteral("Music/song.wav"),
                                        QStringLiteral("Other/test.mp3"),
                                        QStringLiteral("Podcasts/ep1.mp3"),
                                        QStringLiteral("Podcasts/sub/ep2.mp3"),
                                    };
}

void TstDirectoryWalker::excludes()
{
    QFETCH(QStringList, excludes);
    QFETCH(QStringList, expectedRelativeFiles);

    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root = tempDir.path();
    createFile(root + QStringLiteral("/Podcasts/ep1.mp3"));
    createFile(root + QStringLiteral("/Podcasts/sub/ep2.mp3"));
    createFile(root + QStringLiteral("/Music/song.wav"));
    createFile(root + QStringLiteral("/Music/song.flac"));
    createFile(root + QStringLiteral("/Classical/Live/concert.mp3"));
    createFile(root + QStringLiteral("/Classical/Studio/album.mp3"));
    createFile(root + QStringLiteral("/周杰伦/晴天.mp3"));
    createFile(root + QStringLiteral("/周杰伦/范特西/夜曲.mp3"));
    createFile(root + QStringLiteral("/Other/test.mp3"));

    WalkOptions options;
    options.excludes = excludes;

    const auto files = DirectoryWalker::walk(root, root, options);

    QStringList actualRelative;
    for (const auto &f : files) {
        actualRelative.append(QDir(root).relativeFilePath(f.path));
    }
    actualRelative.sort();

    QStringList sortedExpected = expectedRelativeFiles;
    sortedExpected.sort();

    QCOMPARE(actualRelative, sortedExpected);
}

void TstDirectoryWalker::excludedDirIsNotEntered()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root = tempDir.path();
    const QString unreadableSub = root + QStringLiteral("/Podcasts/unreadable_sub");
    QVERIFY(QDir().mkpath(unreadableSub));
    createFile(unreadableSub + QStringLiteral("/secret.mp3"));
    createFile(root + QStringLiteral("/Normal/song.mp3"));

    // Remove permissions on unreadable_sub.
    // If DirectoryWalker enters Podcasts, it would encounter an unreadable directory and log a
    // warning. By excluding Podcasts/*, it must skip the entire Podcasts tree without inspecting
    // children.
    QFile::setPermissions(unreadableSub, QFileDevice::Permissions { });

    // Ensure no warning is emitted during walk
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));

    WalkOptions options;
    options.excludes = { QStringLiteral("Podcasts/*") };

    const auto files = DirectoryWalker::walk(root, root, options);
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.at(0).path, QDir::cleanPath(root + QStringLiteral("/Normal/song.mp3")));

    // Restore permissions for clean tempDir teardown
    QFile::setPermissions(
        unreadableSub, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
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

    const QString cleanReal = QDir::cleanPath(realFile);
    const QString cleanLink = QDir::cleanPath(fileSymlink);

    QCOMPARE(files.at(0).path, cleanLink);
    QCOMPARE(files.at(1).path, cleanReal);
}

void TstDirectoryWalker::unreadableSubdirIsSkippedWithWarning()
{
#ifdef Q_OS_UNIX
    if (geteuid() == 0) {
        QSKIP("Running as root; permission check is not applicable.");
    }
#endif

    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root = tempDir.path();
    const QString goodFile = root + QStringLiteral("/good/song.mp3");
    const QString badDir = root + QStringLiteral("/bad");
    const QString badFile = badDir + QStringLiteral("/song.mp3");

    createFile(goodFile);
    createFile(badFile);

    // Make badDir unreadable
    QFile::setPermissions(badDir, QFileDevice::WriteOwner);

    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("Cannot read directory.*")));

    const auto files = DirectoryWalker::walk(root, root, { });
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.at(0).path, QDir::cleanPath(goodFile));

    // Restore permissions for clean tempDir teardown
    QFile::setPermissions(
        badDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

void TstDirectoryWalker::subDirWalkWithRootExcludes()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString root = tempDir.path();
    createFile(root + QStringLiteral("/disc1/track1.mp3"));
    createFile(root + QStringLiteral("/disc1/track2.wav"));
    createFile(root + QStringLiteral("/disc2/track3.mp3"));

    WalkOptions options;
    options.excludes = { QStringLiteral("disc1/*.wav") };

    // Walk with dir = root/disc1, excludes relative to root
    const auto files = DirectoryWalker::walk(root, root + QStringLiteral("/disc1"), options);
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.at(0).path, QDir::cleanPath(root + QStringLiteral("/disc1/track1.mp3")));
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

    // Create a directory symlink loop
    const QString loopLink = sub1 + QStringLiteral("/loop");
    QVERIFY(QFile::link(root, loopLink));

    const auto dirs = DirectoryWalker::listDirectories(
        root, { QStringLiteral("excluded_sub/*"), QStringLiteral("excluded_sub") });

    const QString cleanRoot = QDir::cleanPath(root);
    const QString cleanSub1 = QDir::cleanPath(sub1);
    const QString cleanSub2Parent = QDir::cleanPath(root + QStringLiteral("/sub2"));
    const QString cleanSub2 = QDir::cleanPath(sub2);

    QCOMPARE(dirs.size(), 4);
    QCOMPARE(dirs.at(0), cleanRoot);
    QCOMPARE(dirs.at(1), cleanSub1);
    QCOMPARE(dirs.at(2), cleanSub2Parent);
    QCOMPARE(dirs.at(3), cleanSub2);

    // Non-existent root returns empty
    const auto emptyDirs
        = DirectoryWalker::listDirectories(root + QStringLiteral("/non_existent_12345"));
    QVERIFY(emptyDirs.isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstDirectoryWalker)

#include "tst_DirectoryWalker.moc"
