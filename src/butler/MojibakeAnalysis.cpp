// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MojibakeAnalysis.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtGlobal>

#include <algorithm>

namespace linernotes::butler {

namespace {

using library::CorrectionProposal;
using library::CorrectionSource;
using library::TagField;

struct RecoverableItem {
    qint64 trackId = 0;
    TagField field = TagField::Title;
    QString original { };
    QByteArray bytes { };
    QString filePath { };
};

const char *decodedReason(SourceEncoding encoding)
{
    switch (encoding) {
    case SourceEncoding::Gbk:
        return QT_TRANSLATE_NOOP("butler", "Re-decoded as GBK (decided across the album)");
    case SourceEncoding::Big5:
        return QT_TRANSLATE_NOOP("butler", "Re-decoded as Big5 (decided across the album)");
    case SourceEncoding::ShiftJis:
        return QT_TRANSLATE_NOOP("butler", "Re-decoded as Shift-JIS (decided across the album)");
    case SourceEncoding::EucKr:
        return QT_TRANSLATE_NOOP("butler", "Re-decoded as EUC-KR (decided across the album)");
    case SourceEncoding::Utf8:
        return QT_TRANSLATE_NOOP("butler", "Re-decoded as UTF-8 (decided across the album)");
    }
    return QT_TRANSLATE_NOOP("butler", "Re-decoded as GBK (decided across the album)");
}

void handleIrreparableField(const MojibakeTrack &track, const MojibakeField &field,
    const QString &groupDir, GroupAnalysis &result)
{
    result.irreparable.append({ track.trackId, field.field });

    const QString baseName = QFileInfo(track.filePath).completeBaseName();
    const bool isFilenameClean
        = !baseName.isEmpty() && !looksIrreparable(baseName) && !looksLikeMojibake(baseName);

    if (field.field == TagField::Title && isFilenameClean) {
        const auto guess = guessFromPath(track.filePath);
        if (!guess.title.isEmpty()) {
            result.proposals.append(CorrectionProposal {
                .trackId = track.trackId,
                .field = TagField::Title,
                .oldValue = field.value,
                .newValue = guess.title,
                .source = CorrectionSource::Rule,
                .confidence = 0.5,
                .reason = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "Guessed from file name")),
            });
        }
    } else if (field.field == TagField::Artist && isFilenameClean) {
        const auto guess = guessFromPath(track.filePath);
        if (!guess.artist.isEmpty()) {
            result.proposals.append(CorrectionProposal {
                .trackId = track.trackId,
                .field = TagField::Artist,
                .oldValue = field.value,
                .newValue = guess.artist,
                .source = CorrectionSource::Rule,
                .confidence = 0.5,
                .reason = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "Guessed from file name")),
            });
        }
    } else if (field.field == TagField::Album) {
        const QString albumGuess = guessAlbumFromDirectory(groupDir);
        if (!albumGuess.isEmpty() && !looksIrreparable(albumGuess)
            && !looksLikeMojibake(albumGuess)) {
            result.proposals.append(CorrectionProposal {
                .trackId = track.trackId,
                .field = TagField::Album,
                .oldValue = field.value,
                .newValue = albumGuess,
                .source = CorrectionSource::Rule,
                .confidence = 0.4,
                .reason
                = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "Guessed from folder name")),
            });
        }
    }
}

void processDecidedGroup(const GroupDecision &decision,
    const QList<RecoverableItem> &recoverableItems, GroupAnalysis &result)
{
    const QString reason
        = QString::fromUtf8(decodedReason(decision.encoding.value_or(SourceEncoding::Gbk)));

    for (qsizetype i = 0; i < recoverableItems.size(); ++i) {
        const auto &item = recoverableItems.at(i);
        const auto &decodedOpt = (i < decision.texts.size()) ? decision.texts.at(i) : std::nullopt;
        if (decodedOpt.has_value() && *decodedOpt != item.original) {
            result.proposals.append(CorrectionProposal {
                .trackId = item.trackId,
                .field = item.field,
                .oldValue = item.original,
                .newValue = *decodedOpt,
                .source = CorrectionSource::Rule,
                .confidence = decision.confidence,
                .reason = reason,
            });
        }
    }
}

void processAmbiguousGroup(const QList<RecoverableItem> &recoverableItems, GroupAnalysis &result)
{
    for (const auto &item : recoverableItems) {
        auto cands = decodeCandidates(item.bytes);
        if (cands.size() > 3) {
            cands = cands.mid(0, 3);
        }
        result.ambiguous.append(AmbiguousItem {
            .id = static_cast<int>(result.ambiguous.size()),
            .trackId = item.trackId,
            .field = item.field,
            .original = item.original,
            .candidates = std::move(cands),
        });
    }
}

} // namespace

FilenameGuess guessFromPath(const QString &filePath)
{
    const QString base = QFileInfo(filePath).completeBaseName().trimmed();
    if (base.isEmpty()) {
        return FilenameGuess { };
    }

    // 1. NN - Artist - Title
    static const QRegularExpression s_re1(QStringLiteral(R"(^(\d{1,3})\s*-\s*(.+?)\s*-\s*(.+)$)"));
    const auto m1 = s_re1.match(base);
    if (m1.hasMatch()) {
        return FilenameGuess {
            .title = m1.captured(3).trimmed(),
            .artist = m1.captured(2).trimmed(),
            .trackNumber = m1.captured(1).toInt(),
        };
    }

    // 2. NN. Title
    static const QRegularExpression s_re2(QStringLiteral(R"(^(\d{1,3})\.\s*(.+)$)"));
    const auto m2 = s_re2.match(base);
    if (m2.hasMatch()) {
        return FilenameGuess {
            .title = m2.captured(2).trimmed(),
            .artist = QString(),
            .trackNumber = m2.captured(1).toInt(),
        };
    }

    // 3. NN Title
    static const QRegularExpression s_re3(QStringLiteral(R"(^(\d{1,3})\s+(.+)$)"));
    const auto m3 = s_re3.match(base);
    if (m3.hasMatch()) {
        return FilenameGuess {
            .title = m3.captured(2).trimmed(),
            .artist = QString(),
            .trackNumber = m3.captured(1).toInt(),
        };
    }

    // 4. NN-Title
    static const QRegularExpression s_re4(QStringLiteral(R"(^(\d{1,3})-(.+)$)"));
    const auto m4 = s_re4.match(base);
    if (m4.hasMatch()) {
        return FilenameGuess {
            .title = m4.captured(2).trimmed(),
            .artist = QString(),
            .trackNumber = m4.captured(1).toInt(),
        };
    }

    // 5. Artist - Title
    static const QRegularExpression s_re5(QStringLiteral(R"(^(.+?)\s*-\s*(.+)$)"));
    const auto m5 = s_re5.match(base);
    if (m5.hasMatch()) {
        return FilenameGuess {
            .title = m5.captured(2).trimmed(),
            .artist = m5.captured(1).trimmed(),
            .trackNumber = std::nullopt,
        };
    }

    // 6. Title
    return FilenameGuess {
        .title = base,
        .artist = QString(),
        .trackNumber = std::nullopt,
    };
}

QString guessAlbumFromDirectory(const QString &directory)
{
    QString dirName = QDir(directory).dirName().trimmed();
    if (dirName.isEmpty()) {
        dirName = directory.trimmed();
    }

    // 去掉开头的 YYYY - 、[YYYY] 、(YYYY)
    static const QRegularExpression s_yearPrefix(
        QStringLiteral(R"(^(?:\d{4}\s*-\s*|\[\d{4}\]\s*|\(\d{4}\)\s*))"));
    dirName.remove(s_yearPrefix);
    dirName = dirName.trimmed();

    // 形如 Artist - Album 的取 Album
    static const QRegularExpression s_artistAlbum(QStringLiteral(R"(^.+?\s*-\s*(.+)$)"));
    const auto match = s_artistAlbum.match(dirName);
    if (match.hasMatch()) {
        dirName = match.captured(1).trimmed();
    }

    return dirName;
}

GroupAnalysis analyzeGroup(const MojibakeGroup &group)
{
    GroupAnalysis result;
    QList<RecoverableItem> recoverableItems;

    for (const auto &track : group.tracks) {
        for (const auto &field : track.suspects) {
            if (looksIrreparable(field.value)) {
                handleIrreparableField(track, field, group.directory, result);
            } else {
                const QByteArray bytes = field.rawBytes.has_value()
                    ? *field.rawBytes
                    : recoverBytes(field.value).value_or(QByteArray());
                if (!bytes.isEmpty()) {
                    recoverableItems.append(RecoverableItem {
                        .trackId = track.trackId,
                        .field = field.field,
                        .original = field.value,
                        .bytes = bytes,
                        .filePath = track.filePath,
                    });
                }
            }
        }
    }

    if (recoverableItems.isEmpty()) {
        return result;
    }

    QList<QByteArray> allBytes;
    allBytes.reserve(recoverableItems.size());
    for (const auto &item : recoverableItems) {
        allBytes.append(item.bytes);
    }

    const GroupDecision decision = decideGroup(allBytes);
    result.encoding = decision.encoding;

    if (!decision.ambiguous && decision.encoding.has_value()) {
        processDecidedGroup(decision, recoverableItems, result);
    } else {
        processAmbiguousGroup(recoverableItems, result);
    }

    return result;
}

} // namespace linernotes::butler
