#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

usage() {
    echo "Usage: $0 <music_dir> [hours, default 4] [output_csv, default soak-<timestamp>.csv]" >&2
    exit 2
}

if [[ $# -lt 1 ]] || [[ -z "${1:-}" ]]; then
    usage
fi

MUSIC_DIR="$1"
if [[ ! -d "$MUSIC_DIR" ]]; then
    echo "Error: Directory '$MUSIC_DIR' does not exist." >&2
    exit 1
fi

HOURS="${2:-4}"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
DEFAULT_CSV="soak-${TIMESTAMP}.csv"
CSV_FILE="${3:-$DEFAULT_CSV}"

INTERVAL="${SOAK_INTERVAL:-60}"

DURATION_SEC="$(awk -v h="$HOURS" 'BEGIN { s = int(h * 3600 + 0.5); if (s < 1) s = 1; print s }')"

PLAYCLI_BIN="${PLAYCLI_BIN:-}"
if [[ -z "$PLAYCLI_BIN" ]]; then
    if [[ -x "$REPO_ROOT/build/debug/tools/playcli/linernotes-playcli" ]]; then
        PLAYCLI_BIN="$REPO_ROOT/build/debug/tools/playcli/linernotes-playcli"
    elif [[ -x "$REPO_ROOT/build/release/tools/playcli/linernotes-playcli" ]]; then
        PLAYCLI_BIN="$REPO_ROOT/build/release/tools/playcli/linernotes-playcli"
    elif command -v linernotes-playcli >/dev/null 2>&1; then
        PLAYCLI_BIN="$(command -v linernotes-playcli)"
    else
        echo "Error: linernotes-playcli binary not found. Please build the project first." >&2
        exit 1
    fi
fi

mapfile -t AUDIO_FILES < <(find "$MUSIC_DIR" -type f \( \
    -iname '*.mp3' -o \
    -iname '*.flac' -o \
    -iname '*.ogg' -o \
    -iname '*.opus' -o \
    -iname '*.m4a' -o \
    -iname '*.wav' \
\) | sort)

if [[ ${#AUDIO_FILES[@]} -eq 0 ]]; then
    echo "Error: No supported audio files found in '$MUSIC_DIR'." >&2
    exit 1
fi

echo "Found ${#AUDIO_FILES[@]} audio file(s) in '$MUSIC_DIR'."
echo "Starting soak test for ${HOURS} hour(s) (${DURATION_SEC}s) with interval ${INTERVAL}s..."

echo "elapsed_s,rss_kb" > "$CSV_FILE"

set +e
timeout "$DURATION_SEC" "$PLAYCLI_BIN" \
    --ao null \
    --mode repeat-all \
    --report-memory "$INTERVAL" \
    "${AUDIO_FILES[@]}" 2>/dev/null | awk '
    /^MEM / {
        rss = ""
        elapsed = ""
        for (i = 1; i <= NF; i++) {
            if ($i ~ /^rss_kb=/) {
                split($i, a, "=")
                rss = a[2]
            }
            if ($i ~ /^elapsed_s=/) {
                split($i, b, "=")
                elapsed = b[2]
            }
        }
        if (rss != "" && elapsed != "") {
            print elapsed "," rss
            fflush()
        }
    }' >> "$CSV_FILE"
set -euo pipefail

LINE_COUNT="$(wc -l < "$CSV_FILE")"
if [[ "$LINE_COUNT" -le 1 ]]; then
    echo "Warning: No memory samples collected in '$CSV_FILE'." >&2
else
    FIRST_RSS="$(awk -F, 'NR==2 {print $2}' "$CSV_FILE")"
    LAST_RSS="$(awk -F, 'END {print $2}' "$CSV_FILE")"
    MAX_RSS="$(awk -F, 'NR==2 {max=$2+0} NR>2 {if ($2+0 > max) max=$2+0} END {print max}' "$CSV_FILE")"

    echo ""
    echo "================ Soak Test Summary ================"
    echo "Duration:        ${HOURS} hours (${DURATION_SEC}s)"
    echo "Samples:         $((LINE_COUNT - 1))"
    echo "Initial RSS:     ${FIRST_RSS} kB"
    echo "Final RSS:       ${LAST_RSS} kB"
    echo "Max RSS:         ${MAX_RSS} kB"
    echo "CSV output:      $CSV_FILE"
    echo "==================================================="
fi
