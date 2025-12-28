# Use fixed ubuntu image for reproducibility
FROM ubuntu:24.04

# Install necessary packages
RUN apt-get update && apt-get install -y \
    opentracker iptables \
    && rm -rf /var/lib/apt/lists/*

# Copy the entrypoint script
COPY tests/docker/opentracker_entrypoint.sh /usr/local/bin/opentracker_entrypoint.sh
RUN chmod +x /usr/local/bin/opentracker_entrypoint.sh

# Expose the necessary ports
EXPOSE 8000

# Set the entry point to run opentracker
ENTRYPOINT ["/usr/local/bin/opentracker_entrypoint.sh"]
