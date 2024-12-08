FROM ubuntu:24.10

# Install nodejs and npm
RUN apt-get update && \
    apt-get install -y npm iptables && \
    rm -rf /var/lib/apt/lists/*

RUN npm install webtorrent-cli -g

COPY webtorrent_entrypoint.sh /usr/local/bin/webtorrent_entrypoint.sh
RUN chmod +x /usr/local/bin/webtorrent_entrypoint.sh

EXPOSE 51413

ENTRYPOINT [ "/usr/local/bin/webtorrent_entrypoint.sh" ]
