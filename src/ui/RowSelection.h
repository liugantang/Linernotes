// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QSet>
#include <Qt>

namespace linernotes::ui {

class RowSelection : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(RowSelection)

    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit RowSelection(QObject *parent = nullptr);
    ~RowSelection() override = default;

    [[nodiscard]] int count() const;
    [[nodiscard]] int currentRow() const;
    [[nodiscard]] int revision() const;

    /// 普通点击：只选中 row；Ctrl：切换 row；Shift：从锚点到 row
    /// 的区间（Ctrl+Shift：把区间并入现有选择）。 参数 modifiers 直接传 QML 的 mouse.modifiers /
    /// event.modifiers。
    Q_INVOKABLE void select(int row, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    Q_INVOKABLE void selectAll(int rowCount);
    Q_INVOKABLE void clear();
    [[nodiscard]] Q_INVOKABLE bool isSelected(int row) const;
    [[nodiscard]] Q_INVOKABLE QList<int> selectedRows() const; // 升序

signals:
    void changed();

private:
    QSet<int> m_selected;
    int m_currentRow { -1 };
    int m_anchor { -1 };
    int m_revision { 0 };
};

} // namespace linernotes::ui
