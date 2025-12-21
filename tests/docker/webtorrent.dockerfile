FROM ubuntu:24.04

# Install nodejs and npm
RUN apt-get update && \
    apt-get install -y npm iptables iproute2 && \
    rm -rf /var/lib/apt/lists/* && \
    npm install webtorrent-cli -g

COPY webtorrent_entrypoint.sh /usr/local/bin/webtorrent_entrypoint.sh
RUN chmod +x /usr/local/bin/webtorrent_entrypoint.sh

EXPOSE 51413

ENTRYPOINT [ "/usr/local/bin/webtorrent_entrypoint.sh" ]
