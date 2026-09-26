// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "FileFingerprint.h"

#include <QCryptographicHash>
#include <QFile>
#include <QtEndian>

#include <library/Errors.h>

#include <array>
#include <bit>

namespace linernotes::library {

namespace {

constexpr qint64 kChunkSize = 64LL * 1024LL; // 64 KiB
constexpr qint64 kThreshold = 128LL * 1024LL; // 128 KiB

QString errorMessage(const QFile &file, const QString &fallback)
{
    return file.errorString().isEmpty() ? fallback : file.errorString();
}

core::Result<void> hashSmallFile(
    QFile &file, qint64 fileSize, QCryptographicHash &hash, const QString &path)
{
    if (fileSize <= 0) {
        return { };
    }

    const QByteArray data = file.readAll();
    if (data.size() != fileSize) {
        return core::Error {
            .code = QString(errc::kFileRead),
            .message = errorMessage(file, QStringLiteral("Failed to read file data")),
            .detail = path,
        };
    }
    hash.addData(data);
    return { };
}

core::Result<void> hashLargeFile(
    QFile &file, qint64 fileSize, QCryptographicHash &hash, const QString &path)
{
    const QByteArray head = file.read(kChunkSize);
    if (head.size() != kChunkSize) {
        return core::Error {
            .code = QString(errc::kFileRead),
            .message = errorMessage(file, QStringLiteral("Failed to read head chunk")),
            .detail = path,
        };
    }
    hash.addData(head);

    if (!file.seek(fileSize - kChunkSize)) {
        return core::Error {
            .code = QString(errc::kFileRead),
            .message = errorMessage(file, QStringLiteral("Failed to seek to tail chunk")),
            .detail = path,
        };
    }

    const QByteArray tail = file.read(kChunkSize);
    if (tail.size() != kChunkSize) {
        return core::Error {
            .code = QString(errc::kFileRead),
            .message = errorMessage(file, QStringLiteral("Failed to read tail chunk")),
            .detail = path,
        };
    }
    hash.addData(tail);
    return { };
}

} // namespace

core::Result<QString> FileFingerprint::compute(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return core::Error {
            .code = QString(errc::kFileRead),
            .message = file.errorString(),
            .detail = path,
        };
    }

    const qint64 fileSize = file.size();
    if (fileSize < 0) {
        return core::Error {
            .code = QString(errc::kFileRead),
            .message = errorMessage(file, QStringLiteral("Unable to query file size")),
            .detail = path,
        };
    }

    QCryptographicHash hash(QCryptographicHash::Blake2b_160);
    const auto sizeLe = qToLittleEndian<quint64>(static_cast<quint64>(fileSize));
    const auto sizeBytes = std::bit_cast<std::array<char, sizeof(sizeLe)>>(sizeLe);
    hash.addData(QByteArrayView(sizeBytes.data(), sizeBytes.size()));

    if (fileSize <= kThreshold) {
        const auto smallRes = hashSmallFile(file, fileSize, hash, path);
        if (!smallRes.ok()) {
            return smallRes.error();
        }
    } else {
        const auto largeRes = hashLargeFile(file, fileSize, hash, path);
        if (!largeRes.ok()) {
            return largeRes.error();
        }
    }

    const QString hex = QString::fromLatin1(hash.result().toHex());
    return QString(QStringLiteral("v1:") + hex);
}

} // namespace linernotes::library
