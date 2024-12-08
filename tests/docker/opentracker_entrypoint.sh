#!/bin/sh
set -e

# Resolve the host IP address
HOST_IP=$(getent ahostsv4 host.docker.internal | grep 'STREAM' | awk '{ print $1 }' | head -n 1)
#HOST_IP=192.168.0.18

# Set up iptables rule to route traffic to host.docker.internal
# This is necessary because opentracker uses will use the Docker gateway IP
# for the peer list which will not work from within the container
iptables -t nat -A OUTPUT -d 172.17.0.1 -j DNAT --to-destination "$HOST_IP"

# Run opentracker with the provided arguments
exec opentracker "$@"
