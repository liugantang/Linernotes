// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <library/Database.h>
#include <library/Migrator.h>
#include <ui/LibraryRootsModel.h>

namespace {

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::ui::LibraryRootsModel;

class TstLibraryRootsModel : public QObject {
    Q_OBJECT

private slots:
    void testRootsModelLifecycle();
};

void TstLibraryRootsModel::testRootsModelLifecycle()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    QVERIFY(db.open(Migrator()).ok());

    LibraryRootsModel model(db);
    QCOMPARE(model.rowCount(), 0);

    QSignalSpy rootsSpy(&model, &LibraryRootsModel::rootsChanged);

    const QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QUrl folderUrl = QUrl::fromLocalFile(musicDir.path());

    const bool addOk = model.addRoot(folderUrl);
    QVERIFY(addOk);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(rootsSpy.count(), 1);

    const QModelIndex idx = model.index(0, 0);
    const qint64 rootId = model.data(idx, LibraryRootsModel::RootIdRole).toLongLong();
    QVERIFY(rootId > 0);
    QCOMPARE(model.data(idx, LibraryRootsModel::PathRole).toString(), musicDir.path());
    QCOMPARE(model.data(idx, LibraryRootsModel::EnabledRole).toBool(), true);

    QVERIFY(!model.addRoot(folderUrl));
    QCOMPARE(model.rowCount(), 1);

    rootsSpy.clear();
    const bool enableOk = model.setRootEnabled(rootId, false);
    QVERIFY(enableOk);
    QCOMPARE(rootsSpy.count(), 1);
    const QModelIndex idxUpdated = model.index(0, 0);
    QCOMPARE(model.data(idxUpdated, LibraryRootsModel::EnabledRole).toBool(), false);

    rootsSpy.clear();
    const bool removeOk = model.removeRoot(rootId);
    QVERIFY(removeOk);
    QCOMPARE(rootsSpy.count(), 1);
    QCOMPARE(model.rowCount(), 0);
}

} // namespace

QTEST_MAIN(TstLibraryRootsModel)
#include "tst_LibraryRootsModel.moc"
