// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "UnixSignals.h"

#ifdef Q_OS_UNIX

#include <QCoreApplication>
#include <QSocketNotifier>

#include <core/Logging.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>

namespace linernotes::core {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - signal handlers can only reach
// global state
std::array<int, 2> s_sigFd = { -1, -1 };
struct sigaction s_oldSigTerm { };
struct sigaction s_oldSigInt { };
struct sigaction s_oldSigHup { };
bool s_handlersInstalled = false;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void signalHandler(int sig)
{
    const char a = static_cast<char>(sig);
    if (std::get<1>(s_sigFd) != -1) {
        (void)::write(std::get<1>(s_sigFd), &a, sizeof(a));
    }

    // 收到信号后立即恢复默认处理：若再次收到信号则直接终止进程，避免卡死时无法结束。
    struct sigaction sa { };
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGHUP, &sa, nullptr);
}

} // namespace

UnixSignalQuitter::UnixSignalQuitter(QObject *parent)
    : QObject(parent)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, s_sigFd.data()) != 0) {
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, s_sigFd.data()) != 0) {
            qCWarning(lcCore, "UnixSignalQuitter: socketpair failed: %s", std::strerror(errno));
            return;
        }
    }

    struct sigaction sa { };
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (::sigaction(SIGTERM, &sa, &s_oldSigTerm) != 0) {
        qCWarning(lcCore, "UnixSignalQuitter: sigaction SIGTERM failed: %s", std::strerror(errno));
    }
    if (::sigaction(SIGINT, &sa, &s_oldSigInt) != 0) {
        qCWarning(lcCore, "UnixSignalQuitter: sigaction SIGINT failed: %s", std::strerror(errno));
    }
    if (::sigaction(SIGHUP, &sa, &s_oldSigHup) != 0) {
        qCWarning(lcCore, "UnixSignalQuitter: sigaction SIGHUP failed: %s", std::strerror(errno));
    }
    s_handlersInstalled = true;

    m_notifier = new QSocketNotifier(std::get<0>(s_sigFd), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &UnixSignalQuitter::onActivated);
}

UnixSignalQuitter::~UnixSignalQuitter()
{
    if (s_handlersInstalled) {
        ::sigaction(SIGTERM, &s_oldSigTerm, nullptr);
        ::sigaction(SIGINT, &s_oldSigInt, nullptr);
        ::sigaction(SIGHUP, &s_oldSigHup, nullptr);
        s_handlersInstalled = false;
    }

    if (m_notifier != nullptr) {
        m_notifier->setEnabled(false);
    }

    if (std::get<0>(s_sigFd) != -1) {
        ::close(std::get<0>(s_sigFd));
        std::get<0>(s_sigFd) = -1;
    }
    if (std::get<1>(s_sigFd) != -1) {
        ::close(std::get<1>(s_sigFd));
        std::get<1>(s_sigFd) = -1;
    }
}

void UnixSignalQuitter::onActivated()
{
    if (m_notifier != nullptr) {
        m_notifier->setEnabled(false);
    }

    char sigByte = 0;
    while (::read(std::get<0>(s_sigFd), &sigByte, sizeof(sigByte)) > 0) {
        // Drain socket
    }

    qCInfo(lcCore, "Received signal %d, initiating graceful shutdown", static_cast<int>(sigByte));
    QCoreApplication::quit();
}

} // namespace linernotes::core

#endif // Q_OS_UNIX
