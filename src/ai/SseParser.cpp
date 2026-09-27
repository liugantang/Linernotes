// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SseParser.h"

namespace linernotes::ai {

std::optional<QByteArray> SseParser::extractNextLine()
{
    for (qsizetype i = 0; i < m_buffer.size(); ++i) {
        const char c = m_buffer.at(i);
        if (c == '\r') {
            if (i + 1 < m_buffer.size()) {
                if (m_buffer.at(i + 1) == '\n') {
                    QByteArray line = m_buffer.left(i);
                    m_buffer.remove(0, i + 2);
                    return line;
                }
                QByteArray line = m_buffer.left(i);
                m_buffer.remove(0, i + 1);
                return line;
            }
            // '\r' is at the very end of buffer; wait for next chunk
            return std::nullopt;
        }
        if (c == '\n') {
            QByteArray line = m_buffer.left(i);
            m_buffer.remove(0, i + 1);
            return line;
        }
    }
    return std::nullopt;
}

std::optional<SseEvent> SseParser::processLine(const QByteArray &line)
{
    if (line.isEmpty()) {
        if (m_hasData) {
            SseEvent ev {
                .event = std::move(m_currentEvent),
                .data = std::move(m_currentData),
            };
            m_currentEvent.clear();
            m_currentData.clear();
            m_hasData = false;
            return ev;
        }
        m_currentEvent.clear();
        m_currentData.clear();
        m_hasData = false;
        return std::nullopt;
    }

    if (line.startsWith(':')) {
        // Comment line: ignore
        return std::nullopt;
    }

    const qsizetype colonIndex = line.indexOf(':');
    QByteArray field;
    QByteArray value;
    if (colonIndex != -1) {
        field = line.left(colonIndex);
        value = line.mid(colonIndex + 1);
        if (value.startsWith(' ')) {
            value = value.mid(1);
        }
    } else {
        field = line;
    }

    if (field == "data") {
        if (m_hasData) {
            m_currentData.append('\n');
        }
        m_currentData.append(value);
        m_hasData = true;
    } else if (field == "event") {
        m_currentEvent = QString::fromUtf8(value);
    }

    return std::nullopt;
}

QList<SseEvent> SseParser::feed(QByteArrayView chunk)
{
    m_buffer.append(chunk.data(), chunk.size());
    QList<SseEvent> events;

    while (const auto line = extractNextLine()) {
        if (auto ev = processLine(*line)) {
            events.append(std::move(*ev));
        }
    }

    return events;
}

} // namespace linernotes::ai
