#!/usr/bin/env bash
# Shared staging helper for Bazel+Docker network tests.
# Expects: PKG_DIR, REPO_ROOT, and STAGE_BINS (bash array of bazel-bin-relative paths).
set -euo pipefail

BAZEL_BIN="${REPO_ROOT}/bazel-bin"
if [[ ! -d "${BAZEL_BIN}" ]]; then
    echo "stage-vsomeip-libs: ${BAZEL_BIN} missing — run bazel build first" >&2
    exit 1
fi

mkdir -p "${PKG_DIR}/app/lib" "${PKG_DIR}/configs"

for label_path in "${STAGE_BINS[@]}"; do
    name="$(basename "${label_path}")"
    src="${BAZEL_BIN}/${label_path}"
    if [[ ! -f "${src}" ]]; then
        echo "stage-vsomeip-libs: missing binary ${src}" >&2
        exit 1
    fi
    cp -L "${src}" "${PKG_DIR}/app/${name}"
    chmod +x "${PKG_DIR}/app/${name}"
done

shopt -s nullglob
for lib in \
    "${BAZEL_BIN}"/libvsomeip3.so \
    "${BAZEL_BIN}"/libvsomeip3.so.* \
    "${BAZEL_BIN}"/libvsomeip3-core.so \
    "${BAZEL_BIN}"/libvsomeip3-core.so.* \
    "${BAZEL_BIN}"/libvsomeip3-sd.so \
    "${BAZEL_BIN}"/libvsomeip3-sd.so.* \
    "${BAZEL_BIN}"/libvsomeip3-cfg.so \
    "${BAZEL_BIN}"/libvsomeip3-cfg.so.* \
    "${BAZEL_BIN}"/libvsomeip3-e2e.so \
    "${BAZEL_BIN}"/libvsomeip3-e2e.so.*
do
    [[ -e "${lib}" ]] || continue
    cp -L "${lib}" "${PKG_DIR}/app/lib/"
done
shopt -u nullglob

# Harvest versioned plugins from runfiles / bazel-bin if missing.
ensure_plugin() {
    local soname="$1"
    if [[ -e "${PKG_DIR}/app/lib/${soname}" ]]; then
        return 0
    fi
    local src
    src="$(find "${BAZEL_BIN}" -name "${soname}" -type f 2>/dev/null | head -n1 || true)"
    if [[ -z "${src}" ]]; then
        echo "stage-vsomeip-libs: ${soname} not found under ${BAZEL_BIN}" >&2
        exit 1
    fi
    cp -L "${src}" "${PKG_DIR}/app/lib/"
}

ensure_plugin "libvsomeip3-sd.so.3"
ensure_plugin "libvsomeip3-e2e.so.3"
