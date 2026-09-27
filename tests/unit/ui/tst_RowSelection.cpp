// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <ui/RowSelection.h>

using linernotes::ui::RowSelection;

namespace {

class TstRowSelection : public QObject {
    Q_OBJECT

private slots:
    void singleAndCtrlSelection();
    void shiftSelectionWithAnchor();
    void selectAllAndClear();
};

void TstRowSelection::singleAndCtrlSelection()
{
    RowSelection sel;
    QSignalSpy spy(&sel, &RowSelection::changed);

    // Normal click 2 -> {2}
    sel.select(2, Qt::NoModifier);
    QCOMPARE(sel.count(), 1);
    QCOMPARE(sel.selectedRows(), QList<int>({ 2 }));
    QCOMPARE(sel.currentRow(), 2);
    QVERIFY(sel.isSelected(2));
    QCOMPARE(spy.count(), 1);

    // Ctrl click 5 -> {2, 5}
    sel.select(5, Qt::ControlModifier);
    QCOMPARE(sel.count(), 2);
    QCOMPARE(sel.selectedRows(), QList<int>({ 2, 5 }));
    QCOMPARE(sel.currentRow(), 5);
    QVERIFY(sel.isSelected(2));
    QVERIFY(sel.isSelected(5));
    QCOMPARE(spy.count(), 2);

    // Ctrl click 2 again -> {5}
    sel.select(2, Qt::ControlModifier);
    QCOMPARE(sel.count(), 1);
    QCOMPARE(sel.selectedRows(), QList<int>({ 5 }));
    QCOMPARE(sel.currentRow(), 2);
    QVERIFY(!sel.isSelected(2));
    QVERIFY(sel.isSelected(5));
    QCOMPARE(spy.count(), 3);
}

void TstRowSelection::shiftSelectionWithAnchor()
{
    RowSelection sel;

    // Normal click 3 -> anchor = 3
    sel.select(3, Qt::NoModifier);
    QCOMPARE(sel.selectedRows(), QList<int>({ 3 }));
    QCOMPARE(sel.currentRow(), 3);

    // Shift click 6 -> interval [3, 6]
    sel.select(6, Qt::ShiftModifier);
    QCOMPARE(sel.count(), 4);
    QCOMPARE(sel.selectedRows(), QList<int>({ 3, 4, 5, 6 }));
    QCOMPARE(sel.currentRow(), 6);

    // Shift click 1 -> interval [1, 3] (anchor is still 3)
    sel.select(1, Qt::ShiftModifier);
    QCOMPARE(sel.count(), 3);
    QCOMPARE(sel.selectedRows(), QList<int>({ 1, 2, 3 }));
    QCOMPARE(sel.currentRow(), 1);
}

void TstRowSelection::selectAllAndClear()
{
    RowSelection sel;
    QSignalSpy spy(&sel, &RowSelection::changed);

    sel.selectAll(4);
    QCOMPARE(sel.count(), 4);
    QCOMPARE(sel.selectedRows(), QList<int>({ 0, 1, 2, 3 }));
    QCOMPARE(spy.count(), 1);

    sel.clear();
    QCOMPARE(sel.count(), 0);
    QCOMPARE(sel.selectedRows(), QList<int>());
    QCOMPARE(sel.currentRow(), -1);
    QCOMPARE(spy.count(), 2);
}

} // namespace

QTEST_GUILESS_MAIN(TstRowSelection)
#include "tst_RowSelection.moc"
