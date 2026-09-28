// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MbLookupJobHandler.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <butler/ButlerLogging.h>
#include <butler/MusicBrainzClient.h>

#include <utility>

namespace linernotes::butler {

MbLookupJobHandler::MbLookupJobHandler(MusicBrainzClient &mbClient)
    : m_mbClient(mbClient)
{
}

QString MbLookupJobHandler::kind() const
{
    return QStringLiteral("butler.mb_lookup");
}

ai::TokenUsage MbLookupJobHandler::estimate(
    const QString & /*itemKey*/, const QJsonObject & /*params*/) const
{
    return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
}

std::unique_ptr<QObject> MbLookupJobHandler::process(const QString &itemKey,
    const QJsonObject & /*params*/, std::function<void(const core::Result<void> &)> done)
{
    const QJsonDocument doc = QJsonDocument::fromJson(itemKey.toUtf8());
    if (!doc.isObject()) {
        done({ });
        return nullptr;
    }

    const QString name = doc.object().value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        done({ });
        return nullptr;
    }

    auto searchTask = m_mbClient.searchArtist(name);
    auto *taskPtr = searchTask.get();

    QObject::connect(
        taskPtr, &MbSearchTask::finished, taskPtr, [name, taskPtr, done = std::move(done)]() {
            const auto &res = taskPtr->result();
            if (!res.ok()) {
                qCWarning(lcButler)
                    << "MusicBrainz lookup failed for" << name << ":" << res.error().toString();
            }
            done({ });
        });

    return searchTask;
}

} // namespace linernotes::butler
