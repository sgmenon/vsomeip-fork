#!/usr/bin/env bash
# Stage one E2E suite. Caller is a suite setup.sh; SUITE_DIR is that suite.
# Expects CLIENT_BIN, SERVICE_BIN, CLIENT_CFG, SERVICE_CFG, PKG_DIR exported.
set -euo pipefail

SUITE_DIR="${SUITE_DIR:?SUITE_DIR must be set to the suite directory}"
COMMON_DIR="$(cd "${SUITE_DIR}/../e2e_common" && pwd)"
REPO_ROOT="$(cd "${SUITE_DIR}/../../../.." && pwd)"
PKG_DIR="${PKG_DIR:?PKG_DIR must be set}"

CLIENT_BIN="${CLIENT_BIN:?}"
SERVICE_BIN="${SERVICE_BIN:?}"
CLIENT_CFG="${CLIENT_CFG:?}"
SERVICE_CFG="${SERVICE_CFG:?}"

STAGE_BINS=(
    "test/network_tests/e2e_tests/${CLIENT_BIN}"
    "test/network_tests/e2e_tests/${SERVICE_BIN}"
)

# shellcheck disable=SC1091
source "${SUITE_DIR}/../lib/stage-vsomeip-libs.sh"

cp "${SUITE_DIR}/configs/${CLIENT_CFG}" "${PKG_DIR}/configs/"
cp "${SUITE_DIR}/configs/${SERVICE_CFG}" "${PKG_DIR}/configs/"
cp "${COMMON_DIR}/run-master.sh" "${PKG_DIR}/app/run-master.sh"
cp "${COMMON_DIR}/run-slave.sh" "${PKG_DIR}/app/run-slave.sh"
chmod +x "${PKG_DIR}/app/run-master.sh" "${PKG_DIR}/app/run-slave.sh"

cp -r "${SUITE_DIR}/../docker_infra" "${PKG_DIR}/docker_infra"
chmod +x "${PKG_DIR}/docker_infra/entrypoint.sh"

# run-test.sh looks for ${TEST_DIR}/docker-compose.yml
cp "${COMMON_DIR}/docker-compose.yml" "${SUITE_DIR}/docker-compose.yml"

echo "setup.sh: staged ${CLIENT_BIN} / ${SERVICE_BIN} into ${PKG_DIR}/app"
ls -la "${PKG_DIR}/app" "${PKG_DIR}/app/lib" | sed 's/^/  /'
