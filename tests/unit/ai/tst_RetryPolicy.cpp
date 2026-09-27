// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QString>
#include <QTest>

#include <ai/Errors.h>
#include <ai/RetryPolicy.h>
#include <core/Result.h>

#include <optional>

using linernotes::ai::retryDelayMs;
using linernotes::ai::RetryPolicy;
using linernotes::core::Error;
namespace errc = linernotes::ai::errc;

namespace {

class TstRetryPolicy : public QObject {
    Q_OBJECT

private slots:
    void errorCodesRetryable_data();
    void errorCodesRetryable();
    void exponentialBackoff();
    void retryAfterTakesMax();
    void clampedToMaxDelay();
    void maxRetriesExceeded();
};

void TstRetryPolicy::errorCodesRetryable_data()
{
    QTest::addColumn<QString>("errorCode");
    QTest::addColumn<int>("httpStatus");
    QTest::addColumn<bool>("shouldRetry");

    // Retryable errors
    QTest::newRow("network") << QString(errc::kNetwork) << 0 << true;
    QTest::newRow("timeout") << QString(errc::kTimeout) << 0 << true;
    QTest::newRow("rate_limited_429") << QString(errc::kRateLimited) << 429 << true;
    QTest::newRow("rate_limited_no_status") << QString(errc::kRateLimited) << 0 << true;
    QTest::newRow("http_500") << QString(errc::kHttp) << 500 << true;
    QTest::newRow("http_502") << QString(errc::kHttp) << 502 << true;
    QTest::newRow("http_503") << QString(errc::kHttp) << 503 << true;
    QTest::newRow("http_504") << QString(errc::kHttp) << 504 << true;

    // Non-retryable errors
    QTest::newRow("auth_401") << QString(errc::kAuth) << 401 << false;
    QTest::newRow("auth_403") << QString(errc::kAuth) << 403 << false;
    QTest::newRow("http_400") << QString(errc::kHttp) << 400 << false;
    QTest::newRow("http_404") << QString(errc::kHttp) << 404 << false;
    QTest::newRow("http_422") << QString(errc::kHttp) << 422 << false;
    QTest::newRow("bad_response") << QString(errc::kBadResponse) << 200 << false;
    QTest::newRow("aborted") << QString(errc::kAborted) << 0 << false;
    QTest::newRow("schema_invalid") << QString(errc::kSchemaInvalid) << 0 << false;
    QTest::newRow("schema_mismatch") << QString(errc::kSchemaMismatch) << 0 << false;
    QTest::newRow("bad_json") << QString(errc::kBadJson) << 0 << false;
    QTest::newRow("secret_store") << QString(errc::kSecretStore) << 0 << false;
    QTest::newRow("not_configured") << QString(errc::kNotConfigured) << 0 << false;
}

void TstRetryPolicy::errorCodesRetryable()
{
    QFETCH(QString, errorCode);
    QFETCH(int, httpStatus);
    QFETCH(bool, shouldRetry);

    RetryPolicy policy;
    policy.maxRetries = 3;
    policy.baseDelayMs = 1000;
    policy.maxDelayMs = 60000;

    Error err;
    err.code = errorCode;

    const auto delay = retryDelayMs(policy, err, httpStatus, std::nullopt, 0);
    if (shouldRetry) {
        QCOMPARE(delay, std::optional<qint64>(1000));
    } else {
        QCOMPARE(delay, std::nullopt);
    }
}

void TstRetryPolicy::exponentialBackoff()
{
    RetryPolicy policy;
    policy.maxRetries = 5;
    policy.baseDelayMs = 1000;
    policy.maxDelayMs = 60000;

    Error err;
    err.code = QString(errc::kNetwork);

    // retriesDone: 0 -> base * 2^0 = 1000
    const auto d0 = retryDelayMs(policy, err, 0, std::nullopt, 0);
    QCOMPARE(d0, std::optional<qint64>(1000));

    // retriesDone: 1 -> base * 2^1 = 2000
    const auto d1 = retryDelayMs(policy, err, 0, std::nullopt, 1);
    QCOMPARE(d1, std::optional<qint64>(2000));

    // retriesDone: 2 -> base * 2^2 = 4000
    const auto d2 = retryDelayMs(policy, err, 0, std::nullopt, 2);
    QCOMPARE(d2, std::optional<qint64>(4000));

    // retriesDone: 3 -> base * 2^3 = 8000
    const auto d3 = retryDelayMs(policy, err, 0, std::nullopt, 3);
    QCOMPARE(d3, std::optional<qint64>(8000));

    // retriesDone: 4 -> base * 2^4 = 16000
    const auto d4 = retryDelayMs(policy, err, 0, std::nullopt, 4);
    QCOMPARE(d4, std::optional<qint64>(16000));
}

void TstRetryPolicy::retryAfterTakesMax()
{
    RetryPolicy policy;
    policy.maxRetries = 3;
    policy.baseDelayMs = 1000;
    policy.maxDelayMs = 60000;

    Error err;
    err.code = QString(errc::kRateLimited);

    // retryAfter (5000) > backoff (1000) -> 5000
    const auto d1 = retryDelayMs(policy, err, 429, 5000, 0);
    QCOMPARE(d1, std::optional<qint64>(5000));

    // retryAfter (500) < backoff (1000) -> 1000
    const auto d2 = retryDelayMs(policy, err, 429, 500, 0);
    QCOMPARE(d2, std::optional<qint64>(1000));

    // retryAfter (2000) < backoff at retriesDone=2 (4000) -> 4000
    const auto d3 = retryDelayMs(policy, err, 429, 2000, 2);
    QCOMPARE(d3, std::optional<qint64>(4000));
}

void TstRetryPolicy::clampedToMaxDelay()
{
    RetryPolicy policy;
    policy.maxRetries = 5;
    policy.baseDelayMs = 1000;
    policy.maxDelayMs = 3000;

    Error err;
    err.code = QString(errc::kTimeout);

    // backoff at retriesDone=2 would be 4000, clamped to maxDelayMs 3000
    const auto d1 = retryDelayMs(policy, err, 0, std::nullopt, 2);
    QCOMPARE(d1, std::optional<qint64>(3000));

    // retryAfter 100000 clamped to 3000
    const auto d2 = retryDelayMs(policy, err, 0, 100000, 0);
    QCOMPARE(d2, std::optional<qint64>(3000));
}

void TstRetryPolicy::maxRetriesExceeded()
{
    RetryPolicy policy;
    policy.maxRetries = 3;
    policy.baseDelayMs = 1000;
    policy.maxDelayMs = 60000;

    Error err;
    err.code = QString(errc::kNetwork);

    // retriesDone == maxRetries (3 >= 3) -> nullopt
    const auto d3 = retryDelayMs(policy, err, 0, std::nullopt, 3);
    QCOMPARE(d3, std::nullopt);

    // retriesDone > maxRetries (4 >= 3) -> nullopt
    const auto d4 = retryDelayMs(policy, err, 0, std::nullopt, 4);
    QCOMPARE(d4, std::nullopt);

    // maxRetries = 0 -> retriesDone 0 returns nullopt
    RetryPolicy zeroPolicy;
    zeroPolicy.maxRetries = 0;
    const auto dZero = retryDelayMs(zeroPolicy, err, 0, std::nullopt, 0);
    QCOMPARE(dZero, std::nullopt);
}

} // namespace

QTEST_GUILESS_MAIN(TstRetryPolicy)

#include "tst_RetryPolicy.moc"
