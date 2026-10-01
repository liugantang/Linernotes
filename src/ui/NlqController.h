// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <Qt>

#include <ai/AiConfig.h>
#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <core/Result.h>
#include <library/Database.h>
#include <library/SmartRule.h>
#include <nlq/EntityResolver.h>
#include <nlq/Interpreter.h>
#include <nlq/NlqQuery.h>
#include <nlq/Relaxation.h>
#include <ui/LibraryActions.h>
#include <ui/PlaylistController.h>
#include <ui/SettingsController.h>

#include <cstdint>
#include <optional>

namespace linernotes::nlq {
class QueryRunner;
struct OfflineParse;
} // namespace linernotes::nlq

namespace linernotes::ui {

class NlqController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NlqController)

public:
    enum class State : std::uint8_t {
        Idle,
        Interpreting,
        NeedsChoice,
        Ready,
        Failed,
    };
    Q_ENUM(State)

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool llmConfigured READ isLlmConfigured NOTIFY llmConfiguredChanged)
    Q_PROPERTY(bool offline READ isOffline NOTIFY offlineChanged)
    Q_PROPERTY(QString explanation READ explanation NOTIFY explanationChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(linernotes::nlq::Entity entity READ entity NOTIFY entityChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(QStringList chips READ chips NOTIFY chipsChanged)
    Q_PROPERTY(QVariantMap clarification READ clarification NOTIFY clarificationChanged)
    Q_PROPERTY(bool hasConversation READ hasConversation NOTIFY hasConversationChanged)
    Q_PROPERTY(QString emptyHint READ emptyHint NOTIFY emptyHintChanged)
    Q_PROPERTY(QVariantList relaxations READ relaxations NOTIFY relaxationsChanged)

    NlqController(library::Database &db, ai::LlmService &llm, ai::PromptLibrary &prompts,
        const ai::AiConfig &aiConfig, LibraryActions &actions, PlaylistController &playlists,
        SettingsController &settingsController, QObject *parent = nullptr);
    ~NlqController() override = default;

    [[nodiscard]] State state() const;
    [[nodiscard]] bool isLlmConfigured() const;
    [[nodiscard]] bool isOffline() const;
    [[nodiscard]] QString explanation() const;
    [[nodiscard]] QString errorText() const;
    [[nodiscard]] nlq::Entity entity() const;
    [[nodiscard]] QVariantList rows() const;
    [[nodiscard]] QStringList chips() const;
    [[nodiscard]] QVariantMap clarification() const;
    [[nodiscard]] bool hasConversation() const;
    [[nodiscard]] QString emptyHint() const;
    [[nodiscard]] QVariantList relaxations() const;

    Q_INVOKABLE void submit(const QString &text);
    Q_INVOKABLE void chooseCandidate(int index);
    Q_INVOKABLE void newConversation();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void applyRelaxation(int index);

    Q_INVOKABLE void playAll();
    Q_INVOKABLE void enqueueAll();
    Q_INVOKABLE void playRow(int index);
    Q_INVOKABLE void saveAsPlaylist(const QString &name);

    void invalidateSummaryCache();

signals:
    void stateChanged();
    void llmConfiguredChanged();
    void offlineChanged();
    void explanationChanged();
    void errorTextChanged();
    void entityChanged();
    void rowsChanged();
    void chipsChanged();
    void clarificationChanged();
    void hasConversationChanged();
    void emptyHintChanged();
    void relaxationsChanged();

public slots:
    void refreshLlmConfigured();

private slots:
    void onInterpreterFinished();

private:
    void executeResolved(const nlq::Query &query);
    void executeQuery(const nlq::Query &query);
    void failWith(const core::Error &error);
    [[nodiscard]] QVariantList loadRows(const QList<qint64> &ids, nlq::Entity entity) const;
    void updateEmptyAnalysis(const nlq::QueryRunner &runner, const nlq::Query &query);
    [[nodiscard]] QString relaxationText(const nlq::Relaxation &rel, const nlq::Query &query) const;
    [[nodiscard]] QString chooseEmptyHint(
        const nlq::EmptyResultAnalysis &analysis, const nlq::Query &query) const;
    void updateClarificationProperty();
    [[nodiscard]] QList<qint64> collectAllTrackIds() const;

    void submitOffline(const QString &text);
    void handleOfflineLeftover(const QString &text, nlq::OfflineParse &parse);
    void handleOfflineSingleArtist(nlq::OfflineParse &parse, const nlq::ArtistCandidate &candidate);
    void handleOfflineDisambiguation(
        nlq::OfflineParse &parse, QList<nlq::ArtistCandidate> candidates);
    void handleOfflineKeywordSearch(const QString &text);
    void handleOfflineIgnoredLeftover(const nlq::OfflineParse &parse);

    library::Database &m_db;
    const ai::AiConfig &m_aiConfig;
    LibraryActions &m_actions;
    PlaylistController &m_playlists;
    SettingsController &m_settingsController;

    nlq::Interpreter m_interpreter;

    State m_state = State::Idle;
    bool m_llmConfigured = false;
    QString m_explanation;
    QString m_errorText;
    nlq::Entity m_entity = nlq::Entity::Track;
    QVariantList m_rows;
    QStringList m_chips;
    QVariantMap m_clarification;
    QString m_emptyHint;
    QVariantList m_relaxations;
    QList<nlq::Relaxation> m_lastRelaxations;

    std::optional<nlq::Query> m_currentQuery;
    nlq::Resolution m_resolution;
    QList<nlq::Clarification> m_pendingClarifications;

    std::optional<QDate> m_cachedSummaryDate;
    QString m_cachedSummaryText;
};

} // namespace linernotes::ui
