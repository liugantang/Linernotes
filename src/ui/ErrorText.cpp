// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ErrorText.h"

#include <QCoreApplication>

#include <ai/Errors.h>
#include <core/Result.h>
#include <library/Errors.h>
#include <nlq/Errors.h>

namespace linernotes::ui {

namespace {

QString nlqErrorText(const QString &code)
{
    if (code == nlq::errc::kQueryInvalid) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Invalid natural language query.");
    }
    if (code == nlq::errc::kInvalidResult) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Failed to interpret the query.");
    }
    if (code == nlq::errc::kSchemaNotFound || code == nlq::errc::kPromptRenderFailed) {
        return QCoreApplication::translate("linernotes::ui::ErrorText", "Query template error.");
    }
    return { };
}

QString coreErrorText(const core::Error &error)
{
    if (error.code == library::errc::kDbTooNew) {
        return QCoreApplication::translate("linernotes::ui::ErrorText",
            "The music library was created by a newer version of Linernotes.");
    }
    if (error.code == library::errc::kDbOpen || error.code == library::errc::kDbMigration
        || error.code == library::errc::kDbMigrationInvalid
        || error.code == library::errc::kDbBackup || error.code == library::errc::kDbTransaction
        || error.code == library::errc::kDbQuery || error.code == library::errc::kDbFts5
        || error.code == library::errc::kDbFk || error.code == library::errc::kDbDriver
        || error.code == library::errc::kDbHandle) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "A database error occurred.");
    }
    if (error.code == library::errc::kCorrectionInvalid) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "The correction is invalid.");
    }
    if (error.code == library::errc::kCorrectionNotFound) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "The specified correction was not found.");
    }
    if (error.code == library::errc::kTagOverrideInvalid) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Invalid tag modification.");
    }
    if (error.code == library::errc::kTagRead) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Failed to read audio file tags.");
    }
    if (error.code == library::errc::kTagUnsupported) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "The audio file format or tag format is not supported.");
    }
    if (error.code == library::errc::kTagWriteUnsupported) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Writing tags to this audio format is not supported.");
    }
    if (error.code == library::errc::kTagWriteFailed) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Failed to write tags to audio file.");
    }
    if (error.code == library::errc::kTagWriteVerifyFailed) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Tag verification failed after writing.");
    }
    if (error.code == library::errc::kFileRead) {
        return QCoreApplication::translate("linernotes::ui::ErrorText", "Failed to read file.");
    }
    if (error.code == library::errc::kRootInvalid) {
        return QCoreApplication::translate("linernotes::ui::ErrorText", "Invalid music folder.");
    }
    if (error.code == library::errc::kRootOverlap) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Music folder overlaps with an existing library root.");
    }
    if (error.code == library::errc::kCoverDecode) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Failed to decode cover image.");
    }
    if (error.code == library::errc::kPlaylistInvalid) {
        return QCoreApplication::translate("linernotes::ui::ErrorText", "Invalid playlist.");
    }
    if (error.code == library::errc::kPlaylistRuleInvalid) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Invalid smart playlist rule.");
    }
    if (error.code == library::errc::kRatingInvalid) {
        return QCoreApplication::translate("linernotes::ui::ErrorText", "Invalid rating value.");
    }
    if (error.code == ai::errc::kSecretStore) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Could not access the system keyring.");
    }
    if (error.code == ai::errc::kNetwork) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Unable to connect to AI service.");
    }
    if (error.code == ai::errc::kTimeout) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "AI service request timed out.");
    }
    if (error.code == ai::errc::kAuth) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "AI service authentication failed.");
    }
    if (error.code == ai::errc::kRateLimited) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "AI service rate limit exceeded.");
    }
    if (error.code == ai::errc::kPrivacyBlocked) {
        return QCoreApplication::translate(
            "linernotes::ui::ErrorText", "Request blocked by privacy settings.");
    }

    if (error.code.isEmpty()) {
        return QCoreApplication::translate("linernotes::ui::ErrorText", "Operation failed.");
    }
    return QCoreApplication::translate(
        "linernotes::ui::ErrorText", "Operation failed (error code: %1)")
        .arg(error.code);
}

} // namespace

QString userErrorText(const core::Error &error)
{
    const QString text = nlqErrorText(error.code);
    return text.isEmpty() ? coreErrorText(error) : text;
}

} // namespace linernotes::ui
