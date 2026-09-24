#include <aimusic/core/Version.h>

#include <QObject>
#include <QString>
#include <QTest>

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
