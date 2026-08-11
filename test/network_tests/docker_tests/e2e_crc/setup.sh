#!/usr/bin/env bash
set -euo pipefail
export SUITE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SUITE_DIR}/../e2e_common/setup-suite.sh"
