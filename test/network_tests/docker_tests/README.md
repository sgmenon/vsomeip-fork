# Bazel-built multi-host network tests (Cruise-style).

Host `bazel build` produces binaries; thin Ubuntu Compose containers provide
the two-node topology + multicast.

```bash
./run-test.sh event_test          # plain UDP event subscribe
./run-test.sh e2e_crc             # CRC8 / CRC32 custom E2E
./run-test.sh e2e_p04             # AUTOSAR Profile 04
./run-test.sh e2e_p07             # AUTOSAR Profile 07
```

E2E suites stage `libvsomeip3-e2e.so.3` so routing can dlopen the stock
plugin (no `VSOMEIP_E2E_PROTECTION_MODULE` override needed). Apps under
test send hole-free payloads; see
[`documentation/e2e-scatter-gather-send.md`](../../../documentation/e2e-scatter-gather-send.md).
