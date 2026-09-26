// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlayCli.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace linernotes::playcli {

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::array<int, 2> s_signalFd = { -1, -1 };

void signalHandler(int sig)
{
    const char a = static_cast<char>(sig);
    [[maybe_unused]] const ssize_t written = ::write(s_signalFd.at(0), &a, sizeof(a));
}

} // namespace

PlayCli::PlayCli(const QString &ao, QObject *parent)
    : QObject(parent)
    , m_player(ao.isEmpty() ? player::MpvHandle::OptionList { }
                            : player::MpvHandle::OptionList { { QStringLiteral("ao"), ao } })
{
    setupPlayer();
}

PlayCli::~PlayCli()
{
    if (s_signalFd.at(0) != -1) {
        ::close(s_signalFd.at(0));
        s_signalFd.at(0) = -1;
    }
    if (s_signalFd.at(1) != -1) {
        ::close(s_signalFd.at(1));
        s_signalFd.at(1) = -1;
    }
}

bool PlayCli::init()
{
    if (!m_player.isValid()) {
        std::cerr << "Failed to initialize player.\n";
        return false;
    }
    setupSignals();
    setupStdin();
    return true;
}

void PlayCli::setupPlayer()
{
    connect(
        &m_player, &player::Player::stateChanged, this, [](player::Player::PlaybackState state) {
            const char *stateStr = "Stopped";
            switch (state) {
            case player::Player::PlaybackState::Stopped:
                stateStr = "Stopped";
                break;
            case player::Player::PlaybackState::Playing:
                stateStr = "Playing";
                break;
            case player::Player::PlaybackState::Paused:
                stateStr = "Paused";
                break;
            }
            std::cout << "STATE " << stateStr << "\n" << std::flush;
        });

    connect(&m_player, &player::Player::currentSourceChanged, this, [this](const QString &source) {
        if (!source.isEmpty()) {
            const int idx = m_player.queue()->currentIndex();
            std::cout << "PLAYING " << idx << " " << source.toStdString() << "\n" << std::flush;
        }
    });

    connect(&m_player, &player::Player::playbackError, this,
        [](const QString &source, const QString &message) {
            std::cout << "ERROR " << source.toStdString() << " " << message.toStdString() << "\n"
                      << std::flush;
        });

    connect(&m_player, &player::Player::playbackFinished, this, [this]() {
        std::cout << "FINISHED\n" << std::flush;
        quit(0);
    });
}

void PlayCli::setupSignals()
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, s_signalFd.data()) < 0) {
        std::cerr << "Failed to create signal socketpair.\n";
        return;
    }

    struct sigaction sa { };
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (::sigaction(SIGINT, &sa, nullptr) < 0 || ::sigaction(SIGTERM, &sa, nullptr) < 0) {
        std::cerr << "Failed to install signal handlers.\n";
    }

    m_signalNotifier = new QSocketNotifier(s_signalFd.at(1), QSocketNotifier::Read, this);
    connect(m_signalNotifier, &QSocketNotifier::activated, this, &PlayCli::handleSignal);
}

void PlayCli::setupStdin()
{
    const int flags = ::fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    if (::isatty(STDIN_FILENO) != 0) {
        std::cout << "Commands (press Enter after each): p pause/resume, n next, b previous, "
                     "s <sec> seek, v <0-100> volume, d <gain 0-1> [ms] duck, m <mode>, q quit\n"
                  << std::flush;
    }

    m_stdinNotifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
    connect(m_stdinNotifier, &QSocketNotifier::activated, this, &PlayCli::handleStdin);
}

void PlayCli::setReportMemoryInterval(int intervalSec)
{
    if (intervalSec <= 0) {
        return;
    }
    m_elapsedTimer.start();
    m_reportMemoryTimer.setInterval(intervalSec * 1000);
    connect(&m_reportMemoryTimer, &QTimer::timeout, this, &PlayCli::handleReportMemory);
    m_reportMemoryTimer.start();
}

void PlayCli::setStateFile(const QString &path)
{
    m_stateFilePath = path;
    m_stateStore = std::make_unique<player::PlaybackStateStore>(path);
}

bool PlayCli::playFiles(const QStringList &files, player::PlayMode mode, std::optional<int> volume)
{
    if (files.isEmpty()) {
        return false;
    }

    QList<player::QueueItem> items;
    items.reserve(files.size());
    for (const auto &file : files) {
        items.append(player::QueueItem { .source = file });
    }

    m_player.queue()->setItems(items, 0);
    m_player.queue()->setMode(mode);
    if (volume.has_value()) {
        m_player.setVolume(*volume);
    }

    m_player.playIndex(0);
    return true;
}

bool PlayCli::restoreState(std::optional<player::PlayMode> mode, std::optional<int> volume)
{
    if (!m_stateStore) {
        return false;
    }

    const auto snapshot = m_stateStore->load();
    if (!snapshot.has_value()) {
        std::cerr << "Failed to load state from: " << m_stateFilePath.toStdString() << "\n";
        return false;
    }

    m_player.restore(*snapshot);

    if (mode.has_value()) {
        m_player.queue()->setMode(*mode);
    }
    if (volume.has_value()) {
        m_player.setVolume(*volume);
    }

    std::cerr << "Restored from state file. Press 'p' to resume.\n" << std::flush;
    return true;
}

void PlayCli::quit(int exitCode)
{
    if (m_isQuitting) {
        return;
    }
    m_isQuitting = true;

    if (m_stateStore) {
        const auto snap = m_player.snapshot();
        if (!m_stateStore->save(snap)) {
            std::cerr << "Failed to save playback state.\n";
        }
    }

    QCoreApplication::exit(exitCode);
}

void PlayCli::handleSignal()
{
    if (m_signalNotifier != nullptr) {
        m_signalNotifier->setEnabled(false);
    }
    char sig = 0;
    [[maybe_unused]] const ssize_t bytesRead = ::read(s_signalFd.at(1), &sig, sizeof(sig));
    quit(0);
}

void PlayCli::processStdinBuffer()
{
    while (true) {
        const qsizetype newlineIdx = m_stdinBuffer.indexOf('\n');
        if (newlineIdx == -1) {
            break;
        }
        const QByteArray lineBytes = m_stdinBuffer.left(newlineIdx).trimmed();
        m_stdinBuffer.remove(0, newlineIdx + 1);
        if (!lineBytes.isEmpty()) {
            handleStdinCommand(QString::fromUtf8(lineBytes));
        }
    }
}

void PlayCli::handleStdin()
{
    std::array<char, 1024> buf { };
    while (true) {
        const ssize_t bytesRead = ::read(STDIN_FILENO, buf.data(), buf.size());
        if (bytesRead > 0) {
            m_stdinBuffer.append(buf.data(), static_cast<qsizetype>(bytesRead));
            processStdinBuffer();
        } else if (bytesRead == 0) {
            // EOF: handle remaining text if any, then disable notifier to avoid busy looping
            if (!m_stdinBuffer.trimmed().isEmpty()) {
                handleStdinCommand(QString::fromUtf8(m_stdinBuffer.trimmed()));
                m_stdinBuffer.clear();
            }
            if (m_stdinNotifier != nullptr) {
                m_stdinNotifier->setEnabled(false);
            }
            break;
        } else {
            // EAGAIN / EWOULDBLOCK or error
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                if (m_stdinNotifier != nullptr) {
                    m_stdinNotifier->setEnabled(false);
                }
            }
            break;
        }
    }
}

void PlayCli::handleStdinCommand(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (!executeCommand(trimmed)) {
        std::cout << "UNKNOWN " << trimmed.toStdString()
                  << " (commands: p, n, b, s <sec>, v <0-100>, d <gain> [ms], m <mode>, q)\n"
                  << std::flush;
        return;
    }
    std::cout << "OK " << trimmed.toStdString() << "\n" << std::flush;
}

namespace {

bool executeSeek(player::Player &player, const QString &trimmed)
{
    bool ok = false;
    const double sec = QStringView(trimmed).sliced(2).trimmed().toDouble(&ok);
    if (!ok) {
        return false;
    }
    player.seek(sec);
    return true;
}

bool executeVolume(player::Player &player, const QString &trimmed)
{
    bool ok = false;
    const int vol = QStringView(trimmed).sliced(2).trimmed().toInt(&ok);
    if (!ok) {
        return false;
    }
    player.setVolume(std::clamp(vol, 0, 100));
    return true;
}

bool executeDuck(player::Player &player, const QString &trimmed)
{
    const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("\\s+")));
    bool ok = false;
    const double gain = parts.value(1).toDouble(&ok);
    const int rampMs = parts.size() > 2 ? parts.at(2).toInt() : 300;
    if (!ok) {
        return false;
    }
    player.duckTo(gain, rampMs);
    return true;
}

bool executeMode(player::Player &player, const QString &trimmed)
{
    const QString modeStr = QStringView(trimmed).sliced(2).trimmed().toString().toLower();
    if (modeStr == QStringLiteral("sequential")) {
        player.queue()->setMode(player::PlayMode::Sequential);
    } else if (modeStr == QStringLiteral("repeat-all")) {
        player.queue()->setMode(player::PlayMode::RepeatAll);
    } else if (modeStr == QStringLiteral("repeat-one")) {
        player.queue()->setMode(player::PlayMode::RepeatOne);
    } else if (modeStr == QStringLiteral("shuffle")) {
        player.queue()->setMode(player::PlayMode::Shuffle);
    } else {
        return false;
    }
    return true;
}

} // namespace

bool PlayCli::executeCommand(const QString &trimmed)
{
    if (trimmed == QStringLiteral("p")) {
        m_player.togglePause();
        return true;
    }
    if (trimmed == QStringLiteral("n")) {
        m_player.next();
        return true;
    }
    if (trimmed == QStringLiteral("b")) {
        m_player.previous();
        return true;
    }
    if (trimmed == QStringLiteral("q")) {
        quit(0);
        return true;
    }
    if (trimmed.startsWith(QStringLiteral("s ")) || trimmed.startsWith(QStringLiteral("s\t"))) {
        return executeSeek(m_player, trimmed);
    }
    if (trimmed.startsWith(QStringLiteral("v ")) || trimmed.startsWith(QStringLiteral("v\t"))) {
        return executeVolume(m_player, trimmed);
    }
    if (trimmed.startsWith(QStringLiteral("d ")) || trimmed.startsWith(QStringLiteral("d\t"))) {
        return executeDuck(m_player, trimmed);
    }
    if (trimmed.startsWith(QStringLiteral("m ")) || trimmed.startsWith(QStringLiteral("m\t"))) {
        return executeMode(m_player, trimmed);
    }
    return false;
}

void PlayCli::handleReportMemory()
{
    const qint64 rssKb = readVmRssKb();
    const qint64 elapsedSec = m_elapsedTimer.elapsed() / 1000;
    std::cout << "MEM rss_kb=" << rssKb << " elapsed_s=" << elapsedSec << "\n" << std::flush;
}

qint64 PlayCli::readVmRssKb()
{
    FILE *fp = std::fopen("/proc/self/status", "r");
    if (fp == nullptr) {
        return 0;
    }
    std::array<char, 256> line { };
    qint64 rssKb = 0;
    while (std::fgets(line.data(), static_cast<int>(line.size()), fp) != nullptr) {
        const QByteArrayView lineView(line.data());
        if (lineView.startsWith("VmRSS:")) {
            bool ok = false;
            rssKb = lineView.sliced(6).trimmed().toLongLong(&ok);
            break;
        }
    }
    std::fclose(fp);
    return rssKb;
}

} // namespace linernotes::playcli
