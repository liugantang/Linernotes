// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>
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
#include <ui/LibraryActions.h>
#include <ui/PlaylistController.h>
#include <ui/SettingsController.h>

#include <cstdint>
#include <optional>

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

    enum class ChipKind : std::uint8_t {
        Entity,
        Condition,
        PlayWindow,
        Sort,
        Limit,
    };
    Q_ENUM(ChipKind)

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool llmConfigured READ isLlmConfigured NOTIFY llmConfiguredChanged)
    Q_PROPERTY(QString explanation READ explanation NOTIFY explanationChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(linernotes::nlq::Entity entity READ entity NOTIFY entityChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(QVariantList chips READ chips NOTIFY chipsChanged)
    Q_PROPERTY(QVariantMap clarification READ clarification NOTIFY clarificationChanged)
    Q_PROPERTY(bool hasConversation READ hasConversation NOTIFY hasConversationChanged)

    NlqController(library::Database &db, ai::LlmService &llm, ai::PromptLibrary &prompts,
        const ai::AiConfig &aiConfig, LibraryActions &actions, PlaylistController &playlists,
        SettingsController &settingsController, QObject *parent = nullptr);
    ~NlqController() override = default;

    [[nodiscard]] State state() const;
    [[nodiscard]] bool isLlmConfigured() const;
    [[nodiscard]] QString explanation() const;
    [[nodiscard]] QString errorText() const;
    [[nodiscard]] nlq::Entity entity() const;
    [[nodiscard]] QVariantList rows() const;
    [[nodiscard]] QVariantList chips() const;
    [[nodiscard]] QVariantMap clarification() const;
    [[nodiscard]] bool hasConversation() const;

    Q_INVOKABLE void submit(const QString &text);
    Q_INVOKABLE void chooseCandidate(int index);
    Q_INVOKABLE void newConversation();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void removeChip(int index);
    [[nodiscard]] Q_INVOKABLE linernotes::library::SmartCondition conditionAt(int index) const;
    Q_INVOKABLE void setCondition(int index, const linernotes::library::SmartCondition &cond);
    Q_INVOKABLE void setPlayWindow(const QString &from, const QString &to);
    Q_INVOKABLE void setSort(linernotes::nlq::SortKey key, Qt::SortOrder order);
    Q_INVOKABLE void setLimit(int limit);
    Q_INVOKABLE void setEntity(linernotes::nlq::Entity entity);

    Q_INVOKABLE void playAll();
    Q_INVOKABLE void enqueueAll();
    Q_INVOKABLE void playRow(int index);
    Q_INVOKABLE void saveAsPlaylist(const QString &name);

    void invalidateSummaryCache();

signals:
    void stateChanged();
    void llmConfiguredChanged();
    void explanationChanged();
    void errorTextChanged();
    void entityChanged();
    void rowsChanged();
    void chipsChanged();
    void clarificationChanged();
    void hasConversationChanged();

public slots:
    void refreshLlmConfigured();

private slots:
    void onInterpreterFinished();

private:
    void executeQuery(const nlq::Query &query);
    void updateClarificationProperty();
    [[nodiscard]] QList<qint64> collectAllTrackIds() const;

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
    QVariantList m_chips;
    QVariantMap m_clarification;

    std::optional<nlq::Query> m_currentQuery;
    nlq::Resolution m_resolution;
    QList<nlq::Clarification> m_pendingClarifications;

    std::optional<QDate> m_cachedSummaryDate;
    QString m_cachedSummaryText;
};

} // namespace linernotes::ui
