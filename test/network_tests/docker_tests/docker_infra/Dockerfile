# Shared Docker image for vsomeip multi-host network tests.
#
# Host Bazel builds the binaries; this image only provides networking tools
# and a multicast-ready entrypoint. No CMake / compilers.

FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    iproute2 \
    iputils-ping \
    libatomic1 \
    net-tools \
    tcpdump \
    && rm -rf /var/lib/apt/lists/*

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

WORKDIR /app

ENTRYPOINT ["/entrypoint.sh"]
