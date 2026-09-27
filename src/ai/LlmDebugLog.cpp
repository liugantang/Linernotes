// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LlmDebugLog.h"

#include <core/Settings.h>

namespace linernotes::ai {

namespace {

inline const core::SettingKey<bool> kLlmDebugKey { u"developer/llmDebug", false };
constexpr int kMaxDebugEntries = 200;

} // namespace

LlmDebugLog::LlmDebugLog(core::Settings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

bool LlmDebugLog::isEnabled() const
{
    return m_settings.value(kLlmDebugKey);
}

void LlmDebugLog::setEnabled(bool enabled)
{
    if (isEnabled() == enabled) {
        return;
    }
    m_settings.setValue(kLlmDebugKey, enabled);
    if (!enabled) {
        clear();
    }
    emit enabledChanged();
}

void LlmDebugLog::add(LlmDebugEntry entry)
{
    if (!isEnabled()) {
        return;
    }
    entry.id = ++m_nextId;
    if (m_entries.size() >= kMaxDebugEntries) {
        m_entries.removeFirst();
    }
    const quint64 id = entry.id;
    m_entries.append(std::move(entry));
    emit entryAdded(id);
}

const QList<LlmDebugEntry> &LlmDebugLog::entries() const
{
    return m_entries;
}

std::optional<LlmDebugEntry> LlmDebugLog::entry(quint64 id) const
{
    for (const auto &e : m_entries) {
        if (e.id == id) {
            return e;
        }
    }
    return std::nullopt;
}

void LlmDebugLog::clear()
{
    m_entries.clear();
    emit cleared();
}

} // namespace linernotes::ai
