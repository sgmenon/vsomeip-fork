#!/bin/bash
# Sets up multicast routing for vsomeip inside Docker containers.
# Without this, the kernel has no route for multicast addresses and vsomeip
# SD packets never leave the container.

set -e

IFACE=$(ip route | grep default | awk '{print $5}' | head -n1)
if [ -z "$IFACE" ]; then
    IFACE=$(ip -o link show | awk -F': ' '{print $2}' | grep -v lo | head -n1)
fi

echo "=== Multicast setup: iface=$IFACE ip=$(hostname -I | awk '{print $1}') ==="

if ! ip route show | grep -q "224.0.0.0/4"; then
    ip route add 224.0.0.0/4 dev "$IFACE"
fi

# Disable reverse-path filtering — rp_filter drops incoming multicast whose
# source doesn't match the interface's reverse route.
sysctl -w net.ipv4.conf.all.rp_filter=0       2>/dev/null || true
sysctl -w net.ipv4.conf.default.rp_filter=0   2>/dev/null || true
sysctl -w "net.ipv4.conf.$IFACE.rp_filter=0"  2>/dev/null || true

exec "$@"
