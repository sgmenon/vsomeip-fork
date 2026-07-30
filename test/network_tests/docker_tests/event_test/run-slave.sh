#!/bin/bash
# Slave (service) supervisor for Bazel+Docker event_test.
# Mounted at /app/run-slave.sh. Starts routingmanagerd + event_test_service.
set -uo pipefail

log() { echo "[event-test-slave] $*"; }

APP_DIR=/app
export LD_LIBRARY_PATH="${APP_DIR}/lib:${LD_LIBRARY_PATH:-}"
export VSOMEIP_CONFIGURATION="${APP_DIR}/configs/event_test_slave_udp.json"

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

# Signal master that the service stack is up (healthcheck).
touch /tmp/slave-ready
log "starting event_test_service UDP"
export VSOMEIP_APPLICATION_NAME=event_test_service
"${APP_DIR}/event_test_service" UDP
RC=$?
log "event_test_service exited rc=${RC}"
exit "${RC}"
