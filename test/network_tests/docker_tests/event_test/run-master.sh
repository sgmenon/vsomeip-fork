#!/bin/bash
# Master (client) supervisor for Bazel+Docker event_test.
# Mounted at /app/run-master.sh. Starts routingmanagerd + event_test_client.
# Compose gates this container on slave healthcheck.
set -uo pipefail

log() { echo "[event-test-master] $*"; }

APP_DIR=/app
export LD_LIBRARY_PATH="${APP_DIR}/lib:${LD_LIBRARY_PATH:-}"
export VSOMEIP_CONFIGURATION="${APP_DIR}/configs/event_test_master.json"

TESTMODE="${TESTMODE:-PAYLOAD_FIXED}"
COMMUNICATIONMODE="${COMMUNICATIONMODE:-UDP}"

cleanup() {
    log "tearing down"
    if [[ -n "${PID_RMD:-}" ]]; then
        kill "${PID_RMD}" 2>/dev/null || true
        wait "${PID_RMD}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

log "starting routingmanagerd"
export VSOMEIP_APPLICATION_NAME=routingmanagerd
"${APP_DIR}/routingmanagerd" &
PID_RMD=$!
sleep 1
if ! kill -0 "${PID_RMD}" 2>/dev/null; then
    log "FAIL: routingmanagerd did not stay up"
    exit 1
fi

log "starting event_test_client ${TESTMODE} ${COMMUNICATIONMODE}"
export VSOMEIP_APPLICATION_NAME=event_test_client
"${APP_DIR}/event_test_client" "${TESTMODE}" "${COMMUNICATIONMODE}"
RC=$?
log "event_test_client exited rc=${RC}"
exit "${RC}"
