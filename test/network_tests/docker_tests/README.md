# Bazel-built multi-host network tests (Cruise-style).

Host `bazel build` produces a per-suite `pkg_tar`; thin Ubuntu Compose
containers provide the two-node topology + multicast.

```bash
# Build all E2E staging tars
bazel build //test/network_tests/docker_tests:e2e_pkgs

# Run (builds the suite tar if needed)
./run-test.sh e2e_crc
./run-test.sh e2e_p04
./run-test.sh e2e_p07

# Or reuse a pre-built tar (CI path)
./run-test.sh e2e_crc --package bazel-bin/test/network_tests/docker_tests/e2e_crc_pkg.tar

./run-test.sh event_test   # legacy binary staging (not yet pkg_tar)
```

CI: [`.github/workflows/e2e_docker_tests.yml`](../../../.github/workflows/e2e_docker_tests.yml)
builds `//test/network_tests/docker_tests:e2e_pkgs` once, uploads the three
tars, then a matrix runs each suite with `--package` (no bazel-bin restore).

E2E suites include `libvsomeip3-e2e.so.3` in the tar so routing can dlopen
the stock plugin. Apps under test send hole-free payloads; see
[`documentation/e2e-scatter-gather-send.md`](../../../documentation/e2e-scatter-gather-send.md).
