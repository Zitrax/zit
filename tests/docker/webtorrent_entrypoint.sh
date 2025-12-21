#!/bin/sh
set -e

HOST_IP="${HOST_IP:-}"

# 1) If user set HOST_IP env, use it (already handled by expansion above)

# 2) Try getent ahostsv4 (Docker Desktop / some setups)
if [ -z "$HOST_IP" ] && command -v getent >/dev/null 2>&1; then
	HOST_IP=$(getent ahostsv4 host.docker.internal 2>/dev/null | awk '{print $1; exit}' || true)
fi

# 3) Try classic getent hosts lookup
if [ -z "$HOST_IP" ] && command -v getent >/dev/null 2>&1; then
	HOST_IP=$(getent hosts host.docker.internal 2>/dev/null | awk '{print $1; exit}' || true)
fi

# 4) Fallback: use container default gateway (usually the host's docker bridge IP)
if [ -z "$HOST_IP" ] && command -v ip >/dev/null 2>&1; then
	HOST_IP=$(ip route 2>/dev/null | awk '/default/ {print $3; exit}' || true)
fi

# 5) Last-resort: try /sbin/ip route (some minimal images)
if [ -z "$HOST_IP" ] && [ -x /sbin/ip ]; then
	HOST_IP=$(/sbin/ip route 2>/dev/null | awk '/default/ {print $3; exit}' || true)
fi

if [ -z "$HOST_IP" ]; then
	echo "Warning: HOST_IP could not be determined inside the container." >&2
	echo "You can set HOST_IP env or run container with --add-host=host.docker.internal:host-gateway" >&2
	exit 1
fi

# Set up iptables rule to route traffic to host.docker.internal
# This is necessary because opentracker will use the Docker gateway IP
# for the peer list which will not work from within the container
/usr/sbin/iptables -t nat -A OUTPUT -d 172.17.0.1 -j DNAT --to-destination "$HOST_IP"

# If host UID/GID are provided, fix ownership of the bind mount and
# drop privileges using gosu so files get correct ownership
if [ -n "$HOST_UID" ] && [ -n "$HOST_GID" ] && command -v gosu >/dev/null 2>&1; then
	mkdir -p /data 2>/dev/null || true
	chown -R "$HOST_UID:$HOST_GID" /data 2>/dev/null || true
	exec gosu "$HOST_UID:$HOST_GID" webtorrent "$@"
fi

# Fallback: run as root if HOST_UID/GID not provided
exec webtorrent "$@"
