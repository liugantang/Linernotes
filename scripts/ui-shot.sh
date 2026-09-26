#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors
#
# 在虚拟 X 显示（Xvfb）里启动 linernotes，按步骤操作并截图，不影响当前桌面（Wayland/X11 均可）。
# 需要：xorg-server-xvfb、xdotool、imagemagick。
#
# 用法：scripts/ui-shot.sh [选项] <输出目录> <步骤>...
#   --home <dir>     LINERNOTES_HOME（数据库、配置、缓存所在目录），默认使用临时空目录
#   --size <WxH>     窗口逻辑尺寸，默认 1200x800（与 Theme 默认窗口一致）
#   --scale <n>      QT_SCALE_FACTOR，默认 1
#   --binary <path>  默认 build/debug/app/linernotes
# 步骤（按顺序执行，坐标为窗口内的物理像素）：
#   shot:<名字>      截图保存为 <输出目录>/<名字>.png
#   click:<x>,<y>    左键单击        dclick:<x>,<y>  双击
#   move:<x>,<y>     鼠标移动（悬停）
#   key:<按键>       xdotool 按键名，如 Return、Escape、ctrl+alt+p、Down
#   type:<文本>      输入文本
#   sleep:<秒>       等待
#   resize:<WxH>     改变窗口尺寸
# 例：scripts/ui-shot.sh --home /tmp/h out shot:tracks click:60,125 sleep:1 shot:albums
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOME_DIR=""
SIZE="1200x800"
SCALE="1"
BINARY="${REPO_ROOT}/build/debug/app/linernotes"

while [[ $# -gt 0 && "$1" == --* ]]; do
    case "$1" in
        --home) HOME_DIR="$2"; shift 2 ;;
        --size) SIZE="$2"; shift 2 ;;
        --scale) SCALE="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done
if [[ $# -lt 2 ]]; then
    sed -n '5,22p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
OUT_DIR="$1"; shift
mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"

CLEANUP_HOME=""
if [[ -z "${HOME_DIR}" ]]; then
    HOME_DIR="$(mktemp -d)"
    CLEANUP_HOME="${HOME_DIR}"
fi

W="${SIZE%x*}"; H="${SIZE#*x}"
SCREEN_W=$(awk "BEGIN{print int(${W}*${SCALE})}")
SCREEN_H=$(awk "BEGIN{print int(${H}*${SCALE})}")

run_steps() {
    "${BINARY}" > "${OUT_DIR}/app.log" 2>&1 &
    local pid=$!
    local win=""
    for _ in $(seq 100); do
        win="$(xdotool search --pid "${pid}" --onlyvisible 2>/dev/null | head -1 || true)"
        [[ -n "${win}" ]] && break
        sleep 0.1
    done
    if [[ -z "${win}" ]]; then
        echo "window did not appear; see ${OUT_DIR}/app.log" >&2
        kill "${pid}" 2>/dev/null || true
        return 1
    fi
    xdotool windowmove "${win}" 0 0 windowsize "${win}" "${SCREEN_W}" "${SCREEN_H}"
    sleep 1.5
    for step in "$@"; do
        local cmd="${step%%:*}" arg="${step#*:}"
        case "${cmd}" in
            shot) import -window root -crop "${SCREEN_W}x${SCREEN_H}+0+0" "${OUT_DIR}/${arg}.png"
                  echo "${OUT_DIR}/${arg}.png" ;;
            click) xdotool mousemove ${arg/,/ } click 1; sleep 0.5 ;;
            dclick) xdotool mousemove ${arg/,/ } click --repeat 2 --delay 80 1; sleep 0.5 ;;
            move) xdotool mousemove ${arg/,/ }; sleep 0.5 ;;
            key) xdotool key --window "${win}" "${arg}"; sleep 0.5 ;;
            type) xdotool type --window "${win}" "${arg}"; sleep 0.5 ;;
            sleep) sleep "${arg}" ;;
            resize) xdotool windowsize "${win}" $(awk "BEGIN{split(\"${arg}\",a,\"x\"); print int(a[1]*${SCALE}), int(a[2]*${SCALE})}"); sleep 1 ;;
            *) echo "Unknown step: ${step}" >&2 ;;
        esac
    done
    kill "${pid}" 2>/dev/null || true
    wait "${pid}" 2>/dev/null || true
}
export -f run_steps
export BINARY OUT_DIR SCREEN_W SCREEN_H

env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR="${SCALE}" LINERNOTES_HOME="${HOME_DIR}" \
    timeout 120 xvfb-run -a -s "-screen 0 ${SCREEN_W}x${SCREEN_H}x24" bash -c 'run_steps "$@"' _ "$@"

grep -E "\[W\]|\[C\]" "${OUT_DIR}/app.log" | grep -v "linernotes.player" || true
[[ -n "${CLEANUP_HOME}" ]] && rm -rf "${CLEANUP_HOME}"
exit 0
