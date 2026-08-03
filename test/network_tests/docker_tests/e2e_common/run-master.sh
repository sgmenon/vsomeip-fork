#!/bin/bash
# Generic E2E master (client) supervisor. Env: CLIENT_BIN, CLIENT_CFG.
# Compose gates on slave healthcheck.
set -uo pipefail

log() { echo "[e2e-master] $*"; }

APP_DIR=/app
export LD_LIBRARY_PATH="${APP_DIR}/lib:${LD_LIBRARY_PATH:-}"
export VSOMEIP_CONFIGURATION="${APP_DIR}/configs/${CLIENT_CFG:?CLIENT_CFG unset}"
export VSOMEIP_APPLICATION_NAME=client-sample

CLIENT="${APP_DIR}/${CLIENT_BIN:?CLIENT_BIN unset}"

log "config=${VSOMEIP_CONFIGURATION} bin=${CLIENT}"
"${CLIENT}" --remote
RC=$?
log "${CLIENT_BIN} exited rc=${RC}"
exit "${RC}"
