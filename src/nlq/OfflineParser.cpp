// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "OfflineParser.h"

#include <QRegularExpression>

#include <algorithm>
#include <array>

namespace linernotes::nlq {

namespace {

bool tryMatchAndStripAll(QString &text, const QStringList &patterns)
{
    bool anyMatched = false;
    for (const auto &p : patterns) {
        if (p.startsWith(QLatin1String(R"(\b)"))) {
            const QRegularExpression re(p, QRegularExpression::CaseInsensitiveOption);
            while (true) {
                const auto m = re.match(text);
                if (!m.hasMatch()) {
                    break;
                }
                text.replace(m.capturedStart(0), m.capturedLength(0), QStringLiteral(" "));
                anyMatched = true;
            }
        } else {
            while (true) {
                const qsizetype idx = text.indexOf(p, 0, Qt::CaseInsensitive);
                if (idx < 0) {
                    break;
                }
                text.replace(idx, p.size(), QStringLiteral(" "));
                anyMatched = true;
            }
        }
    }
    return anyMatched;
}

bool matchLimit(QString &text, int &limit, QStringList &tags)
{
    static const QRegularExpression s_prefixLimit(
        QStringLiteral(
            R"((?:\b(?:top|limit)\s*|前\s*)(\d+)(?:\s*(?:tracks?\b|songs?\b|首歌曲?|首曲目|首|张专辑|张|个艺人|个))?)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression s_suffixLimit(
        QStringLiteral(
            R"((?:\b|(?<=\D))(\d+)\s*(?:tracks?\b|songs?\b|首歌曲?|首曲目|首|张专辑|张|个艺人|个))"),
        QRegularExpression::CaseInsensitiveOption);

    const auto mPrefix = s_prefixLimit.match(text);
    if (mPrefix.hasMatch()) {
        const int val = mPrefix.captured(1).toInt();
        if (val > 0) {
            limit = std::clamp(val, 1, 500);
            tags.append(QStringLiteral("limit %1").arg(limit));
            text.replace(mPrefix.capturedStart(0), mPrefix.capturedLength(0), QStringLiteral(" "));
            return true;
        }
    }

    const auto mSuffix = s_suffixLimit.match(text);
    if (mSuffix.hasMatch()) {
        const int val = mSuffix.captured(1).toInt();
        if (val > 0) {
            limit = std::clamp(val, 1, 500);
            tags.append(QStringLiteral("limit %1").arg(limit));
            text.replace(mSuffix.capturedStart(0), mSuffix.capturedLength(0), QStringLiteral(" "));
            return true;
        }
    }
    return false;
}

bool matchDecade(QString &text, QList<library::SmartCondition> &conditions, QStringList &tags)
{
    static const QRegularExpression s_decade4(
        QStringLiteral(R"((?:\b|(?<=\D))(19\d0|20\d0)\s*(?:年代|s\b|'s\b))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression s_decade2(
        QStringLiteral(R"((?:\b|(?<=\D))(\d0)\s*(?:年代|s\b|'s\b))"),
        QRegularExpression::CaseInsensitiveOption);

    const auto m4 = s_decade4.match(text);
    if (m4.hasMatch()) {
        const int startYear = m4.captured(1).toInt();
        const int endYear = startYear + 9;
        conditions.append(library::SmartCondition {
            .field = library::SmartField::Year,
            .op = library::SmartOp::Between,
            .value = startYear,
            .value2 = endYear,
        });
        tags.append(QStringLiteral("%1s").arg(startYear));
        text.replace(m4.capturedStart(0), m4.capturedLength(0), QStringLiteral(" "));
        return true;
    }

    const auto m2 = s_decade2.match(text);
    if (m2.hasMatch()) {
        const int val = m2.captured(1).toInt();
        const int startYear = (val >= 30) ? (1900 + val) : (2000 + val);
        const int endYear = startYear + 9;
        conditions.append(library::SmartCondition {
            .field = library::SmartField::Year,
            .op = library::SmartOp::Between,
            .value = startYear,
            .value2 = endYear,
        });
        tags.append(QStringLiteral("%1s").arg(startYear));
        text.replace(m2.capturedStart(0), m2.capturedLength(0), QStringLiteral(" "));
        return true;
    }
    return false;
}

bool matchYear(QString &text, QList<library::SmartCondition> &conditions, QStringList &tags)
{
    static const QRegularExpression s_yearZh(
        QStringLiteral(R"((?:\b|(?<=\D))(19\d\d|20\d\d)\s*年)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression s_yearIn(
        QStringLiteral(R"(\bin\s+(19\d\d|20\d\d)\b)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression s_yearPrefix(
        QStringLiteral(R"(\byear\s+(19\d\d|20\d\d)\b)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression s_yearSuffix(
        QStringLiteral(R"((?:\b|(?<=\D))(19\d\d|20\d\d)\s*years?\b)"),
        QRegularExpression::CaseInsensitiveOption);

    const std::array<const QRegularExpression *, 4> regexes
        = { &s_yearZh, &s_yearIn, &s_yearPrefix, &s_yearSuffix };

    for (const auto *re : regexes) {
        const auto m = re->match(text);
        if (m.hasMatch()) {
            const int year = m.captured(1).toInt();
            conditions.append(library::SmartCondition {
                .field = library::SmartField::Year,
                .op = library::SmartOp::Equals,
                .value = year,
                .value2 = { },
            });
            tags.append(QString::number(year));
            text.replace(m.capturedStart(0), m.capturedLength(0), QStringLiteral(" "));
            return true;
        }
    }
    return false;
}

struct VersionTypeEntry {
    QString value { };
    QString tag { };
    QStringList patterns;
};

bool matchVersionType(QString &text, QList<library::SmartCondition> &conditions, QStringList &tags)
{
    static const std::array<VersionTypeEntry, 5> s_versionRules = { {
        {
            .value = QStringLiteral("live"),
            .tag = QStringLiteral("live"),
            .patterns = { QStringLiteral("现场录音"), QStringLiteral("现场版"),
                QStringLiteral("演唱会版"), QStringLiteral("演唱会"), QStringLiteral("现场"),
                QStringLiteral(R"(\blive\s+version\b)"), QStringLiteral(R"(\blive\b)") },
        },
        {
            .value = QStringLiteral("instrumental"),
            .tag = QStringLiteral("instrumental"),
            .patterns = { QStringLiteral("伴奏版"), QStringLiteral("纯音乐版"),
                QStringLiteral("消音版"), QStringLiteral("纯音乐"), QStringLiteral("伴唱"),
                QStringLiteral("伴奏"), QStringLiteral("器乐"),
                QStringLiteral(R"(\binstrumental\b)"), QStringLiteral(R"(\boff\s+vocal\b)"),
                QStringLiteral(R"(\bkaraoke\b)"), QStringLiteral(R"(\bbacking\s+track\b)") },
        },
        {
            .value = QStringLiteral("remix"),
            .tag = QStringLiteral("remix"),
            .patterns
            = { QStringLiteral("混音版"), QStringLiteral("电音版"), QStringLiteral("混音"),
                QStringLiteral(R"(\bremixed\b)"), QStringLiteral(R"(\bremix\b)") },
        },
        {
            .value = QStringLiteral("acoustic"),
            .tag = QStringLiteral("acoustic"),
            .patterns = { QStringLiteral("不插电版"), QStringLiteral("木吉他版"),
                QStringLiteral("钢琴版"), QStringLiteral("原声版"), QStringLiteral("不插电"),
                QStringLiteral(R"(\bacoustic\b)"), QStringLiteral(R"(\bunplugged\b)") },
        },
        {
            .value = QStringLiteral("remaster"),
            .tag = QStringLiteral("remaster"),
            .patterns
            = { QStringLiteral("母带重制"), QStringLiteral("重制版"), QStringLiteral("重混版"),
                QStringLiteral("重制"), QStringLiteral("重混"), QStringLiteral("重录"),
                QStringLiteral(R"(\bremastered\b)"), QStringLiteral(R"(\bremaster\b)") },
        },
    } };

    bool matched = false;
    for (const auto &rule : s_versionRules) {
        if (tryMatchAndStripAll(text, rule.patterns)) {
            conditions.append(library::SmartCondition {
                .field = library::SmartField::VersionType,
                .op = library::SmartOp::Is,
                .value = rule.value,
                .value2 = { },
            });
            tags.append(rule.tag);
            matched = true;
        }
    }
    return matched;
}

struct LanguageEntry {
    QString value { };
    QString tag { };
    QStringList patterns;
};

bool matchLanguage(QString &text, QList<library::SmartCondition> &conditions, QStringList &tags)
{
    static const std::array<LanguageEntry, 4> s_languageRules = { {
        {
            .value = QStringLiteral("ja"),
            .tag = QStringLiteral("Japanese"),
            .patterns = { QStringLiteral("日本语"), QStringLiteral("日语歌"),
                QStringLiteral("日文歌"), QStringLiteral("日本歌"), QStringLiteral("日语"),
                QStringLiteral("日文"), QStringLiteral(R"(\bjapanese\b)"),
                QStringLiteral(R"(\bj-pop\b)"), QStringLiteral(R"(\bjpop\b)") },
        },
        {
            .value = QStringLiteral("zh"),
            .tag = QStringLiteral("Chinese"),
            .patterns
            = { QStringLiteral("中文歌"), QStringLiteral("华语歌"), QStringLiteral("国语歌"),
                QStringLiteral("粤语歌"), QStringLiteral("中文"), QStringLiteral("华语"),
                QStringLiteral("国语"), QStringLiteral("粤语"), QStringLiteral(R"(\bchinese\b)"),
                QStringLiteral(R"(\bmandarin\b)"), QStringLiteral(R"(\bcantonese\b)"),
                QStringLiteral(R"(\bc-pop\b)"), QStringLiteral(R"(\bcpop\b)") },
        },
        {
            .value = QStringLiteral("ko"),
            .tag = QStringLiteral("Korean"),
            .patterns = { QStringLiteral("韩国语"), QStringLiteral("韩语歌"),
                QStringLiteral("韩文歌"), QStringLiteral("韩歌"), QStringLiteral("韩语"),
                QStringLiteral("韩文"), QStringLiteral(R"(\bkorean\b)"),
                QStringLiteral(R"(\bk-pop\b)"), QStringLiteral(R"(\bkpop\b)") },
        },
        {
            .value = QStringLiteral("western"),
            .tag = QStringLiteral("English"),
            .patterns
            = { QStringLiteral("英文歌"), QStringLiteral("欧美歌"), QStringLiteral("英语歌"),
                QStringLiteral("西洋歌"), QStringLiteral("西文"), QStringLiteral("英文"),
                QStringLiteral("欧美"), QStringLiteral("英语"), QStringLiteral("西洋"),
                QStringLiteral(R"(\benglish\b)"), QStringLiteral(R"(\bwestern\b)") },
        },
    } };

    bool matched = false;
    for (const auto &rule : s_languageRules) {
        if (tryMatchAndStripAll(text, rule.patterns)) {
            conditions.append(library::SmartCondition {
                .field = library::SmartField::Language,
                .op = library::SmartOp::Is,
                .value = rule.value,
                .value2 = { },
            });
            tags.append(rule.tag);
            matched = true;
        }
    }
    return matched;
}

bool matchPlayStatsRules(QString &text, SortKey &sortKey, Qt::SortOrder &sortOrder,
    bool &sortMatched, QList<library::SmartCondition> &conditions, QStringList &tags)
{
    // 1. Haven't played
    static const QStringList s_havenotPlayedPatterns = { QStringLiteral("很久没听过"),
        QStringLiteral("好久没听过"), QStringLiteral("很久没听"), QStringLiteral("好久没听"),
        QStringLiteral("很久没放过"), QStringLiteral("很久没播过"), QStringLiteral("很久未听"),
        QStringLiteral("长期没听"), QStringLiteral(R"(\bhaven't\s+played\s+in\s+a\s+while\b)"),
        QStringLiteral(R"(\bhaven't\s+played\s+recently\b)"),
        QStringLiteral(R"(\bhaven't\s+played\b)"), QStringLiteral(R"(\bhaven't\s+listened\b)"),
        QStringLiteral(R"(\bnot\s+played\s+recently\b)"),
        QStringLiteral(R"(\bleast\s+recently\s+played\b)"),
        QStringLiteral(R"(\bnot\s+listened\s+to\s+recently\b)") };
    if (tryMatchAndStripAll(text, s_havenotPlayedPatterns)) {
        sortKey = SortKey::LastPlayed;
        sortOrder = Qt::AscendingOrder;
        sortMatched = true;
        conditions.append(library::SmartCondition {
            .field = library::SmartField::LastPlayed,
            .op = library::SmartOp::NotInLastDays,
            .value = 180,
            .value2 = { },
        });
        tags.append(QStringLiteral("not played in 180 days"));
        return true;
    }

    // 2. Never played
    static const QStringList s_neverPlayedPatterns = { QStringLiteral("从没听过"),
        QStringLiteral("从未听过"), QStringLiteral("没听过的"), QStringLiteral("没听过"),
        QStringLiteral("从没播过"), QStringLiteral("从没放过"), QStringLiteral("零播放"),
        QStringLiteral(R"(\bnever\s+played\b)"), QStringLiteral(R"(\bnever\s+listened\b)"),
        QStringLiteral(R"(\bnever\s+heard\b)"), QStringLiteral(R"(\bunplayed\b)") };
    if (tryMatchAndStripAll(text, s_neverPlayedPatterns)) {
        conditions.append(library::SmartCondition {
            .field = library::SmartField::PlayCount,
            .op = library::SmartOp::Equals,
            .value = 0,
            .value2 = { },
        });
        tags.append(QStringLiteral("never played"));
        return true;
    }

    // 3. Recently added
    static const QStringList s_recentlyAddedPatterns = { QStringLiteral("最近添加"),
        QStringLiteral("最新添加"), QStringLiteral("新添加的"), QStringLiteral("新添加"),
        QStringLiteral("新加的"), QStringLiteral("刚加的"), QStringLiteral("最近加入"),
        QStringLiteral("新加"), QStringLiteral(R"(\brecently\s+added\b)"),
        QStringLiteral(R"(\brecent\s+added\b)"), QStringLiteral(R"(\bnewly\s+added\b)"),
        QStringLiteral(R"(\blatest\s+added\b)"), QStringLiteral(R"(\bjust\s+added\b)") };
    if (tryMatchAndStripAll(text, s_recentlyAddedPatterns)) {
        sortKey = SortKey::DateAdded;
        sortOrder = Qt::DescendingOrder;
        sortMatched = true;
        tags.append(QStringLiteral("recently added"));
        return true;
    }

    // 4. Random / Shuffle
    static const QStringList s_randomPatterns
        = { QStringLiteral("随机播放"), QStringLiteral("随机"), QStringLiteral("随便听听"),
              QStringLiteral("随便"), QStringLiteral("随心"), QStringLiteral("洗牌"),
              QStringLiteral(R"(\brandom\b)"), QStringLiteral(R"(\bshuffle\b)") };
    if (tryMatchAndStripAll(text, s_randomPatterns)) {
        sortKey = SortKey::Random;
        sortOrder = Qt::DescendingOrder;
        sortMatched = true;
        tags.append(QStringLiteral("random"));
        return true;
    }

    // 5. Most played
    static const QStringList s_mostPlayedPatterns
        = { QStringLiteral("最常听"), QStringLiteral("听得最多"), QStringLiteral("循环最多"),
              QStringLiteral("常听"), QStringLiteral("经常听"), QStringLiteral("常放"),
              QStringLiteral("多听"), QStringLiteral(R"(\bmost\s+played\b)"),
              QStringLiteral(R"(\btop\s+played\b)"), QStringLiteral(R"(\bmost\s+listened\b)"),
              QStringLiteral(R"(\bfrequently\s+played\b)"), QStringLiteral(R"(\btop\s+tracks\b)"),
              QStringLiteral(R"(\btop\s+songs\b)"), QStringLiteral(R"(\btop\b)") };
    if (tryMatchAndStripAll(text, s_mostPlayedPatterns)) {
        sortKey = SortKey::PlayCount;
        sortOrder = Qt::DescendingOrder;
        sortMatched = true;
        conditions.append(library::SmartCondition {
            .field = library::SmartField::PlayCount,
            .op = library::SmartOp::Greater,
            .value = 0,
            .value2 = { },
        });
        tags.append(QStringLiteral("most played"));
        return true;
    }

    return false;
}

bool matchPlayWindow(QString &text, QDate today, std::optional<QDate> &playedFrom,
    std::optional<QDate> &playedTo, QStringList &tags)
{
    // 1. This week
    static const QStringList s_thisWeekPatterns = { QStringLiteral("这星期"),
        QStringLiteral("本星期"), QStringLiteral("这周"), QStringLiteral("本周"),
        QStringLiteral("近7天"), QStringLiteral("近一周"), QStringLiteral(R"(\bthis\s+week\b)"),
        QStringLiteral(R"(\bpast\s+week\b)"), QStringLiteral(R"(\blast\s+7\s+days\b)") };
    if (tryMatchAndStripAll(text, s_thisWeekPatterns)) {
        playedFrom = today.addDays(-6);
        playedTo = today;
        tags.append(QStringLiteral("this week"));
        return true;
    }

    // 2. Last month
    static const QStringList s_lastMonthPatterns
        = { QStringLiteral("上个自然月"), QStringLiteral("上个月"), QStringLiteral("上月"),
              QStringLiteral(R"(\blast\s+month\b)"), QStringLiteral(R"(\bprevious\s+month\b)") };
    if (tryMatchAndStripAll(text, s_lastMonthPatterns)) {
        const QDate startMonth = QDate(today.year(), today.month(), 1).addMonths(-1);
        const QDate endMonth = startMonth.addDays(startMonth.daysInMonth() - 1);
        playedFrom = startMonth;
        playedTo = endMonth;
        tags.append(QStringLiteral("last month"));
        return true;
    }

    // 3. This year
    static const QStringList s_thisYearPatterns
        = { QStringLiteral("这一年"), QStringLiteral("今年"), QStringLiteral("本年"),
              QStringLiteral("这年"), QStringLiteral(R"(\bthis\s+year\b)") };
    if (tryMatchAndStripAll(text, s_thisYearPatterns)) {
        playedFrom = QDate(today.year(), 1, 1);
        playedTo = today;
        tags.append(QStringLiteral("this year"));
        return true;
    }

    // 4. Last 30 days / Recently
    static const QStringList s_recentPatterns = { QStringLiteral("近一个月内"),
        QStringLiteral("近一个月"), QStringLiteral("一个月内"), QStringLiteral("这几天"),
        QStringLiteral("近30天"), QStringLiteral("最近"), QStringLiteral(R"(\blast\s+30\s+days\b)"),
        QStringLiteral(R"(\bpast\s+30\s+days\b)"), QStringLiteral(R"(\bpast\s+month\b)"),
        QStringLiteral(R"(\brecently\b)"), QStringLiteral(R"(\brecent\b)"),
        QStringLiteral(R"(\bthese\s+days\b)"), QStringLiteral(R"(\blately\b)") };
    if (tryMatchAndStripAll(text, s_recentPatterns)) {
        playedFrom = today.addDays(-30);
        playedTo = today;
        tags.append(QStringLiteral("last 30 days"));
        return true;
    }

    return false;
}

bool matchFavorite(QString &text, bool &favoriteMatched, QStringList &tags)
{
    static const QStringList s_favPatterns
        = { QStringLiteral("已收藏"), QStringLiteral("收藏的"), QStringLiteral("收藏"),
              QStringLiteral("喜欢的"), QStringLiteral("喜欢"), QStringLiteral("最爱"),
              QStringLiteral("红心"), QStringLiteral("喜爱"), QStringLiteral(R"(\bfavourites?\b)"),
              QStringLiteral(R"(\bfavorites?\b)"), QStringLiteral(R"(\bliked\b)"),
              QStringLiteral(R"(\bfav\b)"), QStringLiteral(R"(\bstarred\b)") };
    if (tryMatchAndStripAll(text, s_favPatterns)) {
        favoriteMatched = true;
        tags.append(QStringLiteral("favorite"));
        return true;
    }
    return false;
}

bool matchEntity(QString &text, Entity &entity, QStringList &tags)
{
    // Album
    static const QStringList s_albumPatterns = { QStringLiteral("唱片"), QStringLiteral("大碟"),
        QStringLiteral("专辑"), QStringLiteral(R"(\balbums?\b)") };
    if (tryMatchAndStripAll(text, s_albumPatterns)) {
        entity = Entity::Album;
        tags.append(QStringLiteral("album"));
        return true;
    }

    // Artist
    static const QStringList s_artistPatterns = { QStringLiteral("音乐人"), QStringLiteral("歌手"),
        QStringLiteral("乐队"), QStringLiteral("艺人"), QStringLiteral(R"(\bartists?\b)"),
        QStringLiteral(R"(\bsingers?\b)"), QStringLiteral(R"(\bbands?\b)") };
    if (tryMatchAndStripAll(text, s_artistPatterns)) {
        entity = Entity::Artist;
        tags.append(QStringLiteral("artist"));
        return true;
    }

    // Track
    static const QStringList s_trackPatterns = { QStringLiteral("歌曲"), QStringLiteral("曲目"),
        QStringLiteral("单曲"), QStringLiteral("歌"), QStringLiteral("曲"),
        QStringLiteral(R"(\bsongs?\b)"), QStringLiteral(R"(\btracks?\b)") };
    if (tryMatchAndStripAll(text, s_trackPatterns)) {
        entity = Entity::Track;
        return true;
    }

    return false;
}

void stripStopwords(QString &text)
{
    static const QStringList s_zhStopwords = { QStringLiteral("想要听"), QStringLiteral("想要"),
        QStringLiteral("想听"), QStringLiteral("你想"), QStringLiteral("帮我"),
        QStringLiteral("给我"), QStringLiteral("来点"), QStringLiteral("来些"),
        QStringLiteral("来首"), QStringLiteral("来张"), QStringLiteral("播放"),
        QStringLiteral("放点"), QStringLiteral("放首"), QStringLiteral("查找"),
        QStringLiteral("搜索"), QStringLiteral("歌曲"), QStringLiteral("曲目"),
        QStringLiteral("首歌"), QStringLiteral("首曲"), QStringLiteral("一些"),
        QStringLiteral("那些"), QStringLiteral("这些"), QStringLiteral("有哪些"),
        QStringLiteral("有什么"), QStringLiteral("哪些"), QStringLiteral("什么"),
        QStringLiteral("我的"), QStringLiteral("咱们"), QStringLiteral("我们"),
        QStringLiteral("只要"), QStringLiteral("版本"), QStringLiteral("所有"),
        QStringLiteral("全部"), QStringLiteral("的"), QStringLiteral("地"), QStringLiteral("得"),
        QStringLiteral("我"), QStringLiteral("咱"), QStringLiteral("要"), QStringLiteral("听"),
        QStringLiteral("只"), QStringLiteral("来"), QStringLiteral("放"), QStringLiteral("请"),
        QStringLiteral("查"), QStringLiteral("找"), QStringLiteral("歌"), QStringLiteral("曲"),
        QStringLiteral("首"), QStringLiteral("了"), QStringLiteral("呢"), QStringLiteral("吧"),
        QStringLiteral("呀"), QStringLiteral("啊"), QStringLiteral("么"), QStringLiteral("吗"),
        QStringLiteral("点"), QStringLiteral("个"), QStringLiteral("份"), QStringLiteral("张"),
        QStringLiteral("版") };

    for (const auto &sw : s_zhStopwords) {
        qsizetype pos = 0;
        while ((pos = text.indexOf(sw, pos, Qt::CaseInsensitive)) >= 0) {
            text.replace(pos, sw.size(), QStringLiteral(" "));
            pos += 1;
        }
    }

    static const QRegularExpression s_enStopwords(
        QStringLiteral(
            R"(\b(?:the|a|an|my|me|i|we|our|songs?|tracks?|music|tunes|please|play|show|give|find|search|for|of|in|with|all|some|only|just|want|listen|to|version|ver|and)\b)"),
        QRegularExpression::CaseInsensitiveOption);

    text.replace(s_enStopwords, QStringLiteral(" "));
}

QString cleanLeftover(const QString &text)
{
    static const QRegularExpression s_punct(QStringLiteral(
        R"(^[\s,，.。!！?？、~～:：;；"“”'‘’()（）\[\]]+|[\s,，.。!！?？、~～:：;；"“”'‘’()（）\[\]]+$)"));
    static const QRegularExpression s_multiSpace(QStringLiteral(R"(\s+)"));

    QString cleaned = text;
    cleaned.replace(s_punct, QString());
    cleaned.replace(s_multiSpace, QStringLiteral(" "));
    return cleaned.trimmed();
}

struct ParseContext {
    QString remaining { };
    QStringList tags;
    QList<library::SmartCondition> conditions;
    std::optional<Entity> entity;
    std::optional<SortKey> sortKey;
    std::optional<Qt::SortOrder> sortOrder;
    std::optional<int> limit;
    std::optional<QDate> playedFrom;
    std::optional<QDate> playedTo;
    bool windowMatched = false;
    bool favoriteMatched = false;
    bool matchedRule = false;
};

void applyPlayRules(ParseContext &ctx)
{
    SortKey sortKey = SortKey::Default;
    Qt::SortOrder sortOrder = Qt::DescendingOrder;
    bool sortMatched = false;
    if (matchPlayStatsRules(
            ctx.remaining, sortKey, sortOrder, sortMatched, ctx.conditions, ctx.tags)) {
        ctx.matchedRule = true;
        if (sortMatched) {
            ctx.sortKey = sortKey;
            ctx.sortOrder = sortOrder;
        }
    }
}

void applyTimeWindowRules(ParseContext &ctx, QDate today)
{
    std::optional<QDate> playedFrom;
    std::optional<QDate> playedTo;
    if (matchPlayWindow(ctx.remaining, today, playedFrom, playedTo, ctx.tags)) {
        ctx.matchedRule = true;
        ctx.windowMatched = true;
        ctx.playedFrom = playedFrom;
        ctx.playedTo = playedTo;
    }
}

void applyLanguageRules(ParseContext &ctx)
{
    if (matchLanguage(ctx.remaining, ctx.conditions, ctx.tags)) {
        ctx.matchedRule = true;
    }
}

void applyVersionRules(ParseContext &ctx)
{
    if (matchVersionType(ctx.remaining, ctx.conditions, ctx.tags)) {
        ctx.matchedRule = true;
    }
}

void applyYearRules(ParseContext &ctx)
{
    if (matchDecade(ctx.remaining, ctx.conditions, ctx.tags)) {
        ctx.matchedRule = true;
    }
    if (matchYear(ctx.remaining, ctx.conditions, ctx.tags)) {
        ctx.matchedRule = true;
    }
}

void applyFavoriteRules(ParseContext &ctx)
{
    if (matchFavorite(ctx.remaining, ctx.favoriteMatched, ctx.tags)) {
        ctx.matchedRule = true;
    }
}

void applyLimitRule(ParseContext &ctx)
{
    int limit = 50;
    if (matchLimit(ctx.remaining, limit, ctx.tags)) {
        ctx.matchedRule = true;
        ctx.limit = limit;
    }
}

void applyEntityRules(ParseContext &ctx)
{
    Entity entity = Entity::Track;
    if (matchEntity(ctx.remaining, entity, ctx.tags)) {
        ctx.matchedRule = true;
        ctx.entity = entity;
    }
}

QString stripFillers(ParseContext &ctx)
{
    stripStopwords(ctx.remaining);
    return cleanLeftover(ctx.remaining);
}

void mergeBaseConditions(Query &query, const QList<library::SmartCondition> &newConditions)
{
    for (const auto &newCond : newConditions) {
        bool replaced = false;
        for (auto &existing : query.rule.conditions) {
            if (existing.field == newCond.field && existing.op == newCond.op) {
                existing = newCond;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            query.rule.conditions.append(newCond);
        }
    }
}

Query buildQuery(const ParseContext &ctx, const std::optional<Query> &base)
{
    if (base.has_value()) {
        Query query = *base;
        if (ctx.entity.has_value()) {
            query.entity = *ctx.entity;
        }
        if (ctx.sortKey.has_value() && ctx.sortOrder.has_value()) {
            query.sortKey = *ctx.sortKey;
            query.sortOrder = *ctx.sortOrder;
        }
        if (ctx.limit.has_value()) {
            query.limit = *ctx.limit;
        }
        if (ctx.windowMatched) {
            query.rule.playedFrom = ctx.playedFrom;
            query.rule.playedTo = ctx.playedTo;
        }
        mergeBaseConditions(query, ctx.conditions);
        return query;
    }

    Query query;
    query.entity = ctx.entity.value_or(Entity::Track);
    query.sortKey = ctx.sortKey.value_or(SortKey::Default);
    query.sortOrder = ctx.sortOrder.value_or(Qt::DescendingOrder);
    query.limit = ctx.limit.value_or(50);
    query.rule.playedFrom = ctx.windowMatched ? ctx.playedFrom : std::nullopt;
    query.rule.playedTo = ctx.windowMatched ? ctx.playedTo : std::nullopt;
    query.rule.conditions = ctx.conditions;
    return query;
}

void applyFavoriteCondition(Query &query, const ParseContext &ctx)
{
    if (ctx.favoriteMatched) {
        library::SmartField favField = library::SmartField::Favorite;
        if (query.entity == Entity::Album) {
            favField = library::SmartField::AlbumFavorite;
        } else if (query.entity == Entity::Artist) {
            favField = library::SmartField::ArtistFavorite;
        }
        const library::SmartCondition favCond {
            .field = favField,
            .op = library::SmartOp::IsTrue,
            .value = { },
            .value2 = { },
        };
        bool replaced = false;
        for (auto &existing : query.rule.conditions) {
            if (existing.field == library::SmartField::Favorite
                || existing.field == library::SmartField::AlbumFavorite
                || existing.field == library::SmartField::ArtistFavorite) {
                existing = favCond;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            query.rule.conditions.append(favCond);
        }
    } else if (ctx.entity.has_value()) {
        for (auto &existing : query.rule.conditions) {
            if (existing.field == library::SmartField::Favorite
                || existing.field == library::SmartField::AlbumFavorite
                || existing.field == library::SmartField::ArtistFavorite) {
                if (query.entity == Entity::Album) {
                    existing.field = library::SmartField::AlbumFavorite;
                } else if (query.entity == Entity::Artist) {
                    existing.field = library::SmartField::ArtistFavorite;
                } else {
                    existing.field = library::SmartField::Favorite;
                }
            }
        }
    }
}

} // namespace

OfflineParse parseOffline(const QString &text, QDate today, const std::optional<Query> &base)
{
    ParseContext ctx;
    ctx.remaining = text;

    applyPlayRules(ctx);
    applyTimeWindowRules(ctx, today);
    applyLanguageRules(ctx);
    applyVersionRules(ctx);
    applyYearRules(ctx);
    applyFavoriteRules(ctx);
    applyLimitRule(ctx);
    applyEntityRules(ctx);

    const QString leftover = stripFillers(ctx);
    Query query = buildQuery(ctx, base);
    applyFavoriteCondition(query, ctx);

    return OfflineParse {
        .query = query,
        .leftover = leftover,
        .matchedRule = ctx.matchedRule,
        .matchedTags = ctx.tags,
    };
}

} // namespace linernotes::nlq
