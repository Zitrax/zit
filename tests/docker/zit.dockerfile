# Simple runtime image - uses pre-built zit binary from host
FROM ubuntu:24.04

# Install only actual runtime dependencies (from ldd)
RUN apt-get update && \
    apt-get install -y \
    libstdc++6 \
    curl \
    iptables \
    iproute2 \
    gosu \
    llvm && \
    rm -rf /var/lib/apt/lists/*

# Copy pre-built zit binary from host build directory
COPY out/build/zit-clang-user-debug/src/zit /usr/local/bin/zit
COPY tests/docker/zit_entrypoint.sh /usr/local/bin/zit_entrypoint.sh
COPY tests/docker/tsan_suppressions.txt /tsan_suppressions.txt
RUN chmod +x /usr/local/bin/zit /usr/local/bin/zit_entrypoint.sh

WORKDIR /data

ENV ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer
ENV TSAN_OPTIONS="external_symbolizer_path=/usr/bin/llvm-symbolizer suppressions=/tsan_suppressions.txt"

ENTRYPOINT ["/usr/local/bin/zit_entrypoint.sh"]
