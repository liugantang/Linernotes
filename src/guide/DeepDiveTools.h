// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <ai/ChatTypes.h>
#include <core/Clock.h>
#include <guide/GuideEnums.h>
#include <guide/Knowledge.h>
#include <guide/Lyrics.h>
#include <library/Database.h>
#include <library/LibrarySearch.h>

namespace linernotes::guide {

class KnowledgeService;
class LyricsService;

struct DeepDiveSource {
    QString label { };
    QUrl url { };
    bool operator==(const DeepDiveSource &) const = default;
};

using DeepDiveSourceList = QList<DeepDiveSource>;

/// Returns the tool specifications for lookup_knowledge, search_library, and get_lyrics.
[[nodiscard]] QList<ai::ToolSpec> deepDiveToolSpecs();

/// Pure helpers for serializing tool results to compact JSON strings.
[[nodiscard]] QString lookupResultJson(const KnowledgeResult &r);
[[nodiscard]] QString searchResultJson(
    const library::SearchResults &r, const QHash<qint64, int> &plays);
[[nodiscard]] QString lyricsResultJson(const LyricsResult &r, const QStringList &translations);

class DeepDiveTools : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DeepDiveTools)

public:
    DeepDiveTools(library::Database &db, KnowledgeService &knowledge, LyricsService &lyrics,
        const core::Clock &clock, QObject *parent = nullptr);
    ~DeepDiveTools() override = default;

    /// 异步执行一次工具调用；result 为给模型看的 JSON 文本（紧凑），出错时也返回 JSON：{"error":
    /// "..."}
    void execute(const QString &callId, const QString &name, const QString &argumentsJson,
        qint64 contextTrackId);

signals:
    void executed(const QString &callId, const QString &resultJson,
        const linernotes::guide::DeepDiveSourceList &sources);

private:
    void onKnowledgeFinished(const KnowledgeResult &result);
    void onLyricsFinished(const LyricsResult &result);

    void executeLookupKnowledge(const QString &callId, const QString &argumentsJson);
    void executeSearchLibrary(const QString &callId, const QString &argumentsJson);
    void executeGetLyrics(const QString &callId, qint64 contextTrackId);

    struct PendingLookup {
        QString callId;
        QString key;
    };

    struct PendingLyrics {
        QString callId;
        qint64 trackId = 0;
    };

    library::Database &m_db;
    KnowledgeService &m_knowledge;
    LyricsService &m_lyrics;
    const core::Clock &m_clock;

    QList<PendingLookup> m_pendingLookups;
    QList<PendingLyrics> m_pendingLyrics;
};

} // namespace linernotes::guide

Q_DECLARE_METATYPE(linernotes::guide::DeepDiveSource)
Q_DECLARE_METATYPE(linernotes::guide::DeepDiveSourceList)
