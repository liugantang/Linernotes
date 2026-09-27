// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QTest>

#include <ui/CoverAccent.h>

namespace {

using linernotes::ui::coverAccentColor;

class TstCoverAccent : public QObject {
    Q_OBJECT

private slots:
    void pureRedReturnsRedHue();
    void pureGrayReturnsInvalidColor();
    void mostlyGrayWithBlueReturnsBlue();
    void nullImageReturnsInvalid();
};

void TstCoverAccent::pureRedReturnsRedHue()
{
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(QColor(255, 0, 0));

    const QColor accent = coverAccentColor(img);
    QVERIFY(accent.isValid());

    float h = 0.0F;
    float s = 0.0F;
    float v = 0.0F;
    accent.getHsvF(&h, &s, &v);
    QVERIFY(h < 0.05F || h > 0.95F);
    QVERIFY(s > 0.8F);
    QVERIFY(v > 0.8F);
}

void TstCoverAccent::pureGrayReturnsInvalidColor()
{
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(QColor(128, 128, 128));

    const QColor accent = coverAccentColor(img);
    QVERIFY(!accent.isValid());
}

void TstCoverAccent::mostlyGrayWithBlueReturnsBlue()
{
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(QColor(128, 128, 128));

    QPainter p(&img);
    p.fillRect(0, 0, 20, 20, QColor(0, 0, 255));
    p.end();

    const QColor accent = coverAccentColor(img);
    QVERIFY(accent.isValid());

    float h = 0.0F;
    float s = 0.0F;
    float v = 0.0F;
    accent.getHsvF(&h, &s, &v);
    const float hueDeg = h * 360.0F;
    QVERIFY(hueDeg >= 220.0F && hueDeg <= 260.0F);
}

void TstCoverAccent::nullImageReturnsInvalid()
{
    const QImage img;
    const QColor accent = coverAccentColor(img);
    QVERIFY(!accent.isValid());
}

} // namespace

QTEST_MAIN(TstCoverAccent)
#include "tst_CoverAccent.moc"
