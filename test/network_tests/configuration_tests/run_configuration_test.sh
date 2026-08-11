#!/usr/bin/env bash
# configuration_test.cpp opens "configuration_test.json" relative to cwd.
set -euo pipefail

RUNFILES="${RUNFILES_DIR:-}"
if [[ -z "${RUNFILES}" && -n "${TEST_SRCDIR:-}" ]]; then
    RUNFILES="${TEST_SRCDIR}"
fi
if [[ -z "${RUNFILES}" ]]; then
    echo "run_configuration_test.sh: RUNFILES_DIR / TEST_SRCDIR unset" >&2
    exit 1
fi

# bzlmod workspace appears as _main under TEST_SRCDIR.
WS="${TEST_WORKSPACE:-_main}"
CFG_DIR="${RUNFILES}/${WS}/test/network_tests/configuration_tests"
BIN="${CFG_DIR}/configuration_test_bin"

if [[ ! -x "${BIN}" ]]; then
    # Fallback: find the binary anywhere under runfiles.
    BIN="$(find "${RUNFILES}" -name configuration_test_bin -type f -executable | head -n1)"
    CFG_DIR="$(dirname "$(find "${RUNFILES}" -name configuration_test.json -type f | head -n1)")"
fi

cd "${CFG_DIR}"
exec "${BIN}" "$@"
