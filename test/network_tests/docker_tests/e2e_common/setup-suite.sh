#!/usr/bin/env bash
# Stage one E2E suite from its pkg_tar into PKG_DIR.
# Caller (e2e_*/setup.sh) must export SUITE_DIR; run-test.sh exports PKG_DIR + PACKAGE_TAR.
set -euo pipefail

SUITE_DIR="${SUITE_DIR:?SUITE_DIR must be set to the suite directory}"
COMMON_DIR="$(cd "${SUITE_DIR}/../e2e_common" && pwd)"
PKG_DIR="${PKG_DIR:?PKG_DIR must be set}"
PACKAGE_TAR="${PACKAGE_TAR:?PACKAGE_TAR must be set (run-test.sh builds or --package)}"

if [[ ! -f "${PACKAGE_TAR}" ]]; then
    echo "setup.sh: package tar missing: ${PACKAGE_TAR}" >&2
    exit 1
fi

mkdir -p "${PKG_DIR}"
tar -xf "${PACKAGE_TAR}" -C "${PKG_DIR}"

# run-test.sh looks for ${TEST_DIR}/docker-compose.yml
cp "${COMMON_DIR}/docker-compose.yml" "${SUITE_DIR}/docker-compose.yml"

echo "setup.sh: extracted $(basename "${PACKAGE_TAR}") into ${PKG_DIR}"
ls -la "${PKG_DIR}/app" "${PKG_DIR}/app/lib" "${PKG_DIR}/configs" | sed 's/^/  /'
