#include <core/Version.h>

namespace aimusic::core {

QString versionString()
{
    return QStringLiteral("%1.%2.%3").arg(kVersionMajor).arg(kVersionMinor).arg(kVersionPatch);
}

QString applicationName()
{
    return QStringLiteral("AiMusic");
}

} // namespace aimusic::core
