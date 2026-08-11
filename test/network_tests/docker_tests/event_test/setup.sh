#!/usr/bin/env bash
# Stage host-built Bazel binaries + shared libs into PKG_DIR for Compose.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
PKG_DIR="${PKG_DIR:?PKG_DIR must be set}"

# Prefer the workspace bazel-bin symlink; `bazel info` can fail if the
# toolchain extension is mid-refresh even after a successful build.
BAZEL_BIN="${REPO_ROOT}/bazel-bin"
if [[ ! -d "${BAZEL_BIN}" ]]; then
    echo "setup.sh: ${BAZEL_BIN} missing — run bazel build first" >&2
    exit 1
fi

mkdir -p "${PKG_DIR}/app/lib" "${PKG_DIR}/configs"

copy_bin() {
    local label_path="$1"
    local name
    name="$(basename "${label_path}")"
    local src="${BAZEL_BIN}/${label_path}"
    if [[ ! -f "${src}" ]]; then
        echo "setup.sh: missing binary ${src}" >&2
        exit 1
    fi
    cp -L "${src}" "${PKG_DIR}/app/${name}"
    chmod +x "${PKG_DIR}/app/${name}"
}

copy_bin "test/network_tests/event_tests/event_test_client"
copy_bin "test/network_tests/event_tests/event_test_service"
copy_bin "examples/routingmanagerd/routingmanagerd"

# Shared libraries produced next to the package root in bazel-bin.
# Prefer versioned SD/CFG sonames that plugin_manager dlopens.
shopt -s nullglob
for lib in \
    "${BAZEL_BIN}"/libvsomeip3.so \
    "${BAZEL_BIN}"/libvsomeip3.so.* \
    "${BAZEL_BIN}"/libvsomeip3-core.so \
    "${BAZEL_BIN}"/libvsomeip3-core.so.* \
    "${BAZEL_BIN}"/libvsomeip3-sd.so \
    "${BAZEL_BIN}"/libvsomeip3-sd.so.* \
    "${BAZEL_BIN}"/libvsomeip3-cfg.so \
    "${BAZEL_BIN}"/libvsomeip3-cfg.so.*
do
    [[ -e "${lib}" ]] || continue
    cp -L "${lib}" "${PKG_DIR}/app/lib/"
done
shopt -u nullglob

# Also harvest any solibs living under the binaries' runfiles trees.
for rf in \
    "${BAZEL_BIN}/test/network_tests/event_tests/event_test_client.runfiles" \
    "${BAZEL_BIN}/test/network_tests/event_tests/event_test_service.runfiles" \
    "${BAZEL_BIN}/examples/routingmanagerd/routingmanagerd.runfiles"
do
    [[ -d "${rf}" ]] || continue
    while IFS= read -r -d '' so; do
        base="$(basename "${so}")"
        # Skip libc / libstdc++ / libgcc — use the image's.
        case "${base}" in
            libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libstdc++.so*|libgcc_s.so*|ld-linux*)
                continue
                ;;
        esac
        cp -L "${so}" "${PKG_DIR}/app/lib/${base}" 2>/dev/null || true
    done < <(find "${rf}" -name 'libvsomeip*.so*' -print0 2>/dev/null)
done

# Ensure the SD soname plugin_manager asks for is present.
if [[ ! -e "${PKG_DIR}/app/lib/libvsomeip3-sd.so.3" ]]; then
    sd_src="$(find "${BAZEL_BIN}" -name 'libvsomeip3-sd.so.3' -type f | head -n1 || true)"
    if [[ -n "${sd_src}" ]]; then
        cp -L "${sd_src}" "${PKG_DIR}/app/lib/"
    else
        echo "setup.sh: libvsomeip3-sd.so.3 not found under ${BAZEL_BIN}" >&2
        exit 1
    fi
fi

cp "${SCRIPT_DIR}/configs/"*.json "${PKG_DIR}/configs/"
cp "${SCRIPT_DIR}/run-master.sh" "${PKG_DIR}/app/run-master.sh"
cp "${SCRIPT_DIR}/run-slave.sh" "${PKG_DIR}/app/run-slave.sh"
chmod +x "${PKG_DIR}/app/run-master.sh" "${PKG_DIR}/app/run-slave.sh"

cp -r "${SCRIPT_DIR}/../docker_infra" "${PKG_DIR}/docker_infra"
chmod +x "${PKG_DIR}/docker_infra/entrypoint.sh"

echo "setup.sh: staged binaries into ${PKG_DIR}/app"
ls -la "${PKG_DIR}/app" "${PKG_DIR}/app/lib" | sed 's/^/  /'
