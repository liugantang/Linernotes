#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors
#
# 一次性验证：构建、测试、格式、clang-tidy 全部并行，只输出摘要。
#   --quick  执行者用：debug 构建 + ctest + 格式 + tidy（仅改动文件）
#   默认     审查者用：再加 ci 预设（-Werror）构建 + ctest，tidy 全量（与 CI 的 Lint 一致）
#   --asan   额外构建并运行 asan 预设
# 每一步的完整输出在 build/verify/<步骤>.log，失败时打印其尾部。
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

QUICK=false
ASAN=false
for arg in "$@"; do
    case "${arg}" in
        --quick) QUICK=true ;;
        --asan) ASAN=true ;;
        -h|--help)
            sed -n '4,9p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Unknown argument: ${arg}" >&2
            exit 2
            ;;
    esac
done

LOG_DIR="${REPO_ROOT}/build/verify"
mkdir -p "${LOG_DIR}"
rm -f "${LOG_DIR}"/*.log

JOBS_BUILD="$(nproc)"
JOBS_TEST=32

build_and_test() {
    local preset="$1"
    cmake --preset "${preset}" >/dev/null &&
        cmake --build --preset "${preset}" -j "${JOBS_BUILD}" &&
        ctest --test-dir "build/${preset}" -j "${JOBS_TEST}" --output-on-failure
}

tidy_step() {
    # tidy 依赖 debug 的 compile_commands.json 与 moc 生成文件，等 debug 构建完成
    while [[ ! -f "${LOG_DIR}/.debug-built" ]]; do
        [[ -f "${LOG_DIR}/.debug-failed" ]] && { echo "debug build failed; tidy skipped"; return 1; }
        sleep 1
    done
    if [[ "${QUICK}" == true ]]; then
        scripts/tidy.sh --strict --changed
    else
        scripts/tidy.sh --strict
    fi
}

debug_step() {
    if cmake --preset debug >/dev/null && cmake --build --preset debug -j "${JOBS_BUILD}"; then
        touch "${LOG_DIR}/.debug-built"
    else
        touch "${LOG_DIR}/.debug-failed"
        return 1
    fi
    ctest --test-dir build/debug -j "${JOBS_TEST}" --output-on-failure
}

rm -f "${LOG_DIR}/.debug-built" "${LOG_DIR}/.debug-failed"

declare -A PIDS
START=$(date +%s)

run() {
    local name="$1"
    shift
    "$@" >"${LOG_DIR}/${name}.log" 2>&1 &
    PIDS["${name}"]=$!
}

run debug debug_step
run format scripts/format.sh --check
run tidy tidy_step
if [[ "${QUICK}" == false ]]; then
    run ci build_and_test ci
fi
if [[ "${ASAN}" == true ]]; then
    run asan build_and_test asan
fi

FAILED=0
for name in debug format tidy ci asan; do
    [[ -n "${PIDS[${name}]:-}" ]] || continue
    if wait "${PIDS[${name}]}"; then
        summary="$(grep -E 'tests passed|conform|No changed' "${LOG_DIR}/${name}.log" | tail -1)"
        printf 'PASS  %-7s %s\n' "${name}" "${summary}"
    else
        FAILED=1
        printf 'FAIL  %-7s (log: build/verify/%s.log)\n' "${name}" "${name}"
        grep -E 'error|FAILED|Failed|FAIL!|warning:' "${LOG_DIR}/${name}.log" | head -20 | sed 's/^/      /'
        echo "      ... last lines:"
        tail -15 "${LOG_DIR}/${name}.log" | sed 's/^/      /'
    fi
done

echo "verify: $([[ ${FAILED} -eq 0 ]] && echo OK || echo FAILED) in $(( $(date +%s) - START ))s"
exit "${FAILED}"
