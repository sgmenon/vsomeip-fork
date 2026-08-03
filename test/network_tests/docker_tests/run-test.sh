#!/usr/bin/env bash
# Host Bazel build + Docker Compose runner for vsomeip network tests.
#
# Pattern (Cruise-style):
#   1. bazel build the suite binaries on the host
#   2. stage them into /tmp/vsomeip-docker/<suite> via setup.sh
#   3. docker compose up thin Ubuntu images with fixed IPs + multicast
#   4. docker wait on the verdict container
#
# Usage:
#   ./run-test.sh event_test
#   ./run-test.sh event_test --skip-build
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

SKIP_BUILD=false
OVERRIDE_TIMEOUT=""
TEST_NAME=""

usage() {
    cat <<EOF
Usage: $0 <test-name> [--skip-build] [--timeout SECONDS]

Available tests:
$(for d in "${SCRIPT_DIR}"/*/; do
    [[ -f "${d}/test.env" ]] && echo "  $(basename "${d}")"
done)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build) SKIP_BUILD=true; shift ;;
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
BAZEL_TARGETS="${BAZEL_TARGETS:?BAZEL_TARGETS must be set in test.env}"

echo "=== vsomeip Docker network test: ${TEST_NAME} ==="
echo "  Timeout:    ${TIMEOUT}s"
echo "  Bridge:     ${BRIDGE_NAME}"
echo "  Pkg dir:    ${PKG_DIR}"
echo "  Wait on:    ${WAIT_CONTAINER}"

if [[ "${SKIP_BUILD}" = false ]]; then
    echo ""
    echo "--- bazel build ---"
    cd "${REPO_ROOT}"
    # shellcheck disable=SC2086
    if ! bazel build ${BAZEL_TARGETS}; then
        echo "FAIL: bazel build failed" >&2
        exit 1
    fi
fi

echo ""
echo "--- staging package ---"
${SUDO} rm -rf "${PKG_DIR:?}" 2>/dev/null || true
mkdir -p "${PKG_DIR}"
export PKG_DIR
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
