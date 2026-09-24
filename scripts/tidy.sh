#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/debug"

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
        -h|--help)
            echo "Usage: $0 [-p <build_dir>]"
            echo "  -p <build_dir>   Path to build directory (default: build/debug)"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Usage: $0 [-p <build_dir>]" >&2
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
    echo "Please run 'cmake --preset debug' first." >&2
    exit 1
fi

FILES_REGEX='^(?!.*autogen|.*build).*(app|src|tools|tests)/.*\.cpp$'
EXTRA_ARGS=("-removed-arg=-mno-direct-extern-access")

if command -v run-clang-tidy >/dev/null 2>&1; then
    run-clang-tidy -p "${BUILD_DIR}" "${EXTRA_ARGS[@]}" "${FILES_REGEX}"
else
    mapfile -t CPP_FILES < <(find "${REPO_ROOT}/app" "${REPO_ROOT}/src" "${REPO_ROOT}/tools" "${REPO_ROOT}/tests" -type f -name "*.cpp" ! -path "*/build/*" ! -path "*autogen*" 2>/dev/null | sort)
    for f in "${CPP_FILES[@]}"; do
        clang-tidy -p "${BUILD_DIR}" "${EXTRA_ARGS[@]}" "$f"
    done
fi
