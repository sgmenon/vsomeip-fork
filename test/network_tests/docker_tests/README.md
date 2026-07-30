# Bazel-built multi-host network tests (Cruise-style).

Host `bazel build` produces binaries; thin Ubuntu Compose containers provide
the two-node topology + multicast. See `run-test.sh` and `event_test/`.
