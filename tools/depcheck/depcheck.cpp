// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QString>

#include <chromaprint.h>
#include <ebur128.h>
#include <keychain.h>
#include <mpv/client.h>
#include <taglib/taglib.h>
#include <taglib/tstring.h>
#include <taglib/tversionnumber.h>
#include <uchardet.h>
#include <unicode/putil.h>
#include <unicode/uversion.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

#include <array>
#include <iostream>
#include <string_view>

int main()
{
    // 1. mpv
    const unsigned long mpvVer = mpv_client_api_version();
    std::cout << "mpv: " << (mpvVer >> 16) << "." << (mpvVer & 0xFFFF) << "\n";

    // 2. TagLib
#if defined(TAGLIB_MAJOR_VERSION) && TAGLIB_MAJOR_VERSION >= 2
    std::cout << "TagLib: " << TagLib::runtimeVersion().toString().toCString() << "\n";
#else
    std::cout << "TagLib: " << TAGLIB_MAJOR_VERSION << "." << TAGLIB_MINOR_VERSION << "."
              << TAGLIB_PATCH_VERSION << "\n";
#endif

    // 3. uchardet
    uchardet_t ud = uchardet_new();
    constexpr std::string_view kGbkData = "\xb2\xe2\xca\xd4\xd5\xe2\xca\xc7\xd2\xbb\xb6\xceGBK"
                                          "\xb1\xe0\xc2\xeb\xb5\xc4\xce\xc4\xb1\xbe";
    uchardet_handle_data(ud, kGbkData.data(), kGbkData.size());
    uchardet_data_end(ud);
    const char *charset = uchardet_get_charset(ud);
    std::cout << "uchardet: " << (charset != nullptr ? charset : "unknown") << "\n";
    uchardet_delete(ud);

    // 4. ICU
    UVersionInfo icuVersion;
    u_getVersion(icuVersion);
    std::array<char, U_MAX_VERSION_STRING_LENGTH> icuStr { };
    u_versionToString(icuVersion, icuStr.data());
    std::cout << "ICU: " << icuStr.data() << "\n";

    // 5. Chromaprint
    std::cout << "Chromaprint: " << chromaprint_get_version() << "\n";

    // 6. FFmpeg
    const unsigned int avformatVer = avformat_version();
    const unsigned int avcodecVer = avcodec_version();
    std::cout << "FFmpeg avformat: " << AV_VERSION_MAJOR(avformatVer) << "."
              << AV_VERSION_MINOR(avformatVer) << "." << AV_VERSION_MICRO(avformatVer)
              << ", avcodec: " << AV_VERSION_MAJOR(avcodecVer) << "."
              << AV_VERSION_MINOR(avcodecVer) << "." << AV_VERSION_MICRO(avcodecVer) << "\n";

    // 7. libebur128
    int ebMajor = 0;
    int ebMinor = 0;
    int ebPatch = 0;
    ebur128_get_version(&ebMajor, &ebMinor, &ebPatch);
    std::cout << "libebur128: " << ebMajor << "." << ebMinor << "." << ebPatch << "\n";

    // 8. QtKeychain
    const QKeychain::ReadPasswordJob job(QStringLiteral("Linernotes"));
    std::cout << "QtKeychain: ok\n";

    return 0;
}
