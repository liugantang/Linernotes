#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

CHECK_MODE=false

for arg in "$@"; do
    case "$arg" in
        --check)
            CHECK_MODE=true
            ;;
        -h|--help)
            echo "Usage: $0 [--check]"
            echo "  --check   Check formatting without modifying files"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg" >&2
            echo "Usage: $0 [--check]" >&2
            exit 1
            ;;
    esac
done

SEARCH_DIRS=()
for d in app src tools tests; do
    if [[ -d "${REPO_ROOT}/${d}" ]]; then
        SEARCH_DIRS+=("${REPO_ROOT}/${d}")
    fi
done

if [[ ${#SEARCH_DIRS[@]} -eq 0 ]]; then
    exit 0
fi

mapfile -t FILES < <(find "${SEARCH_DIRS[@]}" -type f \( -name "*.h" -o -name "*.cpp" \) ! -path "*/build/*" | sort)

if [[ ${#FILES[@]} -eq 0 ]]; then
    exit 0
fi

if [[ "${CHECK_MODE}" == true ]]; then
    FAILED_FILES=()
    for f in "${FILES[@]}"; do
        if ! clang-format --dry-run -Werror "$f" >/dev/null 2>&1; then
            FAILED_FILES+=("$f")
        fi
    done

    if [[ ${#FAILED_FILES[@]} -gt 0 ]]; then
        echo "The following files are not formatted correctly:" >&2
        for f in "${FAILED_FILES[@]}"; do
            echo "  ${f#"${REPO_ROOT}/"}" >&2
        done
        exit 1
    fi
    echo "All C++ files conform to clang-format rules."
else
    clang-format -i "${FILES[@]}"
    echo "Formatted ${#FILES[@]} file(s)."
fi
