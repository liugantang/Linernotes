// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "FsType.h"

#include <QFile>

#ifdef Q_OS_LINUX
#include <sys/vfs.h>

#include <type_traits>
#endif

namespace linernotes::library {

FsKind fsKindFromMagic(quint64 fType)
{
    switch (fType) {
    case 0xFF534D42ULL: // CIFS
    case 0xFE534D42ULL: // SMB2
    case 0x517BULL: // SMB
    case 0x6969ULL: // NFS
    case 0x65735546ULL: // FUSE
    case 0x01021997ULL: // 9P
    case 0x00C36400ULL: // Ceph
    case 0x5346414FULL: // AFS
        return FsKind::Network;
    default:
        return FsKind::Local;
    }
}

FsKind fsKindForPath(const QString &path)
{
#ifdef Q_OS_LINUX
    if (path.isEmpty()) {
        return FsKind::Unknown;
    }
    struct statfs buf { };
    const QByteArray localPath = QFile::encodeName(path);
    if (statfs(localPath.constData(), &buf) != 0) {
        return FsKind::Unknown;
    }
    const auto rawType = static_cast<std::make_unsigned_t<decltype(buf.f_type)>>(buf.f_type);
    return fsKindFromMagic(static_cast<quint64>(rawType));
#else
    Q_UNUSED(path);
    return FsKind::Local;
#endif
}

} // namespace linernotes::library
