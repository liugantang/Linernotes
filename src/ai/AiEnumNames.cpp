// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QMetaEnum>

#include <ai/AiEnumNames.h>

namespace linernotes::ai {

namespace {

template <typename E, typename NameFunc>
std::optional<E> enumFromName(QStringView name, NameFunc nameOf)
{
    const QMetaEnum meta = QMetaEnum::fromType<E>();
    for (int i = 0; i < meta.keyCount(); ++i) {
        const auto value = static_cast<E>(meta.value(i));
        if (nameOf(value) == name) {
            return value;
        }
    }
    return std::nullopt;
}

} // namespace

QString purposeName(Purpose purpose)
{
    switch (purpose) {
    case Purpose::Cleanup:
        return QStringLiteral("cleanup");
    case Purpose::Query:
        return QStringLiteral("query");
    case Purpose::Dj:
        return QStringLiteral("dj");
    case Purpose::Guide:
        return QStringLiteral("guide");
    case Purpose::Narrative:
        return QStringLiteral("narrative");
    }
    return { };
}

std::optional<Purpose> purposeFromName(QStringView name)
{
    return enumFromName<Purpose>(name, purposeName);
}

QString dataCategoryName(DataCategory category)
{
    switch (category) {
    case DataCategory::PlayHistory:
        return QStringLiteral("play_history");
    case DataCategory::Moments:
        return QStringLiteral("moments");
    case DataCategory::Location:
        return QStringLiteral("location");
    }
    return { };
}

std::optional<DataCategory> dataCategoryFromName(QStringView name)
{
    return enumFromName<DataCategory>(name, dataCategoryName);
}

QString jobStateName(JobState state)
{
    switch (state) {
    case JobState::Running:
        return QStringLiteral("running");
    case JobState::Paused:
        return QStringLiteral("paused");
    case JobState::Completed:
        return QStringLiteral("completed");
    case JobState::Cancelled:
        return QStringLiteral("cancelled");
    }
    return { };
}

std::optional<JobState> jobStateFromName(QStringView name)
{
    return enumFromName<JobState>(name, jobStateName);
}

} // namespace linernotes::ai
