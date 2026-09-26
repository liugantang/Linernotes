// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>
#include <QtGlobal>

#include <cstdint>

namespace linernotes::library {

enum class FsKind : std::uint8_t { Local, Network, Unknown };

/// 纯函数：由 statfs 的 f_type 判断。网络/远程：CIFS 0xFF534D42、SMB2 0xFE534D42、SMB 0x517B、NFS
/// 0x6969、 FUSE 0x65735546（sshfs、rclone 等，按网络处理）、9P 0x01021997、Ceph 0x00C36400、AFS
/// 0x5346414F。其余为 Local。
FsKind fsKindFromMagic(quint64 fType);

/// 对路径调用 statfs；失败返回 Unknown（按 Network 处理，保守）。非 Linux 平台返回
/// Local（以后移植时再做）。
FsKind fsKindForPath(const QString &path);

} // namespace linernotes::library
