// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "HttpError.h"

#include "Errors.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace linernotes::ai::detail {

ErrorMapping mapNetworkOrHttpError(int status, QNetworkReply::NetworkError netErr, bool aborted)
{
    if (status == 401 || status == 403) {
        return { .code = QString(errc::kAuth), .message = QStringLiteral("Authentication failed") };
    }
    if (status == 429) {
        return { .code = QString(errc::kRateLimited), .message = QStringLiteral("Rate limited") };
    }
    if (status >= 400 && status < 600) {
        return { .code = QString(errc::kHttp),
            .message = QStringLiteral("HTTP error %1").arg(status) };
    }
    if (netErr == QNetworkReply::TimeoutError) {
        return { .code = QString(errc::kTimeout), .message = QStringLiteral("Request timed out") };
    }
    if (netErr == QNetworkReply::OperationCanceledError && aborted) {
        return { .code = QString(errc::kAborted), .message = QStringLiteral("Request aborted") };
    }
    return { .code = QString(errc::kNetwork), .message = QStringLiteral("Network error") };
}

QString extractErrorMessage(const QByteArray &body)
{
    if (body.isEmpty()) {
        return { };
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error"))) {
            const QJsonValue errVal = obj.value(QStringLiteral("error"));
            if (errVal.isObject()) {
                const QString msg = errVal.toObject().value(QStringLiteral("message")).toString();
                if (!msg.isEmpty()) {
                    return msg;
                }
            } else if (errVal.isString()) {
                return errVal.toString();
            }
        }
        if (obj.contains(QStringLiteral("message"))) {
            const QString msg = obj.value(QStringLiteral("message")).toString();
            if (!msg.isEmpty()) {
                return msg;
            }
        }
    }

    return QString::fromUtf8(body.left(200));
}

core::Error makeNetworkOrHttpError(int status, QNetworkReply::NetworkError netErr,
    const QByteArray &body, const QString &replyErrorString, bool aborted)
{
    const auto [code, message] = mapNetworkOrHttpError(status, netErr, aborted);

    QString errorDetail = extractErrorMessage(body);
    if (errorDetail.isEmpty()) {
        errorDetail = replyErrorString;
    }

    QString detail;
    if (status > 0) {
        detail = QStringLiteral("HTTP %1: %2").arg(QString::number(status), errorDetail);
    } else {
        detail = errorDetail;
    }

    return core::Error {
        .code = code,
        .message = message,
        .detail = detail,
    };
}

} // namespace linernotes::ai::detail
