// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QQuickImageProvider>

namespace linernotes::library {
class CoverStore;
} // namespace linernotes::library

namespace linernotes::ui {

class CoverImageProvider : public QQuickImageProvider {
public:
    explicit CoverImageProvider(const library::CoverStore *coverStore);
    ~CoverImageProvider() override = default;
    Q_DISABLE_COPY_MOVE(CoverImageProvider)

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    const library::CoverStore *m_coverStore { nullptr };
};

} // namespace linernotes::ui
