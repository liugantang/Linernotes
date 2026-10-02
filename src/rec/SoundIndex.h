// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QtGlobal>

#include <audio/EmbeddingIndex.h>
#include <core/Result.h>

#include <memory>

namespace linernotes::core {
class Clock;
} // namespace linernotes::core

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::rec {

class SoundIndex {
    Q_DISABLE_COPY_MOVE(SoundIndex)

public:
    SoundIndex(library::Database &db, const core::Clock &clock);
    ~SoundIndex() = default;

    // 懒加载；EmbeddingStore::analyzedCount 变化时重载。失败返回错误。
    // 返回的指针在下次调用 index() 前有效。
    [[nodiscard]] core::Result<const audio::EmbeddingIndex *> index();

private:
    library::Database &m_db;
    const core::Clock &m_clock;
    std::unique_ptr<audio::EmbeddingIndex> m_index;
    int m_indexedCount = -1;
};

} // namespace linernotes::rec
