FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ARG DEBIAN_FRONTEND=noninteractive

# Host dependencies from the Zephyr Getting Started Guide (Ubuntu 24.04).
# gcc-multilib/g++-multilib don't exist on arm64 (Apple Silicon), so amd64 only.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       git cmake ninja-build gperf ccache dfu-util device-tree-compiler wget \
       python3-dev python3-venv python3-tk xz-utils file make gcc g++ \
       libsdl2-dev libmagic1 \
    && if [ "$(dpkg --print-architecture)" = "amd64" ]; then \
         apt-get install -y --no-install-recommends gcc-multilib g++-multilib; \
       fi \
    && rm -rf /var/lib/apt/lists/*

# Mount point of the Docker volume with the west workspace, venv and SDK.
# Owned by vscode, so a fresh volume takes over these permissions.
RUN mkdir -p /workdir && chown vscode:vscode /workdir

# Stand-in for 'openocd': forwards west flash/debug to OpenOCD on the host,
# because Docker Desktop doesn't pass USB devices into the container.
COPY --chmod=755 openocd-remote /usr/local/bin/openocd-remote
