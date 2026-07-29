#!/usr/bin/env bash
# runs every test_* binary and prints a checklist instead of raw QtTest output (which is
# just PASS/FAIL lines mixed in with QWARN/QDEBUG noise). exits non-zero on any failure.
set -euo pipefail

BUILD_DIR="${1:-$(dirname "$0")/../build}"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "Build directory not found: $BUILD_DIR" >&2
    echo "Usage: $0 [path-to-build-dir]" >&2
    exit 1
fi

if [[ -t 1 ]]; then
    BOLD=$'\033[1m'; DIM=$'\033[2m'; RESET=$'\033[0m'
    GREEN=$'\033[32m'; RED=$'\033[31m'
else
    BOLD=""; DIM=""; RESET=""; GREEN=""; RED=""
fi

total_pass=0
total_fail=0
failed_suites=()

for exe in "$BUILD_DIR"/test_*; do
    [[ -x "$exe" && -f "$exe" ]] || continue
    name="$(basename "$exe")"

    echo "${BOLD}${name}${RESET}"

    set +e
    output="$(QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" "$exe" 2>/dev/null)"
    exe_exit=$?
    set -e
    suite_pass=0
    suite_fail=0

    while IFS= read -r line; do
        case "$line" in
            "PASS   : "*)
                test_name="${line#PASS   : }"
                [[ "$test_name" == *"::initTestCase()" || "$test_name" == *"::cleanupTestCase()" ]] && continue # QtTest boilerplate, not a real test
                echo "  ${GREEN}[✓]${RESET} ${test_name#*::}"
                ((suite_pass++)) || true
                ;;
            "FAIL!  : "*)
                test_name="${line#FAIL!  : }"
                echo "  ${RED}[✗]${RESET} ${test_name#*::}"
                ((suite_fail++)) || true
                ;;
        esac
    done <<< "$output"

    if ((exe_exit != 0)); then
        echo "  ${RED}[✗] process exited with code ${exe_exit} (crashed or was killed before finishing)${RESET}"
        ((suite_fail++)) || true
    fi

    if ((suite_fail > 0)); then
        echo "  ${DIM}${suite_pass} passed, ${RED}${suite_fail} failed${RESET}${DIM}${RESET}"
        failed_suites+=("$name")
    else
        echo "  ${DIM}${suite_pass} passed${RESET}"
    fi
    echo

    ((total_pass += suite_pass)) || true
    ((total_fail += suite_fail)) || true
done

echo "${BOLD}────────────────────────────────────────${RESET}"
if ((total_fail > 0)); then
    echo "${BOLD}${RED}${total_pass} passed, ${total_fail} failed${RESET}"
    echo "Failed suites: ${failed_suites[*]}"
    exit 1
else
    echo "${BOLD}${GREEN}${total_pass} passed, 0 failed${RESET}"
fi
