FROM ubuntu:24.04

# Install nodejs, webtorrent-cli, iptables tools, and gosu for UID/GID drop
RUN apt-get update && \
    apt-get install -y npm iptables iproute2 gosu && \
    rm -rf /var/lib/apt/lists/* && \
    npm install webtorrent-cli -g

COPY tests/docker/webtorrent_entrypoint.sh /usr/local/bin/webtorrent_entrypoint.sh
RUN chmod +x /usr/local/bin/webtorrent_entrypoint.sh

EXPOSE 51413

ENTRYPOINT [ "/usr/local/bin/webtorrent_entrypoint.sh" ]
