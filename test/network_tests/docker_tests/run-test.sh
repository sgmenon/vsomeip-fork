#!/usr/bin/env bash
# Host Bazel build + Docker Compose runner for vsomeip network tests.
#
# Pattern (Cruise-style):
#   1. bazel build a suite pkg_tar (or legacy binary targets)
#   2. stage into /tmp/vsomeip-docker/<suite>
#   3. docker compose up thin Ubuntu images with fixed IPs + multicast
#   4. docker wait on the verdict container
#
# Usage:
#   ./run-test.sh e2e_crc
#   ./run-test.sh e2e_crc --package /path/to/e2e_crc_pkg.tar
#   ./run-test.sh event_test --timeout 180

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
STAGING_ROOT="/tmp/vsomeip-docker"

if [ "$(id -u)" -ne 0 ] && command -v sudo &>/dev/null; then
    SUDO=sudo
else
    SUDO=
fi

OVERRIDE_TIMEOUT=""
PACKAGE_TAR=""
TEST_NAME=""

usage() {
    cat <<EOF
Usage: $0 <test-name> [--package TAR] [--timeout SECONDS]

  --package TAR   Use a pre-built suite tar (skips bazel). Preferred for CI.

Available tests:
$(for d in "${SCRIPT_DIR}"/*/; do
    [[ -f "${d}/test.env" ]] && echo "  $(basename "${d}")"
done)
EOF
}

label_to_tar_path() {
    local label="$1"
    label="${label#//}"
    local pkg="${label%%:*}"
    local name="${label##*:}"
    echo "${REPO_ROOT}/bazel-bin/${pkg}/${name}.tar"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --package) PACKAGE_TAR="$2"; shift 2 ;;
        --timeout) OVERRIDE_TIMEOUT="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        -*)
            echo "Unknown flag: $1" >&2
            usage
            exit 1
            ;;
        *)
            if [[ -n "${TEST_NAME}" ]]; then
                echo "Unexpected argument: $1" >&2
                usage
                exit 1
            fi
            TEST_NAME="$1"
            shift
            ;;
    esac
done

if [[ -z "${TEST_NAME}" ]]; then
    usage
    exit 1
fi

TEST_DIR="${SCRIPT_DIR}/${TEST_NAME}"
TEST_ENV_FILE="${TEST_DIR}/test.env"
if [[ ! -f "${TEST_ENV_FILE}" ]]; then
    echo "FAIL: no test.env at ${TEST_ENV_FILE}" >&2
    usage
    exit 1
fi

# Export everything from test.env so docker compose can substitute
# CLIENT_BIN / SERVICE_BIN / container names / etc.
# shellcheck disable=SC1090
set -a
source "${TEST_ENV_FILE}"
set +a

PKG_DIR="${STAGING_ROOT}/${TEST_NAME}"
export PKG_DIR
TIMEOUT="${OVERRIDE_TIMEOUT:-${TIMEOUT:-90}}"
WAIT_CONTAINER="${WAIT_CONTAINER:?WAIT_CONTAINER must be set in test.env}"
BRIDGE_NAME="${BRIDGE_NAME:?BRIDGE_NAME must be set in test.env}"

echo "=== vsomeip Docker network test: ${TEST_NAME} ==="
echo "  Timeout:    ${TIMEOUT}s"
echo "  Bridge:     ${BRIDGE_NAME}"
echo "  Pkg dir:    ${PKG_DIR}"
echo "  Wait on:    ${WAIT_CONTAINER}"

# Prefer pkg_tar packaging (E2E suites). Fall back to legacy BAZEL_TARGETS
# + setup.sh staging (event_test).
if [[ -n "${PACKAGE_TAR}" ]]; then
    if [[ ! -f "${PACKAGE_TAR}" ]]; then
        echo "FAIL: --package not found: ${PACKAGE_TAR}" >&2
        exit 1
    fi
elif [[ -n "${PACKAGE_TARGET:-}" ]]; then
    echo ""
    echo "--- bazel build ${PACKAGE_TARGET} ---"
    cd "${REPO_ROOT}"
    if ! bazel build "${PACKAGE_TARGET}"; then
        echo "FAIL: bazel build failed" >&2
        exit 1
    fi
    PACKAGE_TAR="$(label_to_tar_path "${PACKAGE_TARGET}")"
    if [[ ! -f "${PACKAGE_TAR}" ]]; then
        echo "FAIL: package tar missing at ${PACKAGE_TAR}" >&2
        exit 1
    fi
elif [[ -n "${BAZEL_TARGETS:-}" ]]; then
    echo ""
    echo "--- bazel build ---"
    cd "${REPO_ROOT}"
    # shellcheck disable=SC2086
    if ! bazel build ${BAZEL_TARGETS}; then
        echo "FAIL: bazel build failed" >&2
        exit 1
    fi
else
    echo "FAIL: test.env must set PACKAGE_TARGET (pkg_tar) or BAZEL_TARGETS (legacy)" >&2
    exit 1
fi

echo ""
echo "--- staging package ---"
${SUDO} rm -rf "${PKG_DIR:?}" 2>/dev/null || true
mkdir -p "${PKG_DIR}"
export PKG_DIR
export PACKAGE_TAR
if ! bash "${TEST_DIR}/setup.sh"; then
    echo "FAIL: setup.sh failed" >&2
    exit 1
fi

echo ""
echo "--- docker compose ---"
export PKG_DIR
COMPOSE_PROJECT_NAME="vsomeip-$(echo "${TEST_NAME}" | tr -c 'a-zA-Z0-9' '-')"
export COMPOSE_PROJECT_NAME
COMPOSE_ARGS=(
    docker compose
    -f "${TEST_DIR}/docker-compose.yml"
    --project-directory "${TEST_DIR}"
)

"${COMPOSE_ARGS[@]}" down --volumes --remove-orphans 2>/dev/null || true

if ! "${COMPOSE_ARGS[@]}" build --quiet; then
    echo "FAIL: docker compose build failed" >&2
    exit 1
fi

if ! "${COMPOSE_ARGS[@]}" create; then
    echo "FAIL: docker compose create failed" >&2
    "${COMPOSE_ARGS[@]}" down --volumes --remove-orphans 2>/dev/null || true
    exit 1
fi

echo "Configuring ${BRIDGE_NAME} for multicast..."
SNOOPING="/sys/devices/virtual/net/${BRIDGE_NAME}/bridge/multicast_snooping"
if [[ -f "${SNOOPING}" ]]; then
    ${SUDO} sh -c "echo 0 > ${SNOOPING}" 2>/dev/null \
        || echo 0 > "${SNOOPING}" 2>/dev/null \
        || echo "WARNING: could not disable IGMP snooping"
    for port in /sys/class/net/"${BRIDGE_NAME}"/brif/*; do
        [[ -d "${port}" ]] || continue
        ${SUDO} sh -c "echo 1 > ${port}/hairpin_mode" 2>/dev/null \
            || echo 1 > "${port}/hairpin_mode" 2>/dev/null \
            || true
    done
else
    echo "  Bridge not visible yet; will configure after start via host ns if needed"
fi

"${COMPOSE_ARGS[@]}" start

# Re-try bridge multicast knobs now that the bridge exists.
if [[ -f "${SNOOPING}" ]]; then
    ${SUDO} sh -c "echo 0 > ${SNOOPING}" 2>/dev/null || true
    for port in /sys/class/net/"${BRIDGE_NAME}"/brif/*; do
        [[ -d "${port}" ]] || continue
        ${SUDO} sh -c "echo 1 > ${port}/hairpin_mode" 2>/dev/null || true
    done
else
    docker run --rm --privileged --network=host --pid=host \
        alpine sh -c "
        echo 0 > /sys/devices/virtual/net/${BRIDGE_NAME}/bridge/multicast_snooping 2>/dev/null || true
        for port in /sys/class/net/${BRIDGE_NAME}/brif/*; do
            [ -d \"\$port\" ] && echo 1 > \"\$port/hairpin_mode\" 2>/dev/null || true
        done
        " 2>/dev/null || echo "WARNING: could not configure bridge multicast"
fi

LOG_FILE="${PKG_DIR}/test.log"
: > "${LOG_FILE}"
(
    timeout $((TIMEOUT + 30)) "${COMPOSE_ARGS[@]}" logs -f 2>&1 || true
) | tee -a "${LOG_FILE}" &
LOGS_PID=$!

set +e
timeout "${TIMEOUT}" docker wait "${WAIT_CONTAINER}"
WAIT_EXIT=$?
set -e

RESULT_EXIT="$(docker inspect "${WAIT_CONTAINER}" --format='{{.State.ExitCode}}' 2>/dev/null || echo 1)"
if [[ "${WAIT_EXIT}" -eq 124 ]]; then
    echo "TIMEOUT: ${WAIT_CONTAINER} did not exit within ${TIMEOUT}s"
    RESULT_EXIT=1
fi

"${COMPOSE_ARGS[@]}" stop -t 5 2>/dev/null || true
if kill "${LOGS_PID}" 2>/dev/null; then
    wait "${LOGS_PID}" 2>/dev/null || true
fi
if [[ ! -s "${LOG_FILE}" ]]; then
    "${COMPOSE_ARGS[@]}" logs --no-color >> "${LOG_FILE}" 2>&1 || true
fi

"${COMPOSE_ARGS[@]}" down --volumes --remove-orphans 2>/dev/null || true

echo ""
if [[ "${RESULT_EXIT}" == "0" ]]; then
    echo "RESULT: PASS (${TEST_NAME})"
else
    echo "RESULT: FAIL (${TEST_NAME}) exit=${RESULT_EXIT}"
    echo "  Full log: ${LOG_FILE}"
fi
exit "${RESULT_EXIT}"
