// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include <library/Database.h>
#include <library/LibraryEnums.h>
#include <library/OverrideStore.h>

#include <cstdint>

namespace linernotes::ui {

class TagEditorModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(TagEditorModel)

    Q_PROPERTY(int trackCount READ trackCount NOTIFY loaded)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

public:
    enum Role : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt 模型角色需与 int
                                // 互转
        FieldRole = Qt::UserRole + 1,
        LabelRole,
        ValueRole,
        MixedRole,
        OverriddenRole,
        EditedRole,
        EditableRole,
    };
    Q_ENUM(Role)

    explicit TagEditorModel(library::Database &db, QObject *parent = nullptr);
    ~TagEditorModel() override = default;

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int trackCount() const;
    [[nodiscard]] QString errorText() const;

    Q_INVOKABLE bool load(const QList<qint64> &trackIds);
    Q_INVOKABLE void setValue(int row, const QString &value);
    Q_INVOKABLE void revertField(int row);
    Q_INVOKABLE bool save();

signals:
    void loaded();
    void saved();
    void errorTextChanged();

private:
    struct FieldRow {
        library::TagField field;
        QString value;
        bool mixed { false };
        bool overridden { false };
        bool edited { false };
        bool reverted { false };
        bool editable { true };
    };

    void resetRows();
    void updateRows(const QList<QHash<library::TagField, QString>> &trackValues,
        const QList<QSet<library::TagField>> &trackOverrides);

    library::OverrideStore m_store;
    QList<qint64> m_trackIds;
    QList<FieldRow> m_rows;
    QString m_errorText;
};

} // namespace linernotes::ui
