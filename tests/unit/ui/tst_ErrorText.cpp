// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>

#include <core/Result.h>
#include <library/Errors.h>
#include <ui/ErrorText.h>

namespace {

using linernotes::core::Error;
using linernotes::ui::userErrorText;

class TstErrorText : public QObject {
    Q_OBJECT

private slots:
    void knownErrorCodeHidesMessage();
    void unknownErrorCodeShowsCodeAndHidesMessage();
};

void TstErrorText::knownErrorCodeHidesMessage()
{
    const Error error {
        .code = QString(linernotes::library::errc::kCorrectionNotFound),
        .message = QStringLiteral("Canonical artist not found"),
        .detail = QStringLiteral("sensitive detail"),
    };
    const QString text = userErrorText(error);
    QVERIFY(!text.isEmpty());
    QVERIFY(!text.contains(QStringLiteral("Canonical artist not found")));
    QVERIFY(!text.contains(QStringLiteral("sensitive detail")));
}

void TstErrorText::unknownErrorCodeShowsCodeAndHidesMessage()
{
    const Error error {
        .code = QStringLiteral("foo.bar"),
        .message = QStringLiteral("some internal error message"),
        .detail = QStringLiteral("sensitive db path"),
    };
    const QString text = userErrorText(error);
    QVERIFY(text.contains(QStringLiteral("foo.bar")));
    QVERIFY(!text.contains(QStringLiteral("some internal error message")));
    QVERIFY(!text.contains(QStringLiteral("sensitive db path")));
}

} // namespace

QTEST_GUILESS_MAIN(TstErrorText)
#include "tst_ErrorText.moc"
