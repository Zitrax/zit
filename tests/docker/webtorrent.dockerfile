FROM ubuntu:24.10

# Install nodejs and npm
RUN apt-get update && \
    apt-get install -y npm && \
    rm -rf /var/lib/apt/lists/*

RUN npm install webtorrent-cli -g

EXPOSE 51413

ENTRYPOINT [ "webtorrent" ]