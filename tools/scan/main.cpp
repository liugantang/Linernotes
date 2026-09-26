// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QSqlQuery>

#include <core/Paths.h>
#include <library/Database.h>
#include <library/LibraryRoots.h>
#include <library/Migrator.h>
#include <library/Scanner.h>
#include <poll.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <csignal>
#include <iostream>
#include <optional>
#include <thread>

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::array<int, 2> s_sigPipe = { -1, -1 };

void signalHandler(int /*sig*/)
{
    const char c = 'q';
    if (std::get<1>(s_sigPipe) != -1) {
        [[maybe_unused]] const auto n = ::write(std::get<1>(s_sigPipe), &c, 1);
    }
}

struct CliConfig {
    QString dbPath;
    QStringList excludes;
    QStringList dirs;
    int maxThreads = 0;
    bool verbose = false;
};

struct CliOptions {
    QCommandLineOption dbOption { QStringLiteral("db"),
        QStringLiteral("Path to SQLite database file."), QStringLiteral("path") };
    QCommandLineOption excludeOption { QStringLiteral("exclude"),
        QStringLiteral(
            "Exclude glob pattern for newly added library roots (can be specified multiple "
            "times)."),
        QStringLiteral("glob") };
    QCommandLineOption threadsOption { QStringLiteral("threads"),
        QStringLiteral("Maximum worker threads for reading tags."), QStringLiteral("N") };
    QCommandLineOption verboseOption { QStringLiteral("verbose"),
        QStringLiteral("Show detailed output and failed file details.") };
};

void setupParser(QCommandLineParser &parser, const CliOptions &opts)
{
    parser.setApplicationDescription(QStringLiteral("Linernotes music library scanner"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(opts.dbOption);
    parser.addOption(opts.excludeOption);
    parser.addOption(opts.threadsOption);
    parser.addOption(opts.verboseOption);
    parser.addPositionalArgument(QStringLiteral("dir"),
        QStringLiteral("Directories to add as library roots and scan."),
        QStringLiteral("<dir>..."));
}

std::optional<CliConfig> parseArgs(
    QCommandLineParser &parser, const CliOptions &opts, int &exitCode)
{
    if (!parser.parse(QCoreApplication::arguments())) {
        std::cerr << qPrintable(parser.errorText()) << "\n";
        exitCode = 2;
        return std::nullopt;
    }

    if (parser.isSet(QStringLiteral("help"))) {
        parser.showHelp(0);
    }
    if (parser.isSet(QStringLiteral("version"))) {
        parser.showVersion();
    }

    const QStringList dirs = parser.positionalArguments();
    if (dirs.isEmpty()) {
        std::cerr << qPrintable(parser.helpText());
        exitCode = 2;
        return std::nullopt;
    }

    int maxThreads = 0;
    if (parser.isSet(opts.threadsOption)) {
        bool ok = false;
        maxThreads = parser.value(opts.threadsOption).toInt(&ok);
        if (!ok || maxThreads <= 0) {
            std::cerr << "Invalid --threads specified: "
                      << qPrintable(parser.value(opts.threadsOption)) << " (must be > 0)\n";
            exitCode = 2;
            return std::nullopt;
        }
    }

    QString dbPath;
    if (parser.isSet(opts.dbOption)) {
        dbPath = parser.value(opts.dbOption);
    } else {
        const auto paths = linernotes::core::Paths::fromEnvironment();
        paths.ensureCreated();
        dbPath = paths.dataDir() + QStringLiteral("/library.db");
    }

    return CliConfig {
        .dbPath = dbPath,
        .excludes = parser.values(opts.excludeOption),
        .dirs = dirs,
        .maxThreads = maxThreads,
        .verbose = parser.isSet(opts.verboseOption),
    };
}

void printVerboseFailedFiles(linernotes::library::Database &db)
{
    const auto connRes = db.connection();
    if (!connRes.ok()) {
        return;
    }
    QSqlQuery q(connRes.value());
    if (q.exec(QStringLiteral(
            "SELECT path, scan_error FROM files WHERE scan_error IS NOT NULL AND missing_since "
            "IS NULL ORDER BY path ASC"))) {
        std::cout << "Failed files:\n";
        while (q.next()) {
            std::cout << "  " << q.value(0).toString().toStdString() << ": "
                      << q.value(1).toString().toStdString() << "\n";
        }
    }
}

void printScanSummary(
    linernotes::library::Database &db, const linernotes::library::ScanStats &stats, bool verbose)
{
    int totalTracks = 0;
    int totalAlbums = 0;
    int totalArtists = 0;
    const auto connRes = db.connection();
    if (connRes.ok()) {
        QSqlQuery q(connRes.value());
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM tracks")) && q.next()) {
            totalTracks = q.value(0).toInt();
        }
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM albums")) && q.next()) {
            totalAlbums = q.value(0).toInt();
        }
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM artists")) && q.next()) {
            totalArtists = q.value(0).toInt();
        }
    }

    const double elapsedSec = static_cast<double>(stats.elapsedMs) / 1000.0;
    const double filesPerSec
        = elapsedSec > 0.0 ? static_cast<double>(stats.found) / elapsedSec : 0.0;

    if (stats.cancelled) {
        std::cout << "已取消 (Scan cancelled).\n";
    }

    std::cout << QStringLiteral(
        "Found %1 files: %2 added, %3 updated, %4 unchanged, %5 moved, %6 missing, %7 "
        "restored, %8 failed in %9 ms (%10 files/s). Total tracks in database: %11, albums: %12, "
        "artists: %13.\n")
                     .arg(QString::number(stats.found), QString::number(stats.added),
                         QString::number(stats.updated), QString::number(stats.unchanged),
                         QString::number(stats.moved), QString::number(stats.missing),
                         QString::number(stats.restored), QString::number(stats.failed),
                         QString::number(stats.elapsedMs), QString::number(filesPerSec, 'f', 1),
                         QString::number(totalTracks), QString::number(totalAlbums),
                         QString::number(totalArtists))
                     .toStdString();

    if (verbose && stats.failed > 0) {
        printVerboseFailedFiles(db);
    }
}

int runCli(const CliConfig &cfg)
{
    linernotes::library::Database db(cfg.dbPath);
    const linernotes::library::Migrator migrator;
    const auto openRes = db.open(migrator);
    if (!openRes.ok()) {
        std::cerr << "Database error: " << qPrintable(openRes.error().toString()) << "\n";
        return 1;
    }

    linernotes::library::LibraryRoots roots(db);
    for (const auto &dir : cfg.dirs) {
        const auto addRes = roots.add(dir, cfg.excludes);
        if (!addRes.ok()) {
            std::cerr << "Failed to add library root '" << qPrintable(dir)
                      << "': " << qPrintable(addRes.error().toString()) << "\n";
            return 2;
        }
    }

    linernotes::library::Scanner::Options opts;
    opts.maxThreads = cfg.maxThreads;
    linernotes::library::Scanner scanner(db, opts);

    if (::pipe(s_sigPipe.data()) == 0) {
        struct sigaction sa { };
        sa.sa_handler = signalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        ::sigaction(SIGINT, &sa, nullptr);
        ::sigaction(SIGTERM, &sa, nullptr);
    }

    std::atomic<bool> watcherRunning { true };
    std::thread watcher([&]() {
        while (watcherRunning.load()) {
            struct pollfd pfd { };
            pfd.fd = std::get<0>(s_sigPipe);
            pfd.events = POLLIN;
            const int ret = poll(&pfd, 1, 100);
            if (ret > 0 && (pfd.revents & POLLIN)) {
                char c = 0;
                if (::read(std::get<0>(s_sigPipe), &c, 1) > 0) {
                    if (c == 'q') {
                        scanner.cancel();
                    }
                    break;
                }
            }
        }
    });

    const auto scanRes = scanner.scanBlocking();

    watcherRunning.store(false);
    if (std::get<1>(s_sigPipe) != -1) {
        const char quitC = 'x';
        [[maybe_unused]] const auto n = ::write(std::get<1>(s_sigPipe), &quitC, 1);
    }
    if (watcher.joinable()) {
        watcher.join();
    }
    if (std::get<0>(s_sigPipe) != -1) {
        ::close(std::get<0>(s_sigPipe));
        ::close(std::get<1>(s_sigPipe));
        std::get<0>(s_sigPipe) = -1;
        std::get<1>(s_sigPipe) = -1;
    }

    if (!scanRes.ok()) {
        std::cerr << "Scan failed: " << qPrintable(scanRes.error().toString()) << "\n";
        return 1;
    }

    printScanSummary(db, scanRes.value(), cfg.verbose);
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    try {
        const QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("linernotes-scan"));
        QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

        const CliOptions opts;
        QCommandLineParser parser;
        setupParser(parser, opts);

        int exitCode = 0;
        const auto cfg = parseArgs(parser, opts, exitCode);
        if (!cfg.has_value()) {
            return exitCode;
        }

        if (!cfg->verbose) {
            QLoggingCategory::setFilterRules(QStringLiteral("linernotes.*.debug=false"));
        }

        return runCli(*cfg);
    } catch (const std::exception &e) {
        std::cerr << "Fatal exception: " << e.what() << "\n";
        return 1;
    }
}
