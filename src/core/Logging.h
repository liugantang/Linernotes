#pragma once

#include <QDateTime>
#include <QLoggingCategory>
#include <QString>
#include <QtLogging>

namespace aimusic::core {

Q_DECLARE_LOGGING_CATEGORY(lcCore)

struct LogConfig {
    QString directory;
    QString baseName = QStringLiteral("aimusic");
    qint64 maxFileBytes = static_cast<qint64>(5) * 1024 * 1024;
    int maxBackupFiles = 5;
    bool writeToConsole = true;
    QtMsgType minimumLevel = QtDebugMsg;
};

class LogSink;

/// 安装全局日志处理器（qInstallMessageHandler）。可重复调用，后一次覆盖前一次配置。
/// 线程安全：可在任意线程调用 qDebug/qCInfo 等。
void installLogging(const LogConfig &config);

/// 卸载处理器，恢复 Qt 默认处理器，并关闭文件。主要用于测试和退出。
void uninstallLogging();

/// 把一条日志格式化为一行文本（不含换行符）。暴露出来便于测试。
/// 格式：2026-09-25T14:03:12.345+08:00 [I] aimusic.core: message  (file.cpp:42)
/// 级别缩写：D I W C F；Release 构建中 QMessageLogContext 无文件信息时省略括号部分。
QString formatLogLine(QtMsgType type, const QMessageLogContext &context, const QString &message,
    const QDateTime &timestamp);

} // namespace aimusic::core
