// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>

#ifdef Q_OS_UNIX

class QSocketNotifier;

namespace linernotes::core {

/// 安装 SIGTERM/SIGINT/SIGHUP 处理：收到后在事件循环里调用 QCoreApplication::quit()。
/// 返回的对象需要在 QCoreApplication 存活期间保持存在。
class UnixSignalQuitter : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(UnixSignalQuitter)

public:
    explicit UnixSignalQuitter(QObject *parent = nullptr);
    ~UnixSignalQuitter() override;

private:
    void onActivated();

    QSocketNotifier *m_notifier = nullptr;
};

} // namespace linernotes::core

#endif // Q_OS_UNIX
