// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <ui/RowSelection.h>

#include <algorithm>

namespace linernotes::ui {

RowSelection::RowSelection(QObject *parent)
    : QObject(parent)
{
}

int RowSelection::count() const
{
    return static_cast<int>(m_selected.size());
}

int RowSelection::currentRow() const
{
    return m_currentRow;
}

int RowSelection::revision() const
{
    return m_revision;
}

void RowSelection::select(int row, Qt::KeyboardModifiers modifiers)
{
    if (row < 0) {
        return;
    }

    const bool isShift = modifiers.testFlag(Qt::ShiftModifier);
    const bool isCtrl = modifiers.testFlag(Qt::ControlModifier);

    if (isShift && isCtrl) {
        if (m_anchor < 0) {
            m_anchor = row;
            m_selected.insert(row);
        } else {
            const int start = std::min(m_anchor, row);
            const int end = std::max(m_anchor, row);
            for (int i = start; i <= end; ++i) {
                m_selected.insert(i);
            }
        }
        m_currentRow = row;
    } else if (isShift) {
        m_selected.clear();
        if (m_anchor < 0) {
            m_anchor = row;
            m_selected.insert(row);
        } else {
            const int start = std::min(m_anchor, row);
            const int end = std::max(m_anchor, row);
            for (int i = start; i <= end; ++i) {
                m_selected.insert(i);
            }
        }
        m_currentRow = row;
    } else if (isCtrl) {
        if (m_selected.contains(row)) {
            m_selected.remove(row);
        } else {
            m_selected.insert(row);
        }
        m_anchor = row;
        m_currentRow = row;
    } else {
        m_selected.clear();
        m_selected.insert(row);
        m_anchor = row;
        m_currentRow = row;
    }

    ++m_revision;
    emit changed();
}

void RowSelection::selectAll(int rowCount)
{
    m_selected.clear();
    if (rowCount > 0) {
        m_selected.reserve(rowCount);
        for (int i = 0; i < rowCount; ++i) {
            m_selected.insert(i);
        }
    }
    ++m_revision;
    emit changed();
}

void RowSelection::clear()
{
    m_selected.clear();
    m_currentRow = -1;
    m_anchor = -1;
    ++m_revision;
    emit changed();
}

bool RowSelection::isSelected(int row) const
{
    return m_selected.contains(row);
}

QList<int> RowSelection::selectedRows() const
{
    QList<int> list = m_selected.values();
    std::ranges::sort(list);
    return list;
}

} // namespace linernotes::ui
