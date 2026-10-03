// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>

#include <ui/DeepDiveController.h>

namespace {

class TstDeepDiveController : public QObject {
    Q_OBJECT

private slots:
    void testContainsUnverifiedMarker();
    void testFormatGuideSubject();
};

void TstDeepDiveController::testContainsUnverifiedMarker()
{
    QVERIFY(!linernotes::ui::containsUnverifiedMarker(QString()));
    QVERIFY(!linernotes::ui::containsUnverifiedMarker(
        QStringLiteral("This is a verified sentence from Wikipedia.")));
    QVERIFY(linernotes::ui::containsUnverifiedMarker(
        QStringLiteral("This fact is from the model's knowledge †.")));
    QVERIFY(linernotes::ui::containsUnverifiedMarker(QStringLiteral("†")));
}

void TstDeepDiveController::testFormatGuideSubject()
{
    QCOMPARE(linernotes::ui::formatGuideSubject(
                 QStringLiteral("Song Title"), QStringLiteral("Artist Name")),
        QStringLiteral("Song Title — Artist Name"));
    QCOMPARE(linernotes::ui::formatGuideSubject(QStringLiteral("Song Title"), QString()),
        QStringLiteral("Song Title"));
    QCOMPARE(linernotes::ui::formatGuideSubject(QString(), QStringLiteral("Artist Name")),
        QStringLiteral("Artist Name"));
    QCOMPARE(linernotes::ui::formatGuideSubject(QString(), QString()), QString());
}

} // namespace

QTEST_MAIN(TstDeepDiveController)
#include "tst_DeepDiveController.moc"
