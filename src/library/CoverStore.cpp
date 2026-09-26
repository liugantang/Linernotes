// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CoverStore.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QSize>
#include <QTemporaryFile>

#include <library/Errors.h>
#include <library/LibraryLogging.h>

#include <algorithm>
#include <ranges>
#include <utility>

namespace linernotes::library {

namespace {

QString determineMime(const QByteArray &formatBytes)
{
    if (formatBytes == "png") {
        return QStringLiteral("image/png");
    }
    if (formatBytes == "webp") {
        return QStringLiteral("image/webp");
    }
    if (formatBytes == "gif") {
        return QStringLiteral("image/gif");
    }
    if (!formatBytes.isEmpty() && formatBytes != "jpeg" && formatBytes != "jpg") {
        return QStringLiteral("image/") + QString::fromLatin1(formatBytes);
    }
    return QStringLiteral("image/jpeg");
}

bool allThumbnailsExist(const QString &subDirPath, const QString &hex)
{
    return std::ranges::all_of(CoverStore::kSizes, [&](int sz) {
        const QString targetPath
            = subDirPath + u'/' + hex + u'_' + QString::number(sz) + QStringLiteral(".jpg");
        return QFile::exists(targetPath);
    });
}

void saveThumbnail(const QString &subDirPath, const QString &targetPath, const QImage &image)
{
    QTemporaryFile tempFile(subDirPath + QStringLiteral("/.tmp_XXXXXX.jpg"));
    tempFile.setAutoRemove(true);
    if (tempFile.open()) {
        if (image.save(&tempFile, "JPG", 85)) {
            tempFile.close();
            if (QFile::rename(tempFile.fileName(), targetPath)) {
                tempFile.setAutoRemove(false);
            } else if (QFile::exists(targetPath)) {
                tempFile.remove();
            }
        }
    }
}

core::Result<void> writeThumbnails(
    const QString &subDirPath, const QString &hex, const QSize &originalSize, QImageReader &reader)
{
    if (!QDir().mkpath(subDirPath)) {
        qCWarning(lcLibrary) << "Failed to create cover cache directory:" << subDirPath;
    }

    const int maxOrigDim = std::max(originalSize.width(), originalSize.height());
    const int maxTargetDim = std::min(maxOrigDim, CoverStore::kSizes.back());

    if (maxOrigDim > maxTargetDim) {
        const QSize scaledMaxSize
            = originalSize.scaled(maxTargetDim, maxTargetDim, Qt::KeepAspectRatio);
        reader.setScaledSize(scaledMaxSize);
    }

    const QImage baseImage = reader.read();
    if (baseImage.isNull()) {
        return core::Error {
            .code = QString(errc::kCoverDecode),
            .message
            = QStringLiteral("Failed to decode image pixels: %1").arg(reader.errorString()),
            .detail = QString(),
        };
    }

    for (const int sz : CoverStore::kSizes) {
        const QString targetPath
            = subDirPath + u'/' + hex + u'_' + QString::number(sz) + QStringLiteral(".jpg");
        if (QFile::exists(targetPath)) {
            continue;
        }

        QImage imgForSize;
        if (maxOrigDim <= sz) {
            imgForSize = baseImage;
        } else {
            const QSize targetSize = originalSize.scaled(sz, sz, Qt::KeepAspectRatio);
            if (baseImage.size() == targetSize) {
                imgForSize = baseImage;
            } else {
                imgForSize
                    = baseImage.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
        }

        saveThumbnail(subDirPath, targetPath, imgForSize);
    }

    return { };
}

} // namespace

CoverStore::CoverStore(QString cacheDir)
    : m_cacheDir(std::move(cacheDir))
{
}

core::Result<CoverStore::Info> CoverStore::ingest(const QByteArray &imageData)
{
    if (imageData.isEmpty()) {
        return core::Error {
            .code = QString(errc::kCoverDecode),
            .message = QStringLiteral("Image data is empty"),
            .detail = QString(),
        };
    }

    QCryptographicHash hashObj(QCryptographicHash::Blake2b_160);
    hashObj.addData(imageData);
    const QString hex = QString::fromLatin1(hashObj.result().toHex());
    const QString hashStr = QStringLiteral("v1:") + hex;

    QBuffer buffer;
    buffer.setData(imageData);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return core::Error {
            .code = QString(errc::kCoverDecode),
            .message = QStringLiteral("Failed to open image buffer"),
            .detail = QString(),
        };
    }

    QImageReader reader(&buffer);
    if (!reader.canRead()) {
        return core::Error {
            .code = QString(errc::kCoverDecode),
            .message = QStringLiteral("Cannot read image data: %1").arg(reader.errorString()),
            .detail = QString(),
        };
    }

    const QSize originalSize = reader.size();
    if (!originalSize.isValid() || originalSize.isEmpty()) {
        return core::Error {
            .code = QString(errc::kCoverDecode),
            .message = QStringLiteral("Invalid image dimensions"),
            .detail = QString(),
        };
    }

    const Info info {
        .hash = hashStr,
        .mime = determineMime(reader.format().toLower()),
        .width = originalSize.width(),
        .height = originalSize.height(),
    };

    if (m_cacheDir.isEmpty()) {
        return info;
    }

    const QString subDirName = hex.left(2);
    const QString subDirPath = m_cacheDir + u'/' + subDirName;

    if (allThumbnailsExist(subDirPath, hex)) {
        return info;
    }

    const auto writeRes = writeThumbnails(subDirPath, hex, originalSize, reader);
    if (!writeRes.ok()) {
        return writeRes.error();
    }

    return info;
}

QString CoverStore::thumbnailPath(const QString &hash, int size) const
{
    if (m_cacheDir.isEmpty() || !hash.startsWith(QLatin1StringView("v1:")) || hash.length() <= 3) {
        return { };
    }

    const QString hex = hash.mid(3);
    const QString subDirPath = m_cacheDir + u'/' + hex.left(2);

    for (const int sz : std::ranges::reverse_view(kSizes)) {
        if (sz <= size) {
            const QString targetPath
                = subDirPath + u'/' + hex + u'_' + QString::number(sz) + QStringLiteral(".jpg");
            if (QFile::exists(targetPath)) {
                return targetPath;
            }
        }
    }
    return { };
}

int CoverStore::prune(const QSet<QString> &keep)
{
    if (m_cacheDir.isEmpty() || !QDir(m_cacheDir).exists()) {
        return 0;
    }

    QSet<QString> removedHashes;
    QDirIterator it(
        m_cacheDir, { QStringLiteral("*.jpg") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QString fileName = it.fileName();
        if (fileName.startsWith(u'.')) {
            QFile::remove(filePath);
            continue;
        }
        const qsizetype lastUnderscore = fileName.lastIndexOf(u'_');
        if (lastUnderscore <= 0) {
            continue;
        }
        const QString hex = fileName.left(lastUnderscore);
        const QString fullHash = QStringLiteral("v1:") + hex;
        if (!keep.contains(fullHash)) {
            if (QFile::remove(filePath)) {
                removedHashes.insert(fullHash);
            }
        }
    }

    const QDir cacheDir(m_cacheDir);
    const QStringList subDirs = cacheDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : subDirs) {
        const QDir subDir(cacheDir.filePath(sub));
        if (subDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
            cacheDir.rmdir(sub);
        }
    }

    return static_cast<int>(removedHashes.size());
}

} // namespace linernotes::library
