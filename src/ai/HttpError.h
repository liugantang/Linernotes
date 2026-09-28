// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QByteArray>
#include <QNetworkReply>
#include <QString>

#include <core/Result.h>

namespace linernotes::ai::detail {

struct ErrorMapping {
    QString code;
    QString message;
};

ErrorMapping mapNetworkOrHttpError(
    int status, QNetworkReply::NetworkError netErr, bool aborted = false);

QString extractErrorMessage(const QByteArray &body);

core::Error makeNetworkOrHttpError(int status, QNetworkReply::NetworkError netErr,
    const QByteArray &body, const QString &replyErrorString = { }, bool aborted = false);

} // namespace linernotes::ai::detail
