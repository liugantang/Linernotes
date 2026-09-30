// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <butler/TranslationStore.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/Errors.h>

namespace linernotes::butler {

TranslationStore::TranslationStore(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<QHash<QString, QString>> TranslationStore::lookup(
    const QStringList &texts, const QString &targetLang) const
{
    if (texts.isEmpty()) {
        return QHash<QString, QString>();
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSet<QString> uniqueTexts;
    for (const auto &text : texts) {
        if (!text.isEmpty()) {
            uniqueTexts.insert(text);
        }
    }

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT translated FROM text_translations "
                             "WHERE source_text = ? AND target_lang = ? AND translated != ''"));

    QHash<QString, QString> results;
    for (const auto &text : uniqueTexts) {
        q.bindValue(0, text);
        q.bindValue(1, targetLang);
        if (!q.exec()) {
            return core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = q.lastError().text(),
                .detail = text,
            };
        }
        if (q.next()) {
            const QString translated = q.value(0).toString();
            if (!translated.isEmpty()) {
                results.insert(text, translated);
            }
        }
    }

    return results;
}

core::Result<void> TranslationStore::save(const QHash<QString, QString> &translations,
    const QString &model, int promptVersion, const QString &targetLang) const
{
    if (translations.isEmpty()) {
        return { };
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    const qint64 now = m_clock.nowMs();

    library::Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(library::errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for translation save"),
            .detail = QString(),
        };
    }

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO text_translations "
        "(source_text, target_lang, translated, model, prompt_version, translated_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"));

    for (auto it = translations.cbegin(); it != translations.cend(); ++it) {
        const QString &sourceText = it.key();
        const QString &translated = it.value();

        q.bindValue(0, sourceText);
        q.bindValue(1, targetLang);
        q.bindValue(2, translated);
        q.bindValue(3, model);
        q.bindValue(4, promptVersion);
        q.bindValue(5, now);

        if (!q.exec()) {
            return core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = q.lastError().text(),
                .detail = sourceText,
            };
        }
    }

    return tx.commit();
}

} // namespace linernotes::butler
