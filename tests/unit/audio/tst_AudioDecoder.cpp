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
    void decodesWithStartMsAndMaxDuration();
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

void TstAudioDecoder::decodesWithStartMsAndMaxDuration()
{
    const QString path = fixturePath(QStringLiteral("audio/melody_8s.flac"));
    const DecodeOptions fullOpts {
        .sampleRate = 44100,
        .channels = 1,
        .maxDurationMs = 0,
        .startMs = 0,
    };

    const auto fullRes = AudioDecoder::decode(path, fullOpts);
    QVERIFY(fullRes.ok());
    const auto &fullPcm = fullRes.value();
    QVERIFY(fullPcm.durationMs() >= 7500);

    const DecodeOptions seekOpts {
        .sampleRate = 44100,
        .channels = 1,
        .maxDurationMs = 1000,
        .startMs = 2000,
    };

    const auto seekRes = AudioDecoder::decode(path, seekOpts);
    QVERIFY(seekRes.ok());
    const auto &seekPcm = seekRes.value();

    const qint64 duration = seekPcm.durationMs();
    QVERIFY(duration >= 950 && duration <= 1050);

    // Compare with full decode slice at 2.0s
    const qsizetype offsetSamples = 2000 * 44100 / 1000;
    const qsizetype compareCount
        = std::min(seekPcm.samples.size(), fullPcm.samples.size() - offsetSamples);
    QVERIFY(compareCount > 40000);

    double dot = 0.0;
    double normFull = 0.0;
    double normSeek = 0.0;
    const qsizetype skipHead = 64;
    for (qsizetype i = skipHead; i < compareCount; ++i) {
        const auto sFull = static_cast<double>(fullPcm.samples.at(offsetSamples + i));
        const auto sSeek = static_cast<double>(seekPcm.samples.at(i));
        dot += sFull * sSeek;
        normFull += sFull * sFull;
        normSeek += sSeek * sSeek;
    }
    const double corr = (normFull > 0.0 && normSeek > 0.0)
        ? (dot / (std::sqrt(normFull) * std::sqrt(normSeek)))
        : 0.0;
    QVERIFY(corr > 0.99);
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
