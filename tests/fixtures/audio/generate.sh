#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# tone_440_1s.flac: 440 Hz 正弦，1 秒，单声道，22050 Hz
ffmpeg -y -f lavfi -i "sine=frequency=440:sample_rate=22050:duration=1.0" -c:a flac "${SCRIPT_DIR}/tone_440_1s.flac"

# tone_880_1s.ogg: 880 Hz 正弦，1 秒，单声道，Vorbis 编码
ffmpeg -y -f lavfi -i "sine=frequency=880:sample_rate=22050:duration=1.0" -c:a libvorbis "${SCRIPT_DIR}/tone_880_1s.ogg"

# tone_660_1s.flac: 660 Hz 正弦，1 秒，单声道，22050 Hz
ffmpeg -y -f lavfi -i "sine=frequency=660:sample_rate=22050:duration=1.0" -c:a flac "${SCRIPT_DIR}/tone_660_1s.flac"

# silence_5s.flac: 静音 5 秒，单声道，22050 Hz
ffmpeg -y -f lavfi -i "anullsrc=channel_layout=mono:sample_rate=22050" -t 5.0 -c:a flac "${SCRIPT_DIR}/silence_5s.flac"

# corrupt.flac: 不是合法音频的内容（固定文本重复若干次）
printf "not an audio file\n%.0s" {1..20} > "${SCRIPT_DIR}/corrupt.flac"
