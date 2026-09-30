// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>

#include <audio/AudioDecoder.h>
#include <common/TestSupport.h>

namespace {

using linernotes::audio::AudioDecoder;
using linernotes::audio::DecodeOptions;
using linernotes::test::fixturePath;

class TstAudioDecoder : public QObject {
    Q_OBJECT

private slots:
    void decodesToneToMonoCustomSampleRate();
    void decodesWithMaxDuration();
    void corruptFileReturnsError();
    void nonexistentPathReturnsError();
};

void TstAudioDecoder::decodesToneToMonoCustomSampleRate()
{
    const QString path = fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const DecodeOptions opts {
        .sampleRate = 11025,
        .channels = 1,
        .maxDurationMs = 0,
    };

    const auto res = AudioDecoder::decode(path, opts);
    QVERIFY(res.ok());

    const auto &pcm = res.value();
    QCOMPARE(pcm.sampleRate, 11025);
    QCOMPARE(pcm.channels, 1);

    const qint64 duration = pcm.durationMs();
    QVERIFY(duration >= 950 && duration <= 1050);
}

void TstAudioDecoder::decodesWithMaxDuration()
{
    const QString path = fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    const DecodeOptions opts {
        .sampleRate = 11025,
        .channels = 1,
        .maxDurationMs = 500,
    };

    const auto res = AudioDecoder::decode(path, opts);
    QVERIFY(res.ok());

    const auto &pcm = res.value();
    const qint64 duration = pcm.durationMs();
    QVERIFY(duration >= 450 && duration <= 550);
}

void TstAudioDecoder::corruptFileReturnsError()
{
    const QString path = fixturePath(QStringLiteral("audio/corrupt.flac"));
    const DecodeOptions opts {
        .sampleRate = 11025,
        .channels = 1,
        .maxDurationMs = 0,
    };

    const auto res = AudioDecoder::decode(path, opts);
    QVERIFY(!res.ok());
}

void TstAudioDecoder::nonexistentPathReturnsError()
{
    const DecodeOptions opts {
        .sampleRate = 11025,
        .channels = 1,
        .maxDurationMs = 0,
    };

    const auto res = AudioDecoder::decode(QStringLiteral("/path/to/nonexistent/sample.flac"), opts);
    QVERIFY(!res.ok());
}

} // namespace

QTEST_GUILESS_MAIN(TstAudioDecoder)

#include "tst_AudioDecoder.moc"
