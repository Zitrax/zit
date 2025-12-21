#!/bin/sh
set -e

HOST_IP="${HOST_IP:-}"

if [ -z "$HOST_IP" ]; then
	echo "ERROR: HOST_IP not set." >&2
	echo "You can set HOST_IP env or run container with --add-host=host.docker.internal:host-gateway" >&2
	exit 1
fi

# Set up iptables rule to route traffic to host.docker.internal
# This is necessary because opentracker will use the Docker gateway IP
# for the peer list which will not work from within the container
/usr/sbin/iptables -t nat -A OUTPUT -d 172.17.0.1 -j DNAT --to-destination "$HOST_IP"

# Run opentracker with the provided arguments
exec webtorrent "$@"
