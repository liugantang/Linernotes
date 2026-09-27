// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QImage>
#include <QUrl>

#include <library/CoverStore.h>
#include <ui/CoverImageProvider.h>

#include <algorithm>

namespace linernotes::ui {

CoverImageProvider::CoverImageProvider(const library::CoverStore *coverStore)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_coverStore(coverStore)
{
}

QImage CoverImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    if (m_coverStore == nullptr || id.isEmpty()) {
        return { };
    }

    const QString hash = QUrl::fromPercentEncoding(id.toUtf8());
    if (hash.isEmpty()) {
        return { };
    }

    int targetSize = 512;
    const int maxRequested = std::max(requestedSize.width(), requestedSize.height());
    if (maxRequested > 0) {
        targetSize = library::CoverStore::kSizes.back();
        for (const int sz : library::CoverStore::kSizes) {
            if (sz >= maxRequested) {
                targetSize = sz;
                break;
            }
        }
    }

    const QString path = m_coverStore->thumbnailPath(hash, targetSize);
    if (path.isEmpty()) {
        return { };
    }

    QImage image(path);
    if (image.isNull()) {
        return { };
    }

    if (size != nullptr) {
        *size = image.size();
    }

    return image;
}

} // namespace linernotes::ui
