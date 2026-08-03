#!/bin/bash
# Generic E2E slave (service) supervisor. Env: SERVICE_BIN, SERVICE_CFG.
set -uo pipefail

log() { echo "[e2e-slave] $*"; }

APP_DIR=/app
export LD_LIBRARY_PATH="${APP_DIR}/lib:${LD_LIBRARY_PATH:-}"
export VSOMEIP_CONFIGURATION="${APP_DIR}/configs/${SERVICE_CFG:?SERVICE_CFG unset}"
export VSOMEIP_APPLICATION_NAME=service-sample

SERVICE="${APP_DIR}/${SERVICE_BIN:?SERVICE_BIN unset}"

log "config=${VSOMEIP_CONFIGURATION} bin=${SERVICE}"
touch /tmp/slave-ready
"${SERVICE}" --remote
RC=$?
log "${SERVICE_BIN} exited rc=${RC}"
exit "${RC}"
