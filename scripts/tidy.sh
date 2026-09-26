#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/debug"
STRICT_MODE=false
CHANGED_ONLY=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p)
            if [[ $# -lt 2 ]]; then
                echo "Error: -p requires a build directory argument" >&2
                exit 1
            fi
            BUILD_DIR="$2"
            shift 2
            ;;
        -p=*)
            BUILD_DIR="${1#-p=}"
            shift
            ;;
        --strict)
            STRICT_MODE=true
            shift
            ;;
        --changed)
            CHANGED_ONLY=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [-p <build_dir>] [--strict] [--changed]"
            echo "  -p <build_dir>   Path to build directory (default: build/debug)"
            echo "  --strict         Treat warnings as errors (exit non-zero on warnings)"
            echo "  --changed        Only check .cpp files changed relative to main (committed, staged,"
            echo "                   unstaged and untracked); falls back to all files if main is unknown"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Usage: $0 [-p <build_dir>] [--strict]" >&2
            exit 1
            ;;
    esac
done

if [[ "${BUILD_DIR}" != /* ]]; then
    if [[ -d "${PWD}/${BUILD_DIR}" ]]; then
        BUILD_DIR="${PWD}/${BUILD_DIR}"
    else
        BUILD_DIR="${REPO_ROOT}/${BUILD_DIR}"
    fi
fi

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
    echo "Error: '${BUILD_DIR}/compile_commands.json' not found." >&2
    echo "Please run 'cmake --preset debug && cmake --build --preset debug' first (moc files are generated at build time)." >&2
    exit 1
fi

FILES_REGEX='^(?!.*autogen|.*build).*(app|src|tools|tests)/.*\.cpp$'

if [[ "${CHANGED_ONLY}" == true ]] && git -C "${REPO_ROOT}" rev-parse --verify -q main >/dev/null; then
    BASE="$(git -C "${REPO_ROOT}" merge-base main HEAD)"
    mapfile -t CHANGED < <(
        {
            git -C "${REPO_ROOT}" diff --name-only --diff-filter=d "${BASE}"
            git -C "${REPO_ROOT}" ls-files --others --exclude-standard
        } | grep -E '^(app|src|tools|tests)/.*\.cpp$' | sort -u
    )
    if [[ ${#CHANGED[@]} -eq 0 ]]; then
        echo "No changed .cpp files; skipping clang-tidy."
        exit 0
    fi
    # run-clang-tidy 的文件参数是正则，按完整路径逐个匹配
    FILES_REGEX="^${REPO_ROOT}/($(printf '%s|' "${CHANGED[@]}" | sed 's/|$//; s/\./\\./g'))$"
fi

if command -v run-clang-tidy >/dev/null 2>&1; then
    RUN_ARGS=("-removed-arg=-mno-direct-extern-access")
    if [[ "${STRICT_MODE}" == true ]]; then
        RUN_ARGS+=("-warnings-as-errors=*")
    fi
    run-clang-tidy -p "${BUILD_DIR}" "${RUN_ARGS[@]}" "${FILES_REGEX}"
else
    TIDY_ARGS=("--removed-arg=-mno-direct-extern-access")
    if [[ "${STRICT_MODE}" == true ]]; then
        TIDY_ARGS+=("--warnings-as-errors=*")
    fi
    mapfile -t CPP_FILES < <(find "${REPO_ROOT}/app" "${REPO_ROOT}/src" "${REPO_ROOT}/tools" "${REPO_ROOT}/tests" -type f -name "*.cpp" ! -path "*/build/*" ! -path "*autogen*" 2>/dev/null | sort)
    FAILED=0
    for f in "${CPP_FILES[@]}"; do
        clang-tidy -p "${BUILD_DIR}" "${TIDY_ARGS[@]}" "$f" || FAILED=1
    done
    if [[ ${FAILED} -ne 0 ]]; then
        exit 1
    fi
fi
