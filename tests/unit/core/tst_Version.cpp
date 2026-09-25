// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QString>
#include <QTest>

#include <core/Version.h>

namespace {

class TstVersion : public QObject {
    Q_OBJECT

private slots:
    void versionStringMatchesComponents();
    void applicationNameIsLinernotes();
};

void TstVersion::versionStringMatchesComponents()
{
    const QString expected = QStringLiteral("%1.%2.%3")
                                 .arg(linernotes::core::kVersionMajor)
                                 .arg(linernotes::core::kVersionMinor)
                                 .arg(linernotes::core::kVersionPatch);
    QCOMPARE(linernotes::core::versionString(), expected);
}

void TstVersion::applicationNameIsLinernotes()
{
    QCOMPARE(linernotes::core::applicationName(), QStringLiteral("Linernotes"));
}

} // namespace

QTEST_GUILESS_MAIN(TstVersion)

#include "tst_Version.moc"
