// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <library/FsType.h>

namespace {

using linernotes::library::FsKind;

class TstFsType : public QObject {
    Q_OBJECT

private slots:
    void fsKindFromMagic_data();
    void fsKindFromMagic();
    void fsKindForPath();
};

void TstFsType::fsKindFromMagic_data()
{
    QTest::addColumn<quint64>("magic");
    QTest::addColumn<FsKind>("expectedKind");

    // Network / Remote filesystems
    QTest::newRow("CIFS") << 0xFF534D42ULL << FsKind::Network;
    QTest::newRow("SMB2") << 0xFE534D42ULL << FsKind::Network;
    QTest::newRow("SMB") << 0x517BULL << FsKind::Network;
    QTest::newRow("NFS") << 0x6969ULL << FsKind::Network;
    QTest::newRow("FUSE") << 0x65735546ULL << FsKind::Network;
    QTest::newRow("9P") << 0x01021997ULL << FsKind::Network;
    QTest::newRow("Ceph") << 0x00C36400ULL << FsKind::Network;
    QTest::newRow("AFS") << 0x5346414FULL << FsKind::Network;

    // Local filesystems
    QTest::newRow("ext4") << 0xEF53ULL << FsKind::Local;
    QTest::newRow("btrfs") << 0x9123683EULL << FsKind::Local;
    QTest::newRow("tmpfs") << 0x01021994ULL << FsKind::Local;
    QTest::newRow("other_unknown_magic") << 0x12345678ULL << FsKind::Local;
}

void TstFsType::fsKindFromMagic()
{
    QFETCH(quint64, magic);
    QFETCH(FsKind, expectedKind);

    QCOMPARE(linernotes::library::fsKindFromMagic(magic), expectedKind);
}

void TstFsType::fsKindForPath()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const FsKind tempKind = linernotes::library::fsKindForPath(tempDir.path());
    // QTemporaryDir should be Local or at least not Unknown on valid path
    QVERIFY(tempKind != FsKind::Unknown);

    const QString nonExistentPath
        = tempDir.path() + QStringLiteral("/non_existent_subdir_xyz_12345");
    QCOMPARE(linernotes::library::fsKindForPath(nonExistentPath), FsKind::Unknown);

    QCOMPARE(linernotes::library::fsKindForPath(QString()), FsKind::Unknown);
}

} // namespace

QTEST_GUILESS_MAIN(TstFsType)

#include "tst_FsType.moc"
