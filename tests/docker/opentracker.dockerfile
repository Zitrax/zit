# Use fixed ubuntu image for reproducibility
FROM ubuntu:24.10

# Install necessary packages
RUN apt-get update && apt-get install -y \
    opentracker \
    && rm -rf /var/lib/apt/lists/*

# Expose the necessary ports
EXPOSE 8000

# Set the entry point to run opentracker
ENTRYPOINT ["opentracker"]
