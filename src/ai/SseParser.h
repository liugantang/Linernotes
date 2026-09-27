// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>

#include <optional>

namespace linernotes::ai {

struct SseEvent {
    QString event; // 未指定时为空
    QByteArray data; // 多行 data: 以 '\n' 连接
    bool operator==(const SseEvent &) const = default;
};

class SseParser {
public:
    /// 喂入任意切分的字节块，返回本次凑齐的完整事件（空行结束一个事件）。
    QList<SseEvent> feed(QByteArrayView chunk);

private:
    std::optional<QByteArray> extractNextLine();
    std::optional<SseEvent> processLine(const QByteArray &line);

    QByteArray m_buffer;
    QString m_currentEvent;
    QByteArray m_currentData;
    bool m_hasData = false;
};

} // namespace linernotes::ai
