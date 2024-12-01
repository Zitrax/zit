# Testing

## Docker

Integration testing are currently setup to run the following:

* Tracker: openstracker in a docker container
* Other torrent client: webtorrent in a docker container
* zit running non containerized

Note that webtorrent neither like tracker/announce urls like localhost or 127.0.0.1.
Using the real 192.168.x.y local ip seem to work though. For this the actual torrent
file need to be modified to contain the actual IP.

TODO: Avoid having to hardcode that.

Also note that the host (at least on windows and mac) can't talk directly to the
docker IP range (172.17.x.y) since it's running in it's own VM. To get that to
work we must ensure that we translate those addresses to the exposed ports on the
host. For now there are translation functions in the code for doing this.
