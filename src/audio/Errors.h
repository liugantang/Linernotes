// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QLatin1StringView>

namespace linernotes::audio::errc {

inline constexpr QLatin1StringView kAudioOpenFailed { "audio.open_failed" };
inline constexpr QLatin1StringView kAudioNoAudioStream { "audio.no_audio_stream" };
inline constexpr QLatin1StringView kAudioDecodeFailed { "audio.decode_failed" };
inline constexpr QLatin1StringView kAudioFingerprintFailed { "audio.fingerprint_failed" };

} // namespace linernotes::audio::errc
