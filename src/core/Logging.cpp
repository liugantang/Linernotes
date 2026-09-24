#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QString>
#include <QtLogging>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

namespace aimusic::core {

Q_LOGGING_CATEGORY(lcCore, "aimusic.core")

namespace {

constexpr int levelSeverity(QtMsgType type) noexcept
{
    switch (type) {
    case QtDebugMsg:
        return 0;
    case QtInfoMsg:
        return 1;
    case QtWarningMsg:
        return 2;
    case QtCriticalMsg:
        return 3;
    case QtFatalMsg:
        return 4;
    }
    return 0;
}

} // namespace

QString formatLogLine(QtMsgType type, const QMessageLogContext &context, const QString &message,
    const QDateTime &timestamp)
{
    char levelChar = 'D';
    switch (type) {
    case QtDebugMsg:
        levelChar = 'D';
        break;
    case QtInfoMsg:
        levelChar = 'I';
        break;
    case QtWarningMsg:
        levelChar = 'W';
        break;
    case QtCriticalMsg:
        levelChar = 'C';
        break;
    case QtFatalMsg:
        levelChar = 'F';
        break;
    }

    const QString timeStr = timestamp.toString(Qt::ISODateWithMs);
    const std::string_view categoryView
        = (context.category != nullptr) ? std::string_view(context.category) : std::string_view();
    const std::string_view category
        = categoryView.empty() ? std::string_view("default") : categoryView;

    QString line;
    line.reserve(
        timeStr.size() + 16 + static_cast<qsizetype>(category.size()) + message.size() + 32);
    line.append(timeStr);
    line.append(QStringLiteral(" ["));
    line.append(QChar::fromLatin1(levelChar));
    line.append(QStringLiteral("] "));
    line.append(QString::fromUtf8(category.data(), static_cast<qsizetype>(category.size())));
    line.append(QStringLiteral(": "));
    line.append(message);

    if (context.file != nullptr) {
        const std::string_view fileView(context.file);
        if (!fileView.empty()) {
            const auto lastSlash = fileView.find_last_of("/\\");
            const std::string_view fileName
                = (lastSlash != std::string_view::npos) ? fileView.substr(lastSlash + 1) : fileView;
            if (!fileName.empty()) {
                line.append(QStringLiteral("  ("));
                line.append(
                    QString::fromUtf8(fileName.data(), static_cast<qsizetype>(fileName.size())));
                line.append(u':');
                line.append(QString::number(context.line));
                line.append(u')');
            }
        }
    }

    return line;
}

class LogSink {
public:
    explicit LogSink(LogConfig config);
    ~LogSink();

    LogSink(const LogSink &) = delete;
    LogSink &operator=(const LogSink &) = delete;
    LogSink(LogSink &&) = delete;
    LogSink &operator=(LogSink &&) = delete;

    void reconfigure(LogConfig config);
    void log(QtMsgType type, const QMessageLogContext &context, const QString &message);

private:
    void ensureFileOpen();
    void rollFiles();
    void reportFileError(const QString &msg);
    [[nodiscard]] QString rolledFilePath(int index) const;
    [[nodiscard]] QString baseFilePath() const;

    LogConfig m_config;
    QFile m_file;
    bool m_fileErrorWarned = false;
};

LogSink::LogSink(LogConfig config)
    : m_config(std::move(config))
{
}

LogSink::~LogSink()
{
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
}

void LogSink::reconfigure(LogConfig config)
{
    const bool fileTargetChanged
        = (m_config.directory != config.directory || m_config.baseName != config.baseName);
    m_config = std::move(config);
    m_fileErrorWarned = false;
    if (fileTargetChanged && m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
}

QString LogSink::baseFilePath() const
{
    return QDir(m_config.directory).filePath(m_config.baseName + QStringLiteral(".log"));
}

QString LogSink::rolledFilePath(int index) const
{
    return QDir(m_config.directory)
        .filePath(QStringLiteral("%1.%2.log").arg(m_config.baseName).arg(index));
}

void LogSink::reportFileError(const QString &msg)
{
    if (!m_fileErrorWarned) {
        m_fileErrorWarned = true;
        const QByteArray utf8 = msg.toUtf8();
        std::fprintf(stderr, "[Logging] Warning: %s\n", utf8.constData());
        std::fflush(stderr);
    }
}

void LogSink::ensureFileOpen()
{
    if (m_file.isOpen()) {
        return;
    }
    if (m_config.directory.isEmpty()) {
        return;
    }

    const QDir dir(m_config.directory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        reportFileError(
            QStringLiteral("Failed to create log directory: %1").arg(m_config.directory));
        return;
    }

    const QString path = baseFilePath();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        reportFileError(
            QStringLiteral("Failed to open log file %1: %2").arg(path, m_file.errorString()));
        return;
    }
}

void LogSink::rollFiles()
{
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }

    const QString baseFile = baseFilePath();

    if (m_config.maxBackupFiles > 0) {
        const QString oldestBackup = rolledFilePath(m_config.maxBackupFiles);
        if (QFile::exists(oldestBackup)) {
            QFile::remove(oldestBackup);
        }

        for (int i = m_config.maxBackupFiles - 1; i >= 1; --i) {
            const QString src = rolledFilePath(i);
            const QString dst = rolledFilePath(i + 1);
            if (QFile::exists(src)) {
                if (QFile::exists(dst)) {
                    QFile::remove(dst);
                }
                QFile::rename(src, dst);
            }
        }

        const QString firstBackup = rolledFilePath(1);
        if (QFile::exists(firstBackup)) {
            QFile::remove(firstBackup);
        }
        QFile::rename(baseFile, firstBackup);
    } else {
        if (QFile::exists(baseFile)) {
            QFile::remove(baseFile);
        }
    }

    m_file.setFileName(baseFile);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        reportFileError(
            QStringLiteral("Failed to reopen log file %1: %2").arg(baseFile, m_file.errorString()));
    }
}

void LogSink::log(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (levelSeverity(type) < levelSeverity(m_config.minimumLevel)) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString line = formatLogLine(type, context, message, now);

    if (m_config.writeToConsole) {
        const QByteArray utf8 = line.toUtf8();
        std::fputs(utf8.constData(), stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }

    if (!m_config.directory.isEmpty()) {
        ensureFileOpen();
        if (m_file.isOpen()) {
            if (m_config.maxFileBytes > 0 && m_file.pos() >= m_config.maxFileBytes) {
                rollFiles();
            }
            if (m_file.isOpen()) {
                const QByteArray utf8 = line.toUtf8();
                m_file.write(utf8);
                m_file.write("\n");
                if (levelSeverity(type) >= levelSeverity(QtWarningMsg)) {
                    m_file.flush();
                }
            }
        }
    }
}

namespace {

struct LoggingState {
    std::atomic<QtMsgType> minimumLevel { QtDebugMsg };
    std::mutex sinkMutex;
    std::unique_ptr<LogSink> sink;
    std::atomic<QLoggingCategory::CategoryFilter> previousFilter { nullptr };
    std::atomic<bool> filterInstalled { false };
};

LoggingState &globalLoggingState()
{
    static LoggingState s_state;
    return s_state;
}

void categoryFilter(QLoggingCategory *category)
{
    const LoggingState &state = globalLoggingState();
    const auto prevFilter = state.previousFilter.load(std::memory_order_relaxed);
    if (prevFilter != nullptr && prevFilter != &categoryFilter) {
        prevFilter(category);
    }

    const QtMsgType minLevel = state.minimumLevel.load(std::memory_order_relaxed);
    const int minSeverity = levelSeverity(minLevel);

    if (levelSeverity(QtDebugMsg) < minSeverity) {
        category->setEnabled(QtDebugMsg, false);
    }
    if (levelSeverity(QtInfoMsg) < minSeverity) {
        category->setEnabled(QtInfoMsg, false);
    }
    if (levelSeverity(QtWarningMsg) < minSeverity) {
        category->setEnabled(QtWarningMsg, false);
    }
    if (levelSeverity(QtCriticalMsg) < minSeverity) {
        category->setEnabled(QtCriticalMsg, false);
    }
}

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // Reentrancy guard: if logging inside customMessageHandler (e.g. from QFile/QDir internals
    // or standard library operations), write directly to stderr without acquiring sinkMutex
    // to avoid deadlocking the non-recursive mutex.
    thread_local bool s_inLoggingHandler = false;
    if (s_inLoggingHandler) {
        const QDateTime now = QDateTime::currentDateTime();
        const QString line = formatLogLine(type, context, message, now);
        const QByteArray utf8 = line.toUtf8();
        std::fputs(utf8.constData(), stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
        if (type == QtFatalMsg) {
            std::abort();
        }
        return;
    }

    struct ReentrancyGuard {
        ReentrancyGuard() noexcept { s_inLoggingHandler = true; }
        ~ReentrancyGuard() noexcept { s_inLoggingHandler = false; }
        ReentrancyGuard(const ReentrancyGuard &) = delete;
        ReentrancyGuard &operator=(const ReentrancyGuard &) = delete;
        ReentrancyGuard(ReentrancyGuard &&) = delete;
        ReentrancyGuard &operator=(ReentrancyGuard &&) = delete;
    } const guard;

    LoggingState &state = globalLoggingState();
    std::unique_lock<std::mutex> lock(state.sinkMutex);
    if (!state.sink) {
        return;
    }
    state.sink->log(type, context, message);
    if (type == QtFatalMsg) {
        lock.unlock();
        std::abort();
    }
}

} // namespace

void installLogging(const LogConfig &config)
{
    LoggingState &state = globalLoggingState();
    const std::scoped_lock lock(state.sinkMutex);
    state.minimumLevel.store(config.minimumLevel, std::memory_order_relaxed);

    if (!state.filterInstalled.load(std::memory_order_relaxed)) {
        const auto oldFilter = QLoggingCategory::installFilter(categoryFilter);
        state.previousFilter.store(oldFilter, std::memory_order_relaxed);
        state.filterInstalled.store(true, std::memory_order_relaxed);
    } else {
        // Re-run filter on all categories with the updated minimumLevel
        QLoggingCategory::installFilter(categoryFilter);
    }

    if (!state.sink) {
        state.sink = std::make_unique<LogSink>(config);
        qInstallMessageHandler(customMessageHandler);
    } else {
        state.sink->reconfigure(config);
    }
}

void uninstallLogging()
{
    LoggingState &state = globalLoggingState();
    const std::scoped_lock lock(state.sinkMutex);
    if (state.filterInstalled.load(std::memory_order_relaxed)) {
        const auto prevFilter = state.previousFilter.load(std::memory_order_relaxed);
        QLoggingCategory::installFilter(prevFilter);
        state.previousFilter.store(nullptr, std::memory_order_relaxed);
        state.filterInstalled.store(false, std::memory_order_relaxed);
    }
    if (state.sink) {
        qInstallMessageHandler(nullptr);
        state.sink.reset();
    }
}

} // namespace aimusic::core
