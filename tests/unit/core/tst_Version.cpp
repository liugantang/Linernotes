// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 AiMusic contributors

#include <QObject>
#include <QString>
#include <QTest>

#include <core/Version.h>

namespace {

class TstVersion : public QObject {
    Q_OBJECT

private slots:
    void versionStringMatchesComponents();
    void applicationNameIsAiMusic();
};

void TstVersion::versionStringMatchesComponents()
{
    const QString expected = QStringLiteral("%1.%2.%3")
                                 .arg(aimusic::core::kVersionMajor)
                                 .arg(aimusic::core::kVersionMinor)
                                 .arg(aimusic::core::kVersionPatch);
    QCOMPARE(aimusic::core::versionString(), expected);
}

void TstVersion::applicationNameIsAiMusic()
{
    QCOMPARE(aimusic::core::applicationName(), QStringLiteral("AiMusic"));
}

} // namespace

QTEST_GUILESS_MAIN(TstVersion)

#include "tst_Version.moc"
