// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TagReader.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMap>
#include <QString>

#include <aifffile.h>
#include <apefile.h>
#include <apetag.h>
#include <asffile.h>
#include <asftag.h>
#include <commentsframe.h>
#include <dsdifffile.h>
#include <dsffile.h>
#include <dsfproperties.h>
#include <fileref.h>
#include <flacfile.h>
#include <id3v1tag.h>
#include <id3v2tag.h>
#include <infotag.h>
#include <library/Errors.h>
#include <library/LibraryLogging.h>
#include <mp4file.h>
#include <mp4properties.h>
#include <mp4tag.h>
#include <mpegfile.h>
#include <oggfile.h>
#include <oggflacfile.h>
#include <opusfile.h>
#include <rifffile.h>
#include <speexfile.h>
#include <tbytevector.h>
#include <textidentificationframe.h>
#include <tpropertymap.h>
#include <tstring.h>
#include <tstringlist.h>
#include <unsynchronizedlyricsframe.h>
#include <vorbisfile.h>
#include <wavfile.h>
#include <wavpackfile.h>
#include <xiphcomment.h>

namespace linernotes::library {

namespace {

QByteArray stripTrailingNullAndSpaces(const QByteArray &bytes)
{
    qsizetype len = bytes.size();
    while (len > 0) {
        const char ch = bytes.at(len - 1);
        if (ch == '\0' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            --len;
        } else {
            break;
        }
    }
    return bytes.left(len);
}

struct Id3v1RawFields {
    bool valid = false;
    QByteArray title;
    QByteArray artist;
    QByteArray album;
    QByteArray year;
    QByteArray comment;
};

Id3v1RawFields readId3v1RawBytes(const QString &path)
{
    Id3v1RawFields fields;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fields;
    }
    if (file.size() < 128) {
        return fields;
    }
    if (!file.seek(file.size() - 128)) {
        return fields;
    }
    const QByteArray block = file.read(128);
    if (block.size() != 128 || !block.startsWith("TAG")) {
        return fields;
    }

    fields.valid = true;
    fields.title = stripTrailingNullAndSpaces(block.mid(3, 30));
    fields.artist = stripTrailingNullAndSpaces(block.mid(33, 30));
    fields.album = stripTrailingNullAndSpaces(block.mid(63, 30));
    fields.year = stripTrailingNullAndSpaces(block.mid(93, 4));

    if (block.at(125) == '\0' && block.at(126) != '\0') {
        fields.comment = stripTrailingNullAndSpaces(block.mid(97, 28));
    } else {
        fields.comment = stripTrailingNullAndSpaces(block.mid(97, 30));
    }
    return fields;
}

QMap<std::pair<QString, int>, QByteArray> extractLatin1RawMap(TagLib::ID3v2::Tag *tag)
{
    QMap<std::pair<QString, int>, QByteArray> latin1RawMap;
    if (tag == nullptr) {
        return latin1RawMap;
    }

    QMap<QString, int> keyCounts;
    for (auto *frame : tag->frameList()) {
        if (frame == nullptr) {
            continue;
        }

        bool isLatin1 = false;
        TagLib::PropertyMap frameProps;

        if (auto *textFrame = dynamic_cast<TagLib::ID3v2::TextIdentificationFrame *>(frame);
            textFrame != nullptr) {
            isLatin1 = (textFrame->textEncoding() == TagLib::String::Latin1);
            frameProps = textFrame->asProperties();
        } else if (auto *commFrame = dynamic_cast<TagLib::ID3v2::CommentsFrame *>(frame);
            commFrame != nullptr) {
            isLatin1 = (commFrame->textEncoding() == TagLib::String::Latin1);
            frameProps = commFrame->asProperties();
        } else if (auto *lyricsFrame
            = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame *>(frame);
            lyricsFrame != nullptr) {
            isLatin1 = (lyricsFrame->textEncoding() == TagLib::String::Latin1);
            frameProps = lyricsFrame->asProperties();
        }

        if (!isLatin1) {
            continue;
        }

        for (const auto &[fKey, fValues] : frameProps) {
            const QString qKey = QString::fromUtf8(fKey.toCString(true)).toUpper();
            for (const auto &val : fValues) {
                const int ord = keyCounts.value(qKey, 0);
                keyCounts.insert(qKey, ord + 1);
                const TagLib::ByteVector bv = val.data(TagLib::String::Latin1);
                latin1RawMap.insert(
                    { qKey, ord }, QByteArray(bv.data(), static_cast<qsizetype>(bv.size())));
            }
        }
    }
    return latin1RawMap;
}

void extractId3v2Tags(TagLib::ID3v2::Tag *tag, int priority, QList<RawTag> &outTags)
{
    if (tag == nullptr) {
        return;
    }

    const auto latin1RawMap = extractLatin1RawMap(tag);
    const TagLib::PropertyMap propMap = tag->properties();
    for (const auto &[pKey, pValues] : propMap) {
        const QString qKey = QString::fromUtf8(pKey.toCString(true)).toUpper();
        int ord = 0;
        for (const auto &pVal : pValues) {
            const QString val = QString::fromUtf8(pVal.toCString(true));
            if (!val.isEmpty()) {
                RawTag raw;
                raw.tagType = QStringLiteral("id3v2");
                raw.priority = priority;
                raw.key = qKey;
                raw.ordinal = ord;
                raw.value = val;

                const auto it = latin1RawMap.constFind({ qKey, ord });
                if (it != latin1RawMap.constEnd()) {
                    raw.rawBytes = it.value();
                    raw.rawEncoding = QStringLiteral("latin1");
                }
                outTags.append(raw);
            }
            ++ord;
        }
    }
}

void extractId3v1Tags(
    TagLib::ID3v1::Tag *tag, int priority, const Id3v1RawFields &rawFields, QList<RawTag> &outTags)
{
    if (tag == nullptr) {
        return;
    }

    const TagLib::PropertyMap propMap = tag->properties();
    for (const auto &[pKey, pValues] : propMap) {
        const QString qKey = QString::fromUtf8(pKey.toCString(true)).toUpper();
        int ord = 0;
        for (const auto &pVal : pValues) {
            const QString val = QString::fromUtf8(pVal.toCString(true));
            if (!val.isEmpty()) {
                RawTag raw;
                raw.tagType = QStringLiteral("id3v1");
                raw.priority = priority;
                raw.key = qKey;
                raw.ordinal = ord;
                raw.value = val;

                if (rawFields.valid) {
                    if (qKey == QStringLiteral("TITLE") && !rawFields.title.isEmpty()) {
                        raw.rawBytes = rawFields.title;
                        raw.rawEncoding = QStringLiteral("latin1");
                    } else if (qKey == QStringLiteral("ARTIST") && !rawFields.artist.isEmpty()) {
                        raw.rawBytes = rawFields.artist;
                        raw.rawEncoding = QStringLiteral("latin1");
                    } else if (qKey == QStringLiteral("ALBUM") && !rawFields.album.isEmpty()) {
                        raw.rawBytes = rawFields.album;
                        raw.rawEncoding = QStringLiteral("latin1");
                    } else if (qKey == QStringLiteral("DATE") && !rawFields.year.isEmpty()) {
                        raw.rawBytes = rawFields.year;
                        raw.rawEncoding = QStringLiteral("latin1");
                    } else if (qKey == QStringLiteral("COMMENT") && !rawFields.comment.isEmpty()) {
                        raw.rawBytes = rawFields.comment;
                        raw.rawEncoding = QStringLiteral("latin1");
                    }
                }
                outTags.append(raw);
            }
            ++ord;
        }
    }
}

void extractGenericProperties(const TagLib::PropertyMap &propMap, const QString &tagType,
    int priority, QList<RawTag> &outTags)
{
    for (const auto &[pKey, pValues] : propMap) {
        const QString qKey = QString::fromUtf8(pKey.toCString(true)).toUpper();
        int ord = 0;
        for (const auto &pVal : pValues) {
            const QString val = QString::fromUtf8(pVal.toCString(true));
            if (!val.isEmpty()) {
                RawTag raw;
                raw.tagType = tagType;
                raw.priority = priority;
                raw.key = qKey;
                raw.ordinal = ord;
                raw.value = val;
                outTags.append(raw);
            }
            ++ord;
        }
    }
}

bool checkFlacCover(TagLib::FLAC::File *flac)
{
    return flac != nullptr && !flac->pictureList().isEmpty();
}

bool checkMpegCover(TagLib::MPEG::File *mpeg)
{
    if (mpeg == nullptr) {
        return false;
    }
    if (mpeg->hasID3v2Tag() && mpeg->ID3v2Tag() != nullptr) {
        if (!mpeg->ID3v2Tag()->frameList("APIC").isEmpty()
            || mpeg->ID3v2Tag()->complexPropertyKeys().contains("PICTURE")) {
            return true;
        }
    }
    return mpeg->hasAPETag() && mpeg->APETag() != nullptr
        && mpeg->APETag()->complexPropertyKeys().contains("PICTURE");
}

bool checkMp4Cover(TagLib::MP4::File *mp4)
{
    return mp4 != nullptr && mp4->hasMP4Tag() && mp4->tag() != nullptr
        && (mp4->tag()->contains("covr") || mp4->tag()->complexPropertyKeys().contains("PICTURE"));
}

bool checkApeCover(TagLib::File *file)
{
    if (auto *wv = dynamic_cast<TagLib::WavPack::File *>(file); wv != nullptr) {
        return wv->hasAPETag() && wv->APETag() != nullptr
            && wv->APETag()->complexPropertyKeys().contains("PICTURE");
    }
    if (auto *ape = dynamic_cast<TagLib::APE::File *>(file); ape != nullptr) {
        return ape->hasAPETag() && ape->APETag() != nullptr
            && ape->APETag()->complexPropertyKeys().contains("PICTURE");
    }
    return false;
}

bool checkAsfCover(TagLib::ASF::File *asf)
{
    return asf != nullptr && asf->tag() != nullptr
        && (asf->tag()->attributeListMap().contains("WM/Picture")
            || asf->tag()->complexPropertyKeys().contains("PICTURE"));
}

bool checkEmbeddedCover(TagLib::FileRef &fileRef, TagLib::File *file)
{
    if (fileRef.complexPropertyKeys().contains("PICTURE")) {
        return true;
    }
    if (file != nullptr) {
        if (file->complexPropertyKeys().contains("PICTURE")) {
            return true;
        }
        if (file->tag() != nullptr && file->tag()->complexPropertyKeys().contains("PICTURE")) {
            return true;
        }
    }
    if (checkFlacCover(dynamic_cast<TagLib::FLAC::File *>(file))) {
        return true;
    }
    if (checkMpegCover(dynamic_cast<TagLib::MPEG::File *>(file))) {
        return true;
    }
    if (checkMp4Cover(dynamic_cast<TagLib::MP4::File *>(file))) {
        return true;
    }
    if (checkApeCover(file)) {
        return true;
    }
    return checkAsfCover(dynamic_cast<TagLib::ASF::File *>(file));
}

bool detectLossyAudio(TagLib::File *file, AudioProperties &outAudio)
{
    if (dynamic_cast<TagLib::MPEG::File *>(file) != nullptr) {
        outAudio.container = QStringLiteral("mp3");
        outAudio.codec = QStringLiteral("mp3");
        outAudio.bitDepth = 0;
        return true;
    }
    if (dynamic_cast<TagLib::Ogg::Vorbis::File *>(file) != nullptr) {
        outAudio.container = QStringLiteral("ogg");
        outAudio.codec = QStringLiteral("vorbis");
        outAudio.bitDepth = 0;
        return true;
    }
    if (dynamic_cast<TagLib::Ogg::Opus::File *>(file) != nullptr) {
        outAudio.container = QStringLiteral("opus");
        outAudio.codec = QStringLiteral("opus");
        outAudio.bitDepth = 0;
        return true;
    }
    if (dynamic_cast<TagLib::Ogg::FLAC::File *>(file) != nullptr) {
        outAudio.container = QStringLiteral("ogg");
        outAudio.codec = QStringLiteral("flac");
        outAudio.bitDepth = 0;
        return true;
    }
    if (dynamic_cast<TagLib::ASF::File *>(file) != nullptr) {
        outAudio.container = QStringLiteral("asf");
        outAudio.codec = QStringLiteral("wma");
        outAudio.bitDepth = 0;
        return true;
    }
    return false;
}

bool detectRiffOrFlacAudio(TagLib::File *file, AudioProperties &outAudio)
{
    if (auto *flac = dynamic_cast<TagLib::FLAC::File *>(file); flac != nullptr) {
        outAudio.container = QStringLiteral("flac");
        outAudio.codec = QStringLiteral("flac");
        if (auto *flacProps = flac->audioProperties(); flacProps != nullptr) {
            outAudio.bitDepth = flacProps->bitsPerSample();
        }
        return true;
    }
    if (auto *mp4 = dynamic_cast<TagLib::MP4::File *>(file); mp4 != nullptr) {
        outAudio.container = QStringLiteral("mp4");
        outAudio.codec = QStringLiteral("aac");
        outAudio.bitDepth = 0;
        if (auto *mp4Props = mp4->audioProperties(); mp4Props != nullptr) {
            if (mp4Props->codec() == TagLib::MP4::Properties::ALAC) {
                outAudio.codec = QStringLiteral("alac");
                outAudio.bitDepth = mp4Props->bitsPerSample();
            }
        }
        return true;
    }
    if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(file); wav != nullptr) {
        outAudio.container = QStringLiteral("wav");
        outAudio.codec = QStringLiteral("pcm");
        if (auto *wavProps = wav->audioProperties(); wavProps != nullptr) {
            outAudio.bitDepth = wavProps->bitsPerSample();
        }
        return true;
    }
    if (auto *aiff = dynamic_cast<TagLib::RIFF::AIFF::File *>(file); aiff != nullptr) {
        outAudio.container = QStringLiteral("aiff");
        outAudio.codec = QStringLiteral("pcm");
        if (auto *aiffProps = aiff->audioProperties(); aiffProps != nullptr) {
            outAudio.bitDepth = aiffProps->bitsPerSample();
        }
        return true;
    }
    return false;
}

bool detectOtherLosslessAudio(TagLib::File *file, AudioProperties &outAudio)
{
    if (auto *wv = dynamic_cast<TagLib::WavPack::File *>(file); wv != nullptr) {
        outAudio.container = QStringLiteral("wavpack");
        outAudio.codec = (wv->audioProperties() != nullptr && wv->audioProperties()->isDsd())
            ? QStringLiteral("dsd")
            : QStringLiteral("wavpack");
        if (auto *wvProps = wv->audioProperties(); wvProps != nullptr) {
            outAudio.bitDepth = wvProps->bitsPerSample();
        }
        return true;
    }
    if (auto *ape = dynamic_cast<TagLib::APE::File *>(file); ape != nullptr) {
        outAudio.container = QStringLiteral("ape");
        outAudio.codec = QStringLiteral("ape");
        if (auto *apeProps = ape->audioProperties(); apeProps != nullptr) {
            outAudio.bitDepth = apeProps->bitsPerSample();
        }
        return true;
    }
    if (auto *dsf = dynamic_cast<TagLib::DSF::File *>(file); dsf != nullptr) {
        outAudio.container = QStringLiteral("dsf");
        outAudio.codec = QStringLiteral("dsd");
        if (auto *dsfProps = dsf->audioProperties(); dsfProps != nullptr) {
            outAudio.bitDepth = dsfProps->bitsPerSample();
        }
        return true;
    }
    if (dynamic_cast<TagLib::DSDIFF::File *>(file) != nullptr) {
        outAudio.container = QStringLiteral("dff");
        outAudio.codec = QStringLiteral("dsd");
        outAudio.bitDepth = 0;
        return true;
    }
    return false;
}

bool detectLosslessAudio(TagLib::File *file, AudioProperties &outAudio)
{
    return detectRiffOrFlacAudio(file, outAudio) || detectOtherLosslessAudio(file, outAudio);
}

void detectAudioProperties(TagLib::File *file, const TagLib::AudioProperties *props,
    const QFileInfo &fileInfo, AudioProperties &outAudio)
{
    outAudio.durationMs = props->lengthInMilliseconds();
    outAudio.bitrate = props->bitrate();
    outAudio.sampleRate = props->sampleRate();
    outAudio.channels = props->channels();

    if (detectLossyAudio(file, outAudio)) {
        return;
    }
    if (detectLosslessAudio(file, outAudio)) {
        return;
    }
    outAudio.container = fileInfo.suffix().toLower();
    outAudio.codec = outAudio.container;
    outAudio.bitDepth = 0;
}

void extractMpegTags(
    TagLib::MPEG::File *mpeg, const Id3v1RawFields &id3v1Fields, QList<RawTag> &outTags)
{
    if (mpeg->hasID3v2Tag() && mpeg->ID3v2Tag() != nullptr) {
        extractId3v2Tags(mpeg->ID3v2Tag(), 0, outTags);
    }
    if (mpeg->hasAPETag() && mpeg->APETag() != nullptr) {
        extractGenericProperties(mpeg->APETag()->properties(), QStringLiteral("ape"), 1, outTags);
    }
    if (mpeg->hasID3v1Tag() && mpeg->ID3v1Tag() != nullptr) {
        extractId3v1Tags(mpeg->ID3v1Tag(), 9, id3v1Fields, outTags);
    }
}

void extractFlacTags(
    TagLib::FLAC::File *flac, const Id3v1RawFields &id3v1Fields, QList<RawTag> &outTags)
{
    if (flac->hasXiphComment() && flac->xiphComment() != nullptr) {
        extractGenericProperties(
            flac->xiphComment()->properties(), QStringLiteral("xiph"), 0, outTags);
    }
    if (flac->hasID3v2Tag() && flac->ID3v2Tag() != nullptr) {
        extractId3v2Tags(flac->ID3v2Tag(), 1, outTags);
    }
    if (flac->hasID3v1Tag() && flac->ID3v1Tag() != nullptr) {
        extractId3v1Tags(flac->ID3v1Tag(), 9, id3v1Fields, outTags);
    }
}

void extractRiffTags(TagLib::File *file, QList<RawTag> &outTags)
{
    if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(file); wav != nullptr) {
        if (wav->hasID3v2Tag() && wav->ID3v2Tag() != nullptr) {
            extractId3v2Tags(wav->ID3v2Tag(), 0, outTags);
        }
        if (wav->hasInfoTag() && wav->InfoTag() != nullptr) {
            extractGenericProperties(
                wav->InfoTag()->properties(), QStringLiteral("riff"), 1, outTags);
        }
    } else if (auto *aiff = dynamic_cast<TagLib::RIFF::AIFF::File *>(file); aiff != nullptr) {
        if (aiff->hasID3v2Tag() && aiff->tag() != nullptr) {
            extractId3v2Tags(aiff->tag(), 0, outTags);
        }
    }
}

void extractWavPackTags(
    TagLib::WavPack::File *wv, const Id3v1RawFields &id3v1Fields, QList<RawTag> &outTags)
{
    if (wv->hasAPETag() && wv->APETag() != nullptr) {
        extractGenericProperties(wv->APETag()->properties(), QStringLiteral("ape"), 0, outTags);
    }
    if (wv->hasID3v1Tag() && wv->ID3v1Tag() != nullptr) {
        extractId3v1Tags(wv->ID3v1Tag(), 9, id3v1Fields, outTags);
    }
}

void extractApeTags(
    TagLib::APE::File *ape, const Id3v1RawFields &id3v1Fields, QList<RawTag> &outTags)
{
    if (ape->hasAPETag() && ape->APETag() != nullptr) {
        extractGenericProperties(ape->APETag()->properties(), QStringLiteral("ape"), 0, outTags);
    }
    if (ape->hasID3v1Tag() && ape->ID3v1Tag() != nullptr) {
        extractId3v1Tags(ape->ID3v1Tag(), 9, id3v1Fields, outTags);
    }
}

void extractDsdTags(TagLib::File *file, QList<RawTag> &outTags)
{
    if (auto *dsf = dynamic_cast<TagLib::DSF::File *>(file); dsf != nullptr) {
        if (dsf->tag() != nullptr) {
            extractId3v2Tags(dsf->tag(), 0, outTags);
        }
    } else if (auto *dsdiff = dynamic_cast<TagLib::DSDIFF::File *>(file); dsdiff != nullptr) {
        if (dsdiff->ID3v2Tag() != nullptr) {
            extractId3v2Tags(dsdiff->ID3v2Tag(), 0, outTags);
        }
    }
}

void extractOggFamilyTags(TagLib::File *file, QList<RawTag> &outTags)
{
    if (auto *vorbis = dynamic_cast<TagLib::Ogg::Vorbis::File *>(file); vorbis != nullptr) {
        if (vorbis->tag() != nullptr) {
            extractGenericProperties(
                vorbis->tag()->properties(), QStringLiteral("xiph"), 0, outTags);
        }
    } else if (auto *opus = dynamic_cast<TagLib::Ogg::Opus::File *>(file); opus != nullptr) {
        if (opus->tag() != nullptr) {
            extractGenericProperties(opus->tag()->properties(), QStringLiteral("xiph"), 0, outTags);
        }
    } else if (auto *oggFlac = dynamic_cast<TagLib::Ogg::FLAC::File *>(file); oggFlac != nullptr) {
        if (oggFlac->tag() != nullptr) {
            extractGenericProperties(
                oggFlac->tag()->properties(), QStringLiteral("xiph"), 0, outTags);
        }
    }
}

void extractAllTags(
    TagLib::File *file, TagLib::FileRef &fileRef, const QString &path, QList<RawTag> &outTags)
{
    Id3v1RawFields id3v1Fields;
    auto getId3v1Fields = [&]() -> const Id3v1RawFields & {
        if (!id3v1Fields.valid) {
            id3v1Fields = readId3v1RawBytes(path);
        }
        return id3v1Fields;
    };

    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file); mpeg != nullptr) {
        extractMpegTags(mpeg, getId3v1Fields(), outTags);
    } else if (auto *flac = dynamic_cast<TagLib::FLAC::File *>(file); flac != nullptr) {
        extractFlacTags(flac, getId3v1Fields(), outTags);
    } else if (dynamic_cast<TagLib::Ogg::File *>(file) != nullptr) {
        extractOggFamilyTags(file, outTags);
    } else if (auto *mp4 = dynamic_cast<TagLib::MP4::File *>(file); mp4 != nullptr) {
        if (mp4->hasMP4Tag() && mp4->tag() != nullptr) {
            extractGenericProperties(mp4->tag()->properties(), QStringLiteral("mp4"), 0, outTags);
        }
    } else if (dynamic_cast<TagLib::RIFF::WAV::File *>(file) != nullptr
        || dynamic_cast<TagLib::RIFF::AIFF::File *>(file) != nullptr) {
        extractRiffTags(file, outTags);
    } else if (auto *wv = dynamic_cast<TagLib::WavPack::File *>(file); wv != nullptr) {
        extractWavPackTags(wv, getId3v1Fields(), outTags);
    } else if (auto *ape = dynamic_cast<TagLib::APE::File *>(file); ape != nullptr) {
        extractApeTags(ape, getId3v1Fields(), outTags);
    } else if (auto *asf = dynamic_cast<TagLib::ASF::File *>(file); asf != nullptr) {
        if (asf->tag() != nullptr) {
            extractGenericProperties(asf->tag()->properties(), QStringLiteral("asf"), 0, outTags);
        }
    } else if (dynamic_cast<TagLib::DSF::File *>(file) != nullptr
        || dynamic_cast<TagLib::DSDIFF::File *>(file) != nullptr) {
        extractDsdTags(file, outTags);
    } else {
        extractGenericProperties(fileRef.properties(), QStringLiteral("other"), 5, outTags);
    }
}

} // namespace

core::Result<TagReadResult> TagReader::read(const QString &path)
{
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qCDebug(lcLibrary) << "File does not exist or is not a file:" << path;
        return core::Error {
            .code = errc::kTagRead,
            .message = QStringLiteral("File does not exist or is not a regular file"),
            .detail = path,
        };
    }

    if (fileInfo.size() == 0) {
        qCDebug(lcLibrary) << "File is empty:" << path;
        return core::Error {
            .code = errc::kTagUnsupported,
            .message = QStringLiteral("File is empty"),
            .detail = path,
        };
    }

    TagLib::FileRef fileRef(
        QFile::encodeName(path).constData(), true, TagLib::AudioProperties::Average);
    if (fileRef.isNull() || fileRef.file() == nullptr || !fileRef.file()->isValid()) {
        qCDebug(lcLibrary) << "Unsupported audio format or corrupt file:" << path;
        return core::Error {
            .code = errc::kTagUnsupported,
            .message = QStringLiteral("Unsupported format or corrupt audio file"),
            .detail = path,
        };
    }

    const TagLib::AudioProperties *props = fileRef.audioProperties();
    if (props == nullptr) {
        qCDebug(lcLibrary) << "Unable to read audio properties:" << path;
        return core::Error {
            .code = errc::kTagUnsupported,
            .message = QStringLiteral("Unable to read audio properties"),
            .detail = path,
        };
    }

    TagLib::File *file = fileRef.file();
    TagReadResult result;

    detectAudioProperties(file, props, fileInfo, result.audio);
    extractAllTags(file, fileRef, path, result.tags);
    result.hasEmbeddedCover = checkEmbeddedCover(fileRef, file);

    return result;
}

} // namespace linernotes::library
