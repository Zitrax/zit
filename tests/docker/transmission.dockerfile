# Use fixed ubuntu image for reproducibility
FROM ubuntu:24.10

# Install necessary packages
RUN apt-get update && apt-get install -y \
    # Requirements from the repositories documentation:
    #   https://github.com/transmission/transmission/blob/main/docs/Building-Transmission.md#on-unix
    build-essential cmake git libcurl4-openssl-dev libssl-dev \
    # Additional packages for the build process added by me:
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

# Clone the Transmission repository
RUN git clone --recurse-submodules --branch 4.0.6 https://github.com/transmission/transmission.git /opt/transmission

# Set the working directory
WORKDIR /opt/transmission

# Build the Transmission project
RUN cmake -B build -G Ninja \
    -DENABLE_DAEMON=OFF \
    -DINSTALL_WEB=OFF \
    -DENABLE_UTILS=OFF \
    -DENABLE_CLI=ON \
    -DENABLE_TESTS=OFF \
    -DINSTALL_DOC=OFF \
    -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build --target install

# Expose the necessary ports
EXPOSE 51413

# Set the entry point to run transmission-cli
ENTRYPOINT ["/opt/transmission/build/cli/transmission-cli"]
