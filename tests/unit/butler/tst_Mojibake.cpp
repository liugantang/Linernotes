// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QTextStream>

#include <butler/Mojibake.h>
#include <common/TestSupport.h>
#include <unicode/ucnv.h>
#include <unicode/utypes.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace {

using linernotes::butler::decideGroup;
using linernotes::butler::decodeCandidates;
using linernotes::butler::GroupDecision;
using linernotes::butler::looksIrreparable;
using linernotes::butler::looksLikeMojibake;
using linernotes::butler::recoverBytes;
using linernotes::butler::SourceEncoding;

struct UConverterDeleter {
    void operator()(UConverter *cnv) const
    {
        if (cnv != nullptr) {
            ucnv_close(cnv);
        }
    }
};

using UConverterPtr = std::unique_ptr<UConverter, UConverterDeleter>;

std::optional<QByteArray> encodeWithIcu(const QString &text, const char *converterName)
{
    UErrorCode status = U_ZERO_ERROR;
    const UConverterPtr cnv(ucnv_open(converterName, &status));
    if (U_FAILURE(status) != 0 || cnv == nullptr) {
        return std::nullopt;
    }

    ucnv_setFromUCallBack(cnv.get(), UCNV_FROM_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &status);
    if (U_FAILURE(status) != 0) {
        return std::nullopt;
    }

    std::vector<UChar> u16(static_cast<size_t>(text.size()));
    for (qsizetype i = 0; i < text.size(); ++i) {
        u16.at(static_cast<size_t>(i)) = static_cast<UChar>(text.at(i).unicode());
    }

    const auto destCap = static_cast<int32_t>((text.size() * 4) + 16);
    QByteArray result;
    result.resize(destCap);

    const int32_t len = ucnv_fromUChars(
        cnv.get(), result.data(), destCap, u16.data(), static_cast<int32_t>(u16.size()), &status);
    if (U_FAILURE(status) != 0 || len < 0) {
        return std::nullopt;
    }

    result.resize(len);
    return result;
}

QString toCp1252Mojibake(const QByteArray &bytes)
{
    QString result;
    result.reserve(bytes.size());
    for (char raw : bytes) {
        const auto b = static_cast<uint8_t>(raw);
        if (b < 0x80 || b >= 0xA0) {
            result.append(QChar(char16_t { b }));
        } else {
            switch (b) {
            case 0x80:
                result.append(QChar(0x20AC));
                break;
            case 0x82:
                result.append(QChar(0x201A));
                break;
            case 0x83:
                result.append(QChar(0x0192));
                break;
            case 0x84:
                result.append(QChar(0x201E));
                break;
            case 0x85:
                result.append(QChar(0x2026));
                break;
            case 0x86:
                result.append(QChar(0x2020));
                break;
            case 0x87:
                result.append(QChar(0x2021));
                break;
            case 0x88:
                result.append(QChar(0x02C6));
                break;
            case 0x89:
                result.append(QChar(0x2030));
                break;
            case 0x8A:
                result.append(QChar(0x0160));
                break;
            case 0x8B:
                result.append(QChar(0x2039));
                break;
            case 0x8C:
                result.append(QChar(0x0152));
                break;
            case 0x8E:
                result.append(QChar(0x017D));
                break;
            case 0x91:
                result.append(QChar(0x2018));
                break;
            case 0x92:
                result.append(QChar(0x2019));
                break;
            case 0x93:
                result.append(QChar(0x201C));
                break;
            case 0x94:
                result.append(QChar(0x201D));
                break;
            case 0x95:
                result.append(QChar(0x2022));
                break;
            case 0x96:
                result.append(QChar(0x2013));
                break;
            case 0x97:
                result.append(QChar(0x2014));
                break;
            case 0x98:
                result.append(QChar(0x02DC));
                break;
            case 0x99:
                result.append(QChar(0x2122));
                break;
            case 0x9A:
                result.append(QChar(0x0161));
                break;
            case 0x9B:
                result.append(QChar(0x203A));
                break;
            case 0x9C:
                result.append(QChar(0x0153));
                break;
            case 0x9E:
                result.append(QChar(0x017E));
                break;
            case 0x9F:
                result.append(QChar(0x0178));
                break;
            default:
                result.append(QChar(char16_t { b }));
                break;
            }
        }
    }
    return result;
}

QStringList readLinesFromFixture(const QString &relativePath)
{
    const QString fullPath = linernotes::test::fixturePath(relativePath);
    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return { };
    }

    QTextStream in(&file);
    QStringList lines;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.isEmpty()) {
            lines.append(line);
        }
    }
    return lines;
}

bool isUtf8RepairCorrect(const QString &line)
{
    const QByteArray utf8Bytes = line.toUtf8();
    if (!looksLikeMojibake(QString::fromLatin1(utf8Bytes))) {
        return false;
    }
    const auto candidates = decodeCandidates(utf8Bytes);
    return !candidates.isEmpty() && candidates.first().encoding == SourceEncoding::Utf8
        && candidates.first().text == line;
}

class TstMojibake : public QObject {
    Q_OBJECT

private slots:
    void recoversLatin1AndCp1252Bytes();
    void repairAccuracyPerLanguage_data();
    void repairAccuracyPerLanguage();
    void repairAccuracyUtf8AndCp1252();
    void legitLatinNotFlagged();
    void groupDecisionUsesSharedEncoding();
    void irreparableDetection();
};

void TstMojibake::recoversLatin1AndCp1252Bytes()
{
    // 包含 ASCII、Latin-1 高位、CP1252 映射字节 (0x80, 0x96, 0x9F) 及保留未定义 C1 (0x81)
    const QByteArray originalBytes = "\x48\x65\x6c\x6c\x6f\xB0\xA1\xC0\xDE\x80\x96\x9F\x81";

    // 1. Latin-1 还原测试
    const QString latin1Str = QString::fromLatin1(originalBytes);
    const auto latin1Recovered = recoverBytes(latin1Str);
    QVERIFY(latin1Recovered.has_value());
    if (!latin1Recovered.has_value()) {
        return;
    }
    QCOMPARE(*latin1Recovered, originalBytes);

    // 2. CP1252 变体还原测试
    const QString cp1252Str = toCp1252Mojibake(originalBytes);
    const auto cp1252Recovered = recoverBytes(cp1252Str);
    QVERIFY(cp1252Recovered.has_value());
    if (!cp1252Recovered.has_value()) {
        return;
    }
    QCOMPARE(*cp1252Recovered, originalBytes);

    // 3. 含不在 CP1252 表中的 U+0100 以上字符应返回 nullopt
    const QString invalidStr1 = QStringLiteral("Hello \u0100 World");
    QVERIFY(!recoverBytes(invalidStr1).has_value());

    const QString invalidStr2 = QStringLiteral("晴天");
    QVERIFY(!recoverBytes(invalidStr2).has_value());
}

void TstMojibake::repairAccuracyPerLanguage_data()
{
    QTest::addColumn<QString>("language");
    QTest::addColumn<QString>("fixtureRelPath");
    QTest::addColumn<SourceEncoding>("expectedEncoding");
    QTest::addColumn<QString>("converterName");

    QTest::newRow("zh_hans -> GBK")
        << QStringLiteral("zh_hans") << QStringLiteral("mojibake/zh_hans.txt")
        << SourceEncoding::Gbk << QStringLiteral("windows-936");
    QTest::newRow("zh_hant -> Big5")
        << QStringLiteral("zh_hant") << QStringLiteral("mojibake/zh_hant.txt")
        << SourceEncoding::Big5 << QStringLiteral("windows-950");
    QTest::newRow("ja -> Shift-JIS") << QStringLiteral("ja") << QStringLiteral("mojibake/ja.txt")
                                     << SourceEncoding::ShiftJis << QStringLiteral("windows-31j");
    QTest::newRow("ko -> EUC-KR") << QStringLiteral("ko") << QStringLiteral("mojibake/ko.txt")
                                  << SourceEncoding::EucKr << QStringLiteral("windows-949");
}

void TstMojibake::repairAccuracyPerLanguage()
{
    QFETCH(QString, language);
    QFETCH(QString, fixtureRelPath);
    QFETCH(SourceEncoding, expectedEncoding);
    QFETCH(QString, converterName);

    const QStringList lines = readLinesFromFixture(fixtureRelPath);
    QVERIFY(!lines.isEmpty());

    int total = 0;
    int skipped = 0;
    int correctCount = 0;

    const QByteArray convNameUtf8 = converterName.toUtf8();

    for (const QString &line : lines) {
        ++total;
        const auto rawBytesOpt = encodeWithIcu(line, convNameUtf8.constData());
        if (!rawBytesOpt.has_value()) {
            ++skipped;
            continue;
        }

        const QByteArray &rawBytes = *rawBytesOpt;
        const QString mojibake = QString::fromLatin1(rawBytes);

        const bool isMoji = looksLikeMojibake(mojibake);
        const auto candidates = decodeCandidates(rawBytes);

        if (isMoji && !candidates.isEmpty()) {
            const auto &best = candidates.first();
            if (best.encoding == expectedEncoding && best.text == line) {
                ++correctCount;
            }
        }
    }

    const int tested = total - skipped;
    QVERIFY(tested > 0);
    const double accuracy = static_cast<double>(correctCount) / tested;

    qInfo("Language: %s | Total: %d, Skipped: %d, Correct: %d, Accuracy: %.2f%%",
        qUtf8Printable(language), total, skipped, correctCount, accuracy * 100.0);

    QVERIFY(accuracy >= 0.95);
}

void TstMojibake::repairAccuracyUtf8AndCp1252()
{
    // 各语言语料取前 50 行做综合测试
    const QStringList zhHans = readLinesFromFixture(QStringLiteral("mojibake/zh_hans.txt"));
    const QStringList zhHant = readLinesFromFixture(QStringLiteral("mojibake/zh_hant.txt"));
    const QStringList ja = readLinesFromFixture(QStringLiteral("mojibake/ja.txt"));
    const QStringList ko = readLinesFromFixture(QStringLiteral("mojibake/ko.txt"));

    QVERIFY(zhHans.size() >= 50);
    QVERIFY(zhHant.size() >= 50);
    QVERIFY(ja.size() >= 50);
    QVERIFY(ko.size() >= 50);

    // 1. UTF-8 -> Latin-1 乱码测试
    QStringList allUtf8Sample;
    for (int i = 0; i < 50; ++i) {
        allUtf8Sample.append(zhHans.at(i));
        allUtf8Sample.append(zhHant.at(i));
        allUtf8Sample.append(ja.at(i));
        allUtf8Sample.append(ko.at(i));
    }

    int utf8Total = 0;
    int utf8Correct = 0;
    for (const QString &line : allUtf8Sample) {
        ++utf8Total;
        if (isUtf8RepairCorrect(line)) {
            ++utf8Correct;
        }
    }

    const double utf8Accuracy = static_cast<double>(utf8Correct) / utf8Total;
    qInfo("UTF-8 Mojibake Accuracy: %d/%d (%.2f%%)", utf8Correct, utf8Total, utf8Accuracy * 100.0);
    QVERIFY(utf8Accuracy >= 0.95);

    // 2. GBK / SJIS -> Windows-1252 乱码测试
    int cp1252Total = 0;
    int cp1252Correct = 0;

    // GBK 前 50 条
    for (int i = 0; i < 50; ++i) {
        const QString &line = zhHans.at(i);
        const auto rawBytesOpt = encodeWithIcu(line, "windows-936");
        if (!rawBytesOpt.has_value()) {
            continue;
        }
        ++cp1252Total;
        const QString cp1252Moji = toCp1252Mojibake(*rawBytesOpt);
        const auto recovered = recoverBytes(cp1252Moji);
        if (recovered.has_value()) {
            const auto candidates = decodeCandidates(*recovered);
            if (!candidates.isEmpty() && candidates.first().encoding == SourceEncoding::Gbk
                && candidates.first().text == line) {
                ++cp1252Correct;
            }
        }
    }

    // SJIS 前 50 条
    for (int i = 0; i < 50; ++i) {
        const QString &line = ja.at(i);
        const auto rawBytesOpt = encodeWithIcu(line, "windows-31j");
        if (!rawBytesOpt.has_value()) {
            continue;
        }
        ++cp1252Total;
        const QString cp1252Moji = toCp1252Mojibake(*rawBytesOpt);
        const auto recovered = recoverBytes(cp1252Moji);
        if (recovered.has_value()) {
            const auto candidates = decodeCandidates(*recovered);
            if (!candidates.isEmpty() && candidates.first().encoding == SourceEncoding::ShiftJis
                && candidates.first().text == line) {
                ++cp1252Correct;
            }
        }
    }

    const double cp1252Accuracy = static_cast<double>(cp1252Correct) / cp1252Total;
    qInfo("CP1252 Mojibake Accuracy: %d/%d (%.2f%%)", cp1252Correct, cp1252Total,
        cp1252Accuracy * 100.0);
    QVERIFY(cp1252Accuracy >= 0.95);
}

void TstMojibake::legitLatinNotFlagged()
{
    const QStringList latinLines = readLinesFromFixture(QStringLiteral("mojibake/latin_legit.txt"));
    QVERIFY(latinLines.size() >= 60);

    for (const QString &line : latinLines) {
        QVERIFY2(!looksLikeMojibake(line),
            qUtf8Printable(QStringLiteral("Legit line flagged as mojibake: %1").arg(line)));
    }

    // 纯 ASCII 字符串测试
    const QStringList asciiLines {
        QStringLiteral("Hello World"),
        QStringLiteral("Pink Floyd - The Dark Side of the Moon"),
        QStringLiteral("Track 01"),
        QStringLiteral("Radiohead - OK Computer"),
        QStringLiteral("1234567890"),
        QStringLiteral("Why?"),
        QStringLiteral("What's Up?"),
    };
    for (const QString &s : asciiLines) {
        QVERIFY(!looksLikeMojibake(s));
    }

    // 正常 CJK 字符串测试
    const QStringList cjkLines {
        QStringLiteral("周杰伦 - 晴天"),
        QStringLiteral("宇多田ヒカル - First Love"),
        QStringLiteral("아이유 - 좋은 날"),
        QStringLiteral("林晓风 - 晚风里的歌"),
        QStringLiteral("雨の日の散歩"),
    };
    for (const QString &s : cjkLines) {
        QVERIFY(!looksLikeMojibake(s));
    }
}

void TstMojibake::groupDecisionUsesSharedEncoding()
{
    const QStringList zhHant = readLinesFromFixture(QStringLiteral("mojibake/zh_hant.txt"));
    QVERIFY(zhHant.size() >= 10);

    // 选取 10 条繁体中文作为同一专辑
    QList<QByteArray> albumItems;
    QStringList originalZhHant;
    for (int i = 0; i < 10; ++i) {
        const QString &line = zhHant.at(i);
        const auto rawBytes = encodeWithIcu(line, "windows-950");
        QVERIFY(rawBytes.has_value());
        if (!rawBytes.has_value()) {
            return;
        }
        albumItems.append(*rawBytes);
        originalZhHant.append(line);
    }

    const GroupDecision decision = decideGroup(albumItems);
    QVERIFY(decision.encoding.has_value());
    if (!decision.encoding.has_value()) {
        return;
    }
    QCOMPARE(*decision.encoding, SourceEncoding::Big5);
    QCOMPARE(decision.texts.size(), albumItems.size());
    for (qsizetype i = 0; i < decision.texts.size(); ++i) {
        const auto &text = decision.texts.at(i);
        QVERIFY(text.has_value());
        if (!text.has_value()) {
            return;
        }
        QCOMPARE(*text, originalZhHant.at(i));
    }
    QVERIFY(!decision.ambiguous);

    // 混合语言组测试：5 条 GBK + 5 条 Shift-JIS，应该判定为 ambiguous
    const QStringList zhHans = readLinesFromFixture(QStringLiteral("mojibake/zh_hans.txt"));
    const QStringList ja = readLinesFromFixture(QStringLiteral("mojibake/ja.txt"));
    QVERIFY(zhHans.size() >= 5);
    QVERIFY(ja.size() >= 5);

    QList<QByteArray> mixedItems;
    for (int i = 0; i < 5; ++i) {
        const auto gbkBytes = encodeWithIcu(zhHans.at(i), "windows-936");
        QVERIFY(gbkBytes.has_value());
        if (!gbkBytes.has_value()) {
            return;
        }
        mixedItems.append(*gbkBytes);
    }
    for (int i = 0; i < 5; ++i) {
        const auto sjisBytes = encodeWithIcu(ja.at(i), "windows-31j");
        QVERIFY(sjisBytes.has_value());
        if (!sjisBytes.has_value()) {
            return;
        }
        mixedItems.append(*sjisBytes);
    }

    const GroupDecision mixedDecision = decideGroup(mixedItems);
    QVERIFY(mixedDecision.ambiguous);
}

void TstMojibake::irreparableDetection()
{
    // 问号损坏
    QVERIFY(looksIrreparable(QStringLiteral("???")));
    QVERIFY(looksIrreparable(QStringLiteral("??? - ??")));
    QVERIFY(looksIrreparable(QStringLiteral("周??")));
    QVERIFY(looksIrreparable(QStringLiteral("??")));

    // 正常标题不应判定为损坏
    QVERIFY(!looksIrreparable(QStringLiteral("Why?")));
    QVERIFY(!looksIrreparable(QStringLiteral("What's Up?")));
    QVERIFY(!looksIrreparable(QStringLiteral("晴天")));
    QVERIFY(!looksIrreparable(QStringLiteral("?")));
    QVERIFY(!looksIrreparable(QStringLiteral("")));
}

} // namespace

QTEST_GUILESS_MAIN(TstMojibake)
#include "tst_Mojibake.moc"
