// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TagWriter.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <apetag.h>
#include <core/Result.h>
#include <fileref.h>
#include <flacfile.h>
#include <id3v1tag.h>
#include <id3v2header.h>
#include <id3v2tag.h>
#include <library/Errors.h>
#include <library/LibraryLogging.h>
#include <mp4file.h>
#include <mp4tag.h>
#include <mpegfile.h>
#include <opusfile.h>
#include <tpropertymap.h>
#include <tstring.h>
#include <tstringlist.h>
#include <vorbisfile.h>
#include <xiphcomment.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace linernotes::library {

namespace {

enum class SupportedFormat : std::uint8_t {
    None,
    Flac,
    Mpeg,
    Mp4,
    OggVorbis,
    OggOpus,
};

QString toQString(const TagLib::String &s)
{
    return QString::fromUtf8(s.toCString(true));
}

TagLib::String toTString(const QString &s)
{
    return { s.toUtf8().constData(), TagLib::String::UTF8 };
}

QMap<QString, QStringList> fromPropertyMap(const TagLib::PropertyMap &propMap)
{
    QMap<QString, QStringList> result;
    for (const auto &[pKey, pValues] : propMap) {
        const QString key = toQString(pKey).toUpper();
        QStringList values;
        for (const auto &val : pValues) {
            values.append(toQString(val));
        }
        result.insert(key, values);
    }
    return result;
}

TagLib::PropertyMap toPropertyMap(const QMap<QString, QStringList> &map)
{
    TagLib::PropertyMap result;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        const TagLib::String key = toTString(it.key());
        TagLib::StringList values;
        for (const QString &val : it.value()) {
            values.append(toTString(val));
        }
        result.insert(key, values);
    }
    return result;
}

QStringList unsupportedToStringList(const TagLib::StringList &list)
{
    QStringList result;
    for (const auto &item : list) {
        result.append(toQString(item));
    }
    return result;
}

SupportedFormat detectFormat(const QString &path)
{
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile() || fileInfo.size() == 0) {
        return SupportedFormat::None;
    }

    TagLib::FileRef const fileRef(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (fileRef.isNull() || fileRef.file() == nullptr || !fileRef.file()->isValid()) {
        return SupportedFormat::None;
    }

    TagLib::File *file = fileRef.file();
    if (dynamic_cast<TagLib::FLAC::File *>(file) != nullptr) {
        return SupportedFormat::Flac;
    }
    if (dynamic_cast<TagLib::MPEG::File *>(file) != nullptr) {
        return SupportedFormat::Mpeg;
    }
    if (dynamic_cast<TagLib::MP4::File *>(file) != nullptr) {
        return SupportedFormat::Mp4;
    }
    if (dynamic_cast<TagLib::Ogg::Vorbis::File *>(file) != nullptr) {
        return SupportedFormat::OggVorbis;
    }
    if (dynamic_cast<TagLib::Ogg::Opus::File *>(file) != nullptr) {
        return SupportedFormat::OggOpus;
    }
    return SupportedFormat::None;
}

core::Result<void> readFlacBlocks(const QString &path, TagSnapshot &outSnapshot)
{
    TagLib::FLAC::File file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open FLAC file"),
            .detail = path,
        };
    }

    if (file.hasXiphComment() && file.xiphComment() != nullptr) {
        TagSnapshot::Block block;
        block.type = QStringLiteral("xiph");
        block.properties = fromPropertyMap(file.xiphComment()->properties());
        block.unsupported
            = unsupportedToStringList(file.xiphComment()->properties().unsupportedData());
        block.id3v2Version = 0;
        outSnapshot.blocks.append(block);
    }
    if (file.hasID3v2Tag() && file.ID3v2Tag() != nullptr) {
        TagSnapshot::Block block;
        block.type = QStringLiteral("id3v2");
        block.properties = fromPropertyMap(file.ID3v2Tag()->properties());
        block.unsupported
            = unsupportedToStringList(file.ID3v2Tag()->properties().unsupportedData());
        if (file.ID3v2Tag()->header() != nullptr) {
            block.id3v2Version = static_cast<int>(file.ID3v2Tag()->header()->majorVersion());
        }
        outSnapshot.blocks.append(block);
    }
    if (file.hasID3v1Tag() && file.ID3v1Tag() != nullptr) {
        TagSnapshot::Block block;
        block.type = QStringLiteral("id3v1");
        block.properties = fromPropertyMap(file.ID3v1Tag()->properties());
        block.unsupported
            = unsupportedToStringList(file.ID3v1Tag()->properties().unsupportedData());
        block.id3v2Version = 0;
        outSnapshot.blocks.append(block);
    }
    return { };
}

core::Result<void> readMpegBlocks(const QString &path, TagSnapshot &outSnapshot)
{
    TagLib::MPEG::File file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open MPEG file"),
            .detail = path,
        };
    }

    if (file.hasID3v2Tag() && file.ID3v2Tag() != nullptr) {
        TagSnapshot::Block block;
        block.type = QStringLiteral("id3v2");
        block.properties = fromPropertyMap(file.ID3v2Tag()->properties());
        block.unsupported
            = unsupportedToStringList(file.ID3v2Tag()->properties().unsupportedData());
        if (file.ID3v2Tag()->header() != nullptr) {
            block.id3v2Version = static_cast<int>(file.ID3v2Tag()->header()->majorVersion());
        }
        outSnapshot.blocks.append(block);
    }
    if (file.hasAPETag() && file.APETag() != nullptr) {
        TagSnapshot::Block block;
        block.type = QStringLiteral("ape");
        block.properties = fromPropertyMap(file.APETag()->properties());
        block.unsupported = unsupportedToStringList(file.APETag()->properties().unsupportedData());
        block.id3v2Version = 0;
        outSnapshot.blocks.append(block);
    }
    if (file.hasID3v1Tag() && file.ID3v1Tag() != nullptr) {
        TagSnapshot::Block block;
        block.type = QStringLiteral("id3v1");
        block.properties = fromPropertyMap(file.ID3v1Tag()->properties());
        block.unsupported
            = unsupportedToStringList(file.ID3v1Tag()->properties().unsupportedData());
        block.id3v2Version = 0;
        outSnapshot.blocks.append(block);
    }
    return { };
}

core::Result<void> readMp4Blocks(const QString &path, TagSnapshot &outSnapshot)
{
    TagLib::MP4::File const file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open MP4 file"),
            .detail = path,
        };
    }

    if (file.hasMP4Tag() && file.tag() != nullptr) {
        TagSnapshot::Block block;
        block.type = QStringLiteral("mp4");
        block.properties = fromPropertyMap(file.tag()->properties());
        block.unsupported = unsupportedToStringList(file.tag()->properties().unsupportedData());
        block.id3v2Version = 0;
        outSnapshot.blocks.append(block);
    }
    return { };
}

core::Result<void> readOggVorbisBlocks(const QString &path, TagSnapshot &outSnapshot)
{
    TagLib::Ogg::Vorbis::File const file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open Ogg Vorbis file"),
            .detail = path,
        };
    }

    if (file.tag() != nullptr) {
        TagSnapshot::Block block;
        block.type = QStringLiteral("xiph");
        block.properties = fromPropertyMap(file.tag()->properties());
        block.unsupported = unsupportedToStringList(file.tag()->properties().unsupportedData());
        block.id3v2Version = 0;
        outSnapshot.blocks.append(block);
    }
    return { };
}

core::Result<void> readOggOpusBlocks(const QString &path, TagSnapshot &outSnapshot)
{
    TagLib::Ogg::Opus::File const file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open Ogg Opus file"),
            .detail = path,
        };
    }

    if (file.tag() != nullptr) {
        TagSnapshot::Block block;
        block.type = QStringLiteral("xiph");
        block.properties = fromPropertyMap(file.tag()->properties());
        block.unsupported = unsupportedToStringList(file.tag()->properties().unsupportedData());
        block.id3v2Version = 0;
        outSnapshot.blocks.append(block);
    }
    return { };
}

void applySimpleField(TagLib::PropertyMap &propMap, const TagLib::String &key,
    const QHash<TagField, QString> &values, TagField field)
{
    if (!values.contains(field)) {
        return;
    }
    const QString val = values.value(field);
    if (val.isEmpty()) {
        propMap.erase(key);
    } else {
        propMap.replace(key, TagLib::StringList(toTString(val)));
    }
}

void applySimpleFields(TagLib::PropertyMap &propMap, const QHash<TagField, QString> &values)
{
    applySimpleField(propMap, "TITLE", values, TagField::Title);
    applySimpleField(propMap, "ARTIST", values, TagField::Artist);
    applySimpleField(propMap, "ALBUM", values, TagField::Album);
    applySimpleField(propMap, "ALBUMARTIST", values, TagField::AlbumArtist);
    applySimpleField(propMap, "GENRE", values, TagField::Genre);
    applySimpleField(propMap, "COMPOSER", values, TagField::Composer);
    applySimpleField(propMap, "DATE", values, TagField::Year);
}

void applyTrackFieldsXiph(TagLib::PropertyMap &propMap, const QHash<TagField, QString> &values)
{
    if (values.contains(TagField::TrackNumber)) {
        const QString num = values.value(TagField::TrackNumber);
        if (num.isEmpty()) {
            propMap.erase("TRACKNUMBER");
        } else {
            propMap.replace("TRACKNUMBER", TagLib::StringList(toTString(num)));
        }
    }
    if (values.contains(TagField::TrackTotal)) {
        const QString tot = values.value(TagField::TrackTotal);
        if (tot.isEmpty()) {
            propMap.erase("TRACKTOTAL");
        } else {
            propMap.replace("TRACKTOTAL", TagLib::StringList(toTString(tot)));
        }
    }
}

void applyTrackFieldsCombined(TagLib::PropertyMap &propMap, const QHash<TagField, QString> &values)
{
    if (!values.contains(TagField::TrackNumber) && !values.contains(TagField::TrackTotal)) {
        return;
    }

    QString currentNum;
    QString currentTot;
    if (propMap.contains("TRACKNUMBER") && !propMap.value("TRACKNUMBER").isEmpty()) {
        const QString raw = toQString(propMap.value("TRACKNUMBER").front());
        const qsizetype slashIdx = raw.indexOf(u'/');
        if (slashIdx >= 0) {
            currentNum = raw.left(slashIdx).trimmed();
            currentTot = raw.mid(slashIdx + 1).trimmed();
        } else {
            currentNum = raw.trimmed();
        }
    }
    if (currentTot.isEmpty() && propMap.contains("TRACKTOTAL")
        && !propMap.value("TRACKTOTAL").isEmpty()) {
        currentTot = toQString(propMap.value("TRACKTOTAL").front()).trimmed();
    }

    const QString finalNum = values.contains(TagField::TrackNumber)
        ? values.value(TagField::TrackNumber).trimmed()
        : currentNum;
    const QString finalTot = values.contains(TagField::TrackTotal)
        ? values.value(TagField::TrackTotal).trimmed()
        : currentTot;

    if (finalNum.isEmpty() && finalTot.isEmpty()) {
        propMap.erase("TRACKNUMBER");
    } else if (!finalNum.isEmpty() && !finalTot.isEmpty()) {
        propMap.replace("TRACKNUMBER",
            TagLib::StringList(toTString(QStringLiteral("%1/%2").arg(finalNum, finalTot))));
    } else if (!finalNum.isEmpty()) {
        propMap.replace("TRACKNUMBER", TagLib::StringList(toTString(finalNum)));
    } else {
        propMap.replace(
            "TRACKNUMBER", TagLib::StringList(toTString(QStringLiteral("/%1").arg(finalTot))));
    }

    if (values.contains(TagField::TrackTotal)) {
        propMap.erase("TRACKTOTAL");
    }
}

void applyDiscFieldsXiph(TagLib::PropertyMap &propMap, const QHash<TagField, QString> &values)
{
    if (values.contains(TagField::DiscNumber)) {
        const QString num = values.value(TagField::DiscNumber);
        if (num.isEmpty()) {
            propMap.erase("DISCNUMBER");
        } else {
            propMap.replace("DISCNUMBER", TagLib::StringList(toTString(num)));
        }
    }
    if (values.contains(TagField::DiscTotal)) {
        const QString tot = values.value(TagField::DiscTotal);
        if (tot.isEmpty()) {
            propMap.erase("DISCTOTAL");
        } else {
            propMap.replace("DISCTOTAL", TagLib::StringList(toTString(tot)));
        }
    }
}

void applyDiscFieldsCombined(TagLib::PropertyMap &propMap, const QHash<TagField, QString> &values)
{
    if (!values.contains(TagField::DiscNumber) && !values.contains(TagField::DiscTotal)) {
        return;
    }

    QString currentNum;
    QString currentTot;
    if (propMap.contains("DISCNUMBER") && !propMap.value("DISCNUMBER").isEmpty()) {
        const QString raw = toQString(propMap.value("DISCNUMBER").front());
        const qsizetype slashIdx = raw.indexOf(u'/');
        if (slashIdx >= 0) {
            currentNum = raw.left(slashIdx).trimmed();
            currentTot = raw.mid(slashIdx + 1).trimmed();
        } else {
            currentNum = raw.trimmed();
        }
    }
    if (currentTot.isEmpty() && propMap.contains("DISCTOTAL")
        && !propMap.value("DISCTOTAL").isEmpty()) {
        currentTot = toQString(propMap.value("DISCTOTAL").front()).trimmed();
    }

    const QString finalNum = values.contains(TagField::DiscNumber)
        ? values.value(TagField::DiscNumber).trimmed()
        : currentNum;
    const QString finalTot = values.contains(TagField::DiscTotal)
        ? values.value(TagField::DiscTotal).trimmed()
        : currentTot;

    if (finalNum.isEmpty() && finalTot.isEmpty()) {
        propMap.erase("DISCNUMBER");
    } else if (!finalNum.isEmpty() && !finalTot.isEmpty()) {
        propMap.replace("DISCNUMBER",
            TagLib::StringList(toTString(QStringLiteral("%1/%2").arg(finalNum, finalTot))));
    } else if (!finalNum.isEmpty()) {
        propMap.replace("DISCNUMBER", TagLib::StringList(toTString(finalNum)));
    } else {
        propMap.replace(
            "DISCNUMBER", TagLib::StringList(toTString(QStringLiteral("/%1").arg(finalTot))));
    }

    if (values.contains(TagField::DiscTotal)) {
        propMap.erase("DISCTOTAL");
    }
}

core::Result<void> writeFlacFields(const QString &path, const QHash<TagField, QString> &values)
{
    TagLib::FLAC::File file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open FLAC file"),
            .detail = path,
        };
    }

    auto *xiph = file.xiphComment(true);
    if (xiph == nullptr) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to create Xiph comment in FLAC file"),
            .detail = path,
        };
    }

    TagLib::PropertyMap propMap = xiph->properties();
    applySimpleFields(propMap, values);
    applyTrackFieldsXiph(propMap, values);
    applyDiscFieldsXiph(propMap, values);
    xiph->setProperties(propMap);

    if (!file.save()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to save FLAC file"),
            .detail = path,
        };
    }
    return { };
}

core::Result<void> writeMpegFields(const QString &path, const QHash<TagField, QString> &values)
{
    TagLib::MPEG::File file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open MPEG file"),
            .detail = path,
        };
    }

    TagLib::ID3v2::Version id3v2Ver = TagLib::ID3v2::v4;
    if (file.hasID3v2Tag() && file.ID3v2Tag() != nullptr && file.ID3v2Tag()->header() != nullptr) {
        if (file.ID3v2Tag()->header()->majorVersion() == 3) {
            id3v2Ver = TagLib::ID3v2::v3;
        }
    }

    auto *id3v2 = file.ID3v2Tag(true);
    if (id3v2 == nullptr) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to create ID3v2 tag in MPEG file"),
            .detail = path,
        };
    }

    TagLib::PropertyMap propMap = id3v2->properties();
    applySimpleFields(propMap, values);
    applyTrackFieldsCombined(propMap, values);
    applyDiscFieldsCombined(propMap, values);
    id3v2->setProperties(propMap);

    if (!file.save(TagLib::MPEG::File::AllTags, TagLib::File::StripNone, id3v2Ver,
            TagLib::File::DoNotDuplicate)) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to save MPEG file"),
            .detail = path,
        };
    }
    return { };
}

core::Result<void> writeMp4Fields(const QString &path, const QHash<TagField, QString> &values)
{
    TagLib::MP4::File file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid() || file.tag() == nullptr) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open MP4 file"),
            .detail = path,
        };
    }

    TagLib::PropertyMap propMap = file.tag()->properties();
    applySimpleFields(propMap, values);
    applyTrackFieldsCombined(propMap, values);
    applyDiscFieldsCombined(propMap, values);
    file.tag()->setProperties(propMap);

    if (!file.save()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to save MP4 file"),
            .detail = path,
        };
    }
    return { };
}

core::Result<void> writeOggVorbisFields(const QString &path, const QHash<TagField, QString> &values)
{
    TagLib::Ogg::Vorbis::File file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid() || file.tag() == nullptr) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open Ogg Vorbis file"),
            .detail = path,
        };
    }

    TagLib::PropertyMap propMap = file.tag()->properties();
    applySimpleFields(propMap, values);
    applyTrackFieldsXiph(propMap, values);
    applyDiscFieldsXiph(propMap, values);
    file.tag()->setProperties(propMap);

    if (!file.save()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to save Ogg Vorbis file"),
            .detail = path,
        };
    }
    return { };
}

core::Result<void> writeOggOpusFields(const QString &path, const QHash<TagField, QString> &values)
{
    TagLib::Ogg::Opus::File file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid() || file.tag() == nullptr) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open Ogg Opus file"),
            .detail = path,
        };
    }

    TagLib::PropertyMap propMap = file.tag()->properties();
    applySimpleFields(propMap, values);
    applyTrackFieldsXiph(propMap, values);
    applyDiscFieldsXiph(propMap, values);
    file.tag()->setProperties(propMap);

    if (!file.save()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to save Ogg Opus file"),
            .detail = path,
        };
    }
    return { };
}

core::Result<void> restoreFlac(const QString &path, const TagSnapshot &snapshot)
{
    TagLib::FLAC::File file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open FLAC file for restore"),
            .detail = path,
        };
    }

    bool hasXiph = false;
    bool hasId3v2 = false;
    bool hasId3v1 = false;
    for (const auto &b : snapshot.blocks) {
        if (b.type == QStringLiteral("xiph")) {
            hasXiph = true;
            file.xiphComment(true)->setProperties(toPropertyMap(b.properties));
        } else if (b.type == QStringLiteral("id3v2")) {
            hasId3v2 = true;
            file.ID3v2Tag(true)->setProperties(toPropertyMap(b.properties));
        } else if (b.type == QStringLiteral("id3v1")) {
            hasId3v1 = true;
            file.ID3v1Tag(true)->setProperties(toPropertyMap(b.properties));
        }
    }

    int stripMask = TagLib::FLAC::File::NoTags;
    if (!hasXiph && file.hasXiphComment()) {
        stripMask |= TagLib::FLAC::File::XiphComment;
    }
    if (!hasId3v2 && file.hasID3v2Tag()) {
        stripMask |= TagLib::FLAC::File::ID3v2;
    }
    if (!hasId3v1 && file.hasID3v1Tag()) {
        stripMask |= TagLib::FLAC::File::ID3v1;
    }
    if (stripMask != TagLib::FLAC::File::NoTags) {
        file.strip(stripMask);
    }

    if (!file.save()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to save restored FLAC file"),
            .detail = path,
        };
    }
    return { };
}

core::Result<void> restoreMpeg(const QString &path, const TagSnapshot &snapshot)
{
    TagLib::MPEG::File file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open MPEG file for restore"),
            .detail = path,
        };
    }

    bool hasId3v2 = false;
    bool hasId3v1 = false;
    bool hasApe = false;
    TagLib::ID3v2::Version id3v2Ver = TagLib::ID3v2::v4;

    for (const auto &b : snapshot.blocks) {
        if (b.type == QStringLiteral("id3v2")) {
            hasId3v2 = true;
            if (b.id3v2Version == 3) {
                id3v2Ver = TagLib::ID3v2::v3;
            }
            file.ID3v2Tag(true)->setProperties(toPropertyMap(b.properties));
        } else if (b.type == QStringLiteral("id3v1")) {
            hasId3v1 = true;
            file.ID3v1Tag(true)->setProperties(toPropertyMap(b.properties));
        } else if (b.type == QStringLiteral("ape")) {
            hasApe = true;
            file.APETag(true)->setProperties(toPropertyMap(b.properties));
        }
    }

    int stripMask = TagLib::MPEG::File::NoTags;
    if (!hasId3v2 && file.hasID3v2Tag()) {
        stripMask |= TagLib::MPEG::File::ID3v2;
    }
    if (!hasId3v1 && file.hasID3v1Tag()) {
        stripMask |= TagLib::MPEG::File::ID3v1;
    }
    if (!hasApe && file.hasAPETag()) {
        stripMask |= TagLib::MPEG::File::APE;
    }
    if (stripMask != TagLib::MPEG::File::NoTags) {
        file.strip(stripMask);
    }

    int saveTags = TagLib::MPEG::File::NoTags;
    if (hasId3v2) {
        saveTags |= TagLib::MPEG::File::ID3v2;
    }
    if (hasId3v1) {
        saveTags |= TagLib::MPEG::File::ID3v1;
    }
    if (hasApe) {
        saveTags |= TagLib::MPEG::File::APE;
    }

    if (!file.save(saveTags, TagLib::File::StripNone, id3v2Ver, TagLib::File::DoNotDuplicate)) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to save restored MPEG file"),
            .detail = path,
        };
    }
    return { };
}

core::Result<void> restoreMp4(const QString &path, const TagSnapshot &snapshot)
{
    TagLib::MP4::File file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid() || file.tag() == nullptr) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open MP4 file for restore"),
            .detail = path,
        };
    }

    bool hasMp4 = false;
    for (const auto &b : snapshot.blocks) {
        if (b.type == QStringLiteral("mp4")) {
            hasMp4 = true;
            file.tag()->setProperties(toPropertyMap(b.properties));
            break;
        }
    }

    if (!hasMp4 && file.hasMP4Tag()) {
        file.strip(TagLib::MP4::File::AllTags);
    }

    if (!file.save()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to save restored MP4 file"),
            .detail = path,
        };
    }
    return { };
}

core::Result<void> restoreOggVorbis(const QString &path, const TagSnapshot &snapshot)
{
    TagLib::Ogg::Vorbis::File file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid() || file.tag() == nullptr) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open Ogg Vorbis file for restore"),
            .detail = path,
        };
    }

    bool hasXiph = false;
    for (const auto &b : snapshot.blocks) {
        if (b.type == QStringLiteral("xiph")) {
            hasXiph = true;
            file.tag()->setProperties(toPropertyMap(b.properties));
            break;
        }
    }
    if (!hasXiph) {
        file.tag()->setProperties(TagLib::PropertyMap());
    }

    if (!file.save()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to save restored Ogg Vorbis file"),
            .detail = path,
        };
    }
    return { };
}

core::Result<void> restoreOggOpus(const QString &path, const TagSnapshot &snapshot)
{
    TagLib::Ogg::Opus::File file(
        QFile::encodeName(path).constData(), false, TagLib::AudioProperties::Fast);
    if (!file.isValid() || file.tag() == nullptr) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to open Ogg Opus file for restore"),
            .detail = path,
        };
    }

    bool hasXiph = false;
    for (const auto &b : snapshot.blocks) {
        if (b.type == QStringLiteral("xiph")) {
            hasXiph = true;
            file.tag()->setProperties(toPropertyMap(b.properties));
            break;
        }
    }
    if (!hasXiph) {
        file.tag()->setProperties(TagLib::PropertyMap());
    }

    if (!file.save()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("Failed to save restored Ogg Opus file"),
            .detail = path,
        };
    }
    return { };
}

std::optional<TagLib::PropertyMap> readPrimaryPropertyMap(const QString &path, SupportedFormat fmt)
{
    const QByteArray encodedPath = QFile::encodeName(path);
    switch (fmt) {
    case SupportedFormat::Flac: {
        TagLib::FLAC::File file(encodedPath.constData(), false, TagLib::AudioProperties::Fast);
        if (file.isValid() && file.xiphComment() != nullptr) {
            return file.xiphComment()->properties();
        }
        return std::nullopt;
    }
    case SupportedFormat::Mpeg: {
        TagLib::MPEG::File file(encodedPath.constData(), false, TagLib::AudioProperties::Fast);
        if (file.isValid() && file.ID3v2Tag() != nullptr) {
            return file.ID3v2Tag()->properties();
        }
        return std::nullopt;
    }
    case SupportedFormat::Mp4: {
        TagLib::MP4::File const file(encodedPath.constData(), false, TagLib::AudioProperties::Fast);
        if (file.isValid() && file.tag() != nullptr) {
            return file.tag()->properties();
        }
        return std::nullopt;
    }
    case SupportedFormat::OggVorbis: {
        TagLib::Ogg::Vorbis::File const file(
            encodedPath.constData(), false, TagLib::AudioProperties::Fast);
        if (file.isValid() && file.tag() != nullptr) {
            return file.tag()->properties();
        }
        return std::nullopt;
    }
    case SupportedFormat::OggOpus: {
        TagLib::Ogg::Opus::File const file(
            encodedPath.constData(), false, TagLib::AudioProperties::Fast);
        if (file.isValid() && file.tag() != nullptr) {
            return file.tag()->properties();
        }
        return std::nullopt;
    }
    case SupportedFormat::None:
        return std::nullopt;
    }
    return std::nullopt;
}

QString readSingleProperty(const TagLib::PropertyMap &readMap, const TagLib::String &key)
{
    if (!readMap.contains(key) || readMap.value(key).isEmpty()) {
        return { };
    }
    return toQString(readMap.value(key).front());
}

struct NumberTotalPair {
    QString number { };
    QString total { };
};

NumberTotalPair readCombinedProperty(const TagLib::PropertyMap &readMap, const TagLib::String &key)
{
    const QString raw = readSingleProperty(readMap, key);
    if (raw.isEmpty()) {
        return { };
    }
    const qsizetype slash = raw.indexOf(u'/');
    if (slash >= 0) {
        return {
            .number = raw.left(slash).trimmed(),
            .total = raw.mid(slash + 1).trimmed(),
        };
    }
    return {
        .number = raw.trimmed(),
        .total = { },
    };
}

bool verifySimpleField(const TagLib::PropertyMap &readMap, TagField field,
    const TagLib::String &key, const QHash<TagField, QString> &values)
{
    if (!values.contains(field)) {
        return true;
    }
    const QString expected = values.value(field);
    const QString actual = readSingleProperty(readMap, key);
    return actual == expected;
}

bool verifySimpleFields(const TagLib::PropertyMap &readMap, const QHash<TagField, QString> &values)
{
    struct FieldMapping {
        TagField field;
        const char *key;
    };
    static constexpr std::array<FieldMapping, 7> kMappings { {
        { .field = TagField::Title, .key = "TITLE" },
        { .field = TagField::Artist, .key = "ARTIST" },
        { .field = TagField::Album, .key = "ALBUM" },
        { .field = TagField::AlbumArtist, .key = "ALBUMARTIST" },
        { .field = TagField::Genre, .key = "GENRE" },
        { .field = TagField::Composer, .key = "COMPOSER" },
        { .field = TagField::Year, .key = "DATE" },
    } };

    return std::ranges::all_of(kMappings,
        [&](const FieldMapping &m) { return verifySimpleField(readMap, m.field, m.key, values); });
}

bool verifyCombinedField(const TagLib::PropertyMap &readMap, TagField numField, TagField totField,
    const TagLib::String &key, const QHash<TagField, QString> &values)
{
    if (!values.contains(numField) && !values.contains(totField)) {
        return true;
    }
    const NumberTotalPair actual = readCombinedProperty(readMap, key);
    if (values.contains(numField) && actual.number != values.value(numField).trimmed()) {
        return false;
    }
    if (values.contains(totField) && actual.total != values.value(totField).trimmed()) {
        return false;
    }
    return true;
}

bool verifyXiphTrackDiscFields(
    const TagLib::PropertyMap &readMap, const QHash<TagField, QString> &values)
{
    return verifySimpleField(readMap, TagField::TrackNumber, "TRACKNUMBER", values)
        && verifySimpleField(readMap, TagField::TrackTotal, "TRACKTOTAL", values)
        && verifySimpleField(readMap, TagField::DiscNumber, "DISCNUMBER", values)
        && verifySimpleField(readMap, TagField::DiscTotal, "DISCTOTAL", values);
}

bool verifyCombinedTrackDiscFields(
    const TagLib::PropertyMap &readMap, const QHash<TagField, QString> &values)
{
    return verifyCombinedField(
               readMap, TagField::TrackNumber, TagField::TrackTotal, "TRACKNUMBER", values)
        && verifyCombinedField(
            readMap, TagField::DiscNumber, TagField::DiscTotal, "DISCNUMBER", values);
}

core::Result<void> verifyModifiedFields(
    const QString &path, SupportedFormat fmt, const QHash<TagField, QString> &values)
{
    const auto readMapOpt = readPrimaryPropertyMap(path, fmt);
    if (!readMapOpt.has_value()) {
        return core::Error {
            .code = errc::kTagWriteVerifyFailed,
            .message = QStringLiteral("Failed to read primary tag for verification"),
            .detail = path,
        };
    }

    const TagLib::PropertyMap &readMap = *readMapOpt;
    if (!verifySimpleFields(readMap, values)) {
        return core::Error {
            .code = errc::kTagWriteVerifyFailed,
            .message = QStringLiteral("Field verification failed"),
            .detail = path,
        };
    }

    const bool isXiph = (fmt == SupportedFormat::Flac || fmt == SupportedFormat::OggVorbis
        || fmt == SupportedFormat::OggOpus);
    const bool trackDiscOk = isXiph ? verifyXiphTrackDiscFields(readMap, values)
                                    : verifyCombinedTrackDiscFields(readMap, values);

    if (!trackDiscOk) {
        return core::Error {
            .code = errc::kTagWriteVerifyFailed,
            .message = QStringLiteral("Track/Disc verification failed"),
            .detail = path,
        };
    }

    return { };
}

std::optional<QStringList> stringListFromJson(const QJsonArray &arr)
{
    QStringList result;
    result.reserve(arr.size());
    for (const auto &item : arr) {
        if (!item.isString()) {
            return std::nullopt;
        }
        result.append(item.toString());
    }
    return result;
}

std::optional<QMap<QString, QStringList>> propertiesFromJson(const QJsonObject &propsObj)
{
    QMap<QString, QStringList> result;
    for (auto it = propsObj.constBegin(); it != propsObj.constEnd(); ++it) {
        if (!it.value().isArray()) {
            return std::nullopt;
        }
        const auto listOpt = stringListFromJson(it.value().toArray());
        if (!listOpt.has_value()) {
            return std::nullopt;
        }
        result.insert(it.key(), *listOpt);
    }
    return result;
}

std::optional<TagSnapshot::Block> blockFromJson(const QJsonObject &bObj)
{
    if (!bObj.contains(QStringLiteral("type")) || !bObj.value(QStringLiteral("type")).isString()) {
        return std::nullopt;
    }

    TagSnapshot::Block block;
    block.type = bObj.value(QStringLiteral("type")).toString();
    block.id3v2Version = bObj.value(QStringLiteral("id3v2Version")).toInt(0);

    if (bObj.contains(QStringLiteral("properties"))) {
        if (!bObj.value(QStringLiteral("properties")).isObject()) {
            return std::nullopt;
        }
        const auto propsOpt
            = propertiesFromJson(bObj.value(QStringLiteral("properties")).toObject());
        if (!propsOpt.has_value()) {
            return std::nullopt;
        }
        block.properties = *propsOpt;
    }

    if (bObj.contains(QStringLiteral("unsupported"))) {
        if (!bObj.value(QStringLiteral("unsupported")).isArray()) {
            return std::nullopt;
        }
        const auto unsupOpt
            = stringListFromJson(bObj.value(QStringLiteral("unsupported")).toArray());
        if (!unsupOpt.has_value()) {
            return std::nullopt;
        }
        block.unsupported = *unsupOpt;
    }

    return block;
}

} // namespace

QJsonObject TagSnapshot::toJson() const
{
    QJsonArray blocksArray;
    for (const auto &b : blocks) {
        QJsonObject bObj;
        bObj.insert(QStringLiteral("type"), b.type);
        if (b.id3v2Version > 0) {
            bObj.insert(QStringLiteral("id3v2Version"), b.id3v2Version);
        }
        QJsonObject propsObj;
        for (auto it = b.properties.cbegin(); it != b.properties.cend(); ++it) {
            QJsonArray valArr;
            for (const auto &val : it.value()) {
                valArr.append(val);
            }
            propsObj.insert(it.key(), valArr);
        }
        bObj.insert(QStringLiteral("properties"), propsObj);

        QJsonArray unsupArr;
        for (const auto &u : b.unsupported) {
            unsupArr.append(u);
        }
        bObj.insert(QStringLiteral("unsupported"), unsupArr);

        blocksArray.append(bObj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("blocks"), blocksArray);
    return root;
}

std::optional<TagSnapshot> TagSnapshot::fromJson(const QJsonObject &obj)
{
    if (!obj.contains(QStringLiteral("blocks")) || !obj.value(QStringLiteral("blocks")).isArray()) {
        return std::nullopt;
    }

    TagSnapshot snapshot;
    const QJsonArray blocksArray = obj.value(QStringLiteral("blocks")).toArray();
    for (const auto &bVal : blocksArray) {
        if (!bVal.isObject()) {
            return std::nullopt;
        }
        const auto blockOpt = blockFromJson(bVal.toObject());
        if (!blockOpt.has_value()) {
            return std::nullopt;
        }
        snapshot.blocks.append(*blockOpt);
    }
    return snapshot;
}

bool TagWriter::isSupported(const QString &path)
{
    // 只看扩展名、不打开文件：界面上按批次统计可写文件数时会对成百上千个文件调用，
    // 曲库常在网络共享上，逐个打开会卡住界面。真实格式在 snapshot / writeFields 打开时再确认。
    static const QSet<QString> s_extensions { QStringLiteral("flac"), QStringLiteral("mp3"),
        QStringLiteral("m4a"), QStringLiteral("mp4"), QStringLiteral("ogg"), QStringLiteral("oga"),
        QStringLiteral("opus") };
    return s_extensions.contains(QFileInfo(path).suffix().toLower());
}

core::Result<TagSnapshot> TagWriter::snapshot(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("File does not exist or is not a regular file"),
            .detail = path,
        };
    }

    const SupportedFormat fmt = detectFormat(path);
    if (fmt == SupportedFormat::None) {
        return core::Error {
            .code = errc::kTagWriteUnsupported,
            .message = QStringLiteral("Unsupported audio format for tag writing"),
            .detail = path,
        };
    }

    TagSnapshot snapshot;
    switch (fmt) {
    case SupportedFormat::Flac: {
        auto res = readFlacBlocks(path, snapshot);
        if (!res.ok()) {
            return res.error();
        }
        break;
    }
    case SupportedFormat::Mpeg: {
        auto res = readMpegBlocks(path, snapshot);
        if (!res.ok()) {
            return res.error();
        }
        break;
    }
    case SupportedFormat::Mp4: {
        auto res = readMp4Blocks(path, snapshot);
        if (!res.ok()) {
            return res.error();
        }
        break;
    }
    case SupportedFormat::OggVorbis: {
        auto res = readOggVorbisBlocks(path, snapshot);
        if (!res.ok()) {
            return res.error();
        }
        break;
    }
    case SupportedFormat::OggOpus: {
        auto res = readOggOpusBlocks(path, snapshot);
        if (!res.ok()) {
            return res.error();
        }
        break;
    }
    case SupportedFormat::None:
        break;
    }

    return snapshot;
}

core::Result<void> TagWriter::writeFields(
    const QString &path, const QHash<TagField, QString> &values)
{
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("File does not exist or is not a regular file"),
            .detail = path,
        };
    }

    const SupportedFormat fmt = detectFormat(path);
    if (fmt == SupportedFormat::None) {
        return core::Error {
            .code = errc::kTagWriteUnsupported,
            .message = QStringLiteral("Unsupported audio format for tag writing"),
            .detail = path,
        };
    }

    core::Result<void> writeRes;
    switch (fmt) {
    case SupportedFormat::Flac:
        writeRes = writeFlacFields(path, values);
        break;
    case SupportedFormat::Mpeg:
        writeRes = writeMpegFields(path, values);
        break;
    case SupportedFormat::Mp4:
        writeRes = writeMp4Fields(path, values);
        break;
    case SupportedFormat::OggVorbis:
        writeRes = writeOggVorbisFields(path, values);
        break;
    case SupportedFormat::OggOpus:
        writeRes = writeOggOpusFields(path, values);
        break;
    case SupportedFormat::None:
        break;
    }

    if (!writeRes.ok()) {
        return writeRes.error();
    }

    return verifyModifiedFields(path, fmt, values);
}

core::Result<void> TagWriter::restore(const QString &path, const TagSnapshot &snapshot)
{
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        return core::Error {
            .code = errc::kTagWriteFailed,
            .message = QStringLiteral("File does not exist or is not a regular file"),
            .detail = path,
        };
    }

    const SupportedFormat fmt = detectFormat(path);
    if (fmt == SupportedFormat::None) {
        return core::Error {
            .code = errc::kTagWriteUnsupported,
            .message = QStringLiteral("Unsupported audio format for tag writing"),
            .detail = path,
        };
    }

    core::Result<void> restoreRes;
    switch (fmt) {
    case SupportedFormat::Flac:
        restoreRes = restoreFlac(path, snapshot);
        break;
    case SupportedFormat::Mpeg:
        restoreRes = restoreMpeg(path, snapshot);
        break;
    case SupportedFormat::Mp4:
        restoreRes = restoreMp4(path, snapshot);
        break;
    case SupportedFormat::OggVorbis:
        restoreRes = restoreOggVorbis(path, snapshot);
        break;
    case SupportedFormat::OggOpus:
        restoreRes = restoreOggOpus(path, snapshot);
        break;
    case SupportedFormat::None:
        break;
    }

    if (!restoreRes.ok()) {
        return restoreRes.error();
    }

    const auto postSnapshotRes = TagWriter::snapshot(path);
    if (!postSnapshotRes.ok()) {
        return core::Error {
            .code = errc::kTagWriteVerifyFailed,
            .message = postSnapshotRes.error().message,
            .detail = path,
        };
    }

    if (!(postSnapshotRes.value() == snapshot)) {
        return core::Error {
            .code = errc::kTagWriteVerifyFailed,
            .message = QStringLiteral("Snapshot after restore does not match original snapshot"),
            .detail = path,
        };
    }

    return { };
}

} // namespace linernotes::library
