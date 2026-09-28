// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TagEditorModel.h"

#include "Format.h"
#include "UiLogging.h"

#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QSet>

#include <array>
#include <iterator>
#include <utility>

namespace linernotes::ui {

namespace {

QString labelForField(library::TagField field)
{
    return formatTagField(field);
}

} // namespace

TagEditorModel::TagEditorModel(library::Database &db, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(db)
{
    const std::array<library::TagField, 11> fields = {
        library::TagField::Title,
        library::TagField::Artist,
        library::TagField::Album,
        library::TagField::AlbumArtist,
        library::TagField::Genre,
        library::TagField::Composer,
        library::TagField::Year,
        library::TagField::TrackNumber,
        library::TagField::TrackTotal,
        library::TagField::DiscNumber,
        library::TagField::DiscTotal,
    };
    m_rows.reserve(fields.size());
    for (const auto field : fields) {
        m_rows.append(FieldRow {
            .field = field,
            .value = QString(),
            .mixed = false,
            .overridden = false,
            .edited = false,
            .reverted = false,
            .editable = true,
        });
    }
}

int TagEditorModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_rows.size());
}

QVariant TagEditorModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return { };
    }
    const auto &item = m_rows.at(index.row());
    switch (role) {
    case FieldRole:
        return QVariant::fromValue(item.field);
    case LabelRole:
    case Qt::DisplayRole:
        return labelForField(item.field);
    case ValueRole:
        return item.value;
    case MixedRole:
        return item.mixed;
    case OverriddenRole:
        return item.overridden;
    case EditedRole:
        return item.edited;
    case EditableRole:
        return item.editable;
    default:
        return { };
    }
}

QHash<int, QByteArray> TagEditorModel::roleNames() const
{
    return {
        { FieldRole, "field" },
        { LabelRole, "label" },
        { ValueRole, "value" },
        { MixedRole, "mixed" },
        { OverriddenRole, "overridden" },
        { EditedRole, "edited" },
        { EditableRole, "editable" },
    };
}

int TagEditorModel::trackCount() const
{
    return static_cast<int>(m_trackIds.size());
}

QString TagEditorModel::errorText() const
{
    return m_errorText;
}

bool TagEditorModel::load(const QList<qint64> &trackIds)
{
    m_trackIds = trackIds;
    m_errorText.clear();
    emit errorTextChanged();

    if (m_trackIds.isEmpty()) {
        resetRows();
        if (!m_rows.isEmpty()) {
            emit dataChanged(index(0, 0), index(static_cast<int>(m_rows.size()) - 1, 0));
        }
        emit loaded();
        return false;
    }

    QList<QHash<library::TagField, QString>> trackValues;
    trackValues.reserve(m_trackIds.size());
    QList<QSet<library::TagField>> trackOverrides;
    trackOverrides.reserve(m_trackIds.size());

    for (const qint64 trackId : m_trackIds) {
        auto valRes = m_store.effectiveValues(trackId);
        if (!valRes.ok()) {
            qCWarning(lcUi, "Failed to fetch effective values for track %lld: %s", trackId,
                qPrintable(valRes.error().toString()));
            m_errorText = valRes.error().message;
            emit errorTextChanged();
            return false;
        }
        trackValues.append(valRes.value());

        auto ovrRes = m_store.overriddenFields(trackId);
        if (!ovrRes.ok()) {
            qCWarning(lcUi, "Failed to fetch overridden fields for track %lld: %s", trackId,
                qPrintable(ovrRes.error().toString()));
            m_errorText = ovrRes.error().message;
            emit errorTextChanged();
            return false;
        }
        trackOverrides.append(ovrRes.value());
    }

    updateRows(trackValues, trackOverrides);

    if (!m_rows.isEmpty()) {
        emit dataChanged(index(0, 0), index(static_cast<int>(m_rows.size()) - 1, 0));
    }
    emit loaded();
    return true;
}

void TagEditorModel::resetRows()
{
    for (auto &row : m_rows) {
        row.value.clear();
        row.mixed = false;
        row.overridden = false;
        row.edited = false;
        row.reverted = false;
        row.editable = true;
    }
}

void TagEditorModel::updateRows(const QList<QHash<library::TagField, QString>> &trackValues,
    const QList<QSet<library::TagField>> &trackOverrides)
{
    const bool multi = m_trackIds.size() > 1;

    for (auto &row : m_rows) {
        row.edited = false;
        row.reverted = false;
        row.editable = !multi
            || (row.field != library::TagField::Title
                && row.field != library::TagField::TrackNumber);

        row.overridden = false;
        for (const auto &ovrSet : trackOverrides) {
            if (ovrSet.contains(row.field)) {
                row.overridden = true;
                break;
            }
        }

        if (m_trackIds.size() == 1) {
            row.mixed = false;
            row.value = trackValues.first().value(row.field);
        } else {
            const QString firstVal = trackValues.first().value(row.field);
            bool isMixed = false;
            for (int i = 1; i < trackValues.size(); ++i) {
                if (trackValues.at(i).value(row.field) != firstVal) {
                    isMixed = true;
                    break;
                }
            }
            row.mixed = isMixed;
            row.value = isMixed ? QString() : firstVal;
        }
    }
}

void TagEditorModel::setValue(int row, const QString &value)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    auto &item = *std::next(m_rows.begin(), row);
    if (!item.editable) {
        return;
    }
    if (item.value == value && item.edited && !item.reverted) {
        return;
    }
    item.value = value;
    item.edited = true;
    item.reverted = false;
    item.mixed = false;
    emit dataChanged(index(row, 0), index(row, 0), { ValueRole, MixedRole, EditedRole });
}

void TagEditorModel::revertField(int row)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return;
    }
    auto &item = *std::next(m_rows.begin(), row);
    if (!item.editable) {
        return;
    }
    item.reverted = true;
    item.edited = true;
    item.mixed = false;
    emit dataChanged(index(row, 0), index(row, 0), { ValueRole, MixedRole, EditedRole });
}

bool TagEditorModel::save()
{
    if (m_trackIds.isEmpty()) {
        m_errorText.clear();
        emit errorTextChanged();
        emit saved();
        return true;
    }

    QList<library::TagEdit> edits;
    for (const auto &item : m_rows) {
        if (!item.edited) {
            continue;
        }
        if (item.reverted) {
            edits.append(library::TagEdit { .field = item.field, .value = std::nullopt });
        } else {
            edits.append(library::TagEdit { .field = item.field, .value = item.value });
        }
    }

    if (edits.isEmpty()) {
        m_errorText.clear();
        emit errorTextChanged();
        emit saved();
        return true;
    }

    auto res = m_store.apply(m_trackIds, edits);
    if (!res.ok()) {
        m_errorText = res.error().message;
        if (m_errorText.isEmpty()) {
            m_errorText = res.error().toString();
        }
        qCWarning(lcUi, "Failed to save tag overrides: %s", qPrintable(res.error().toString()));
        emit errorTextChanged();
        return false;
    }

    m_errorText.clear();
    emit errorTextChanged();

    load(m_trackIds);

    emit saved();
    return true;
}

} // namespace linernotes::ui
