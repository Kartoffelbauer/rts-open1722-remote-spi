FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ARG DEBIAN_FRONTEND=noninteractive

# Host-Abhängigkeiten laut Zephyr Getting Started Guide (Ubuntu 24.04).
# gcc-multilib/g++-multilib gibt es nicht auf arm64 (Apple Silicon) -> nur auf amd64.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       git cmake ninja-build gperf ccache dfu-util device-tree-compiler wget \
       python3-dev python3-venv python3-tk xz-utils file make gcc g++ \
       libsdl2-dev libmagic1 \
    && if [ "$(dpkg --print-architecture)" = "amd64" ]; then \
         apt-get install -y --no-install-recommends gcc-multilib g++-multilib; \
       fi \
    && rm -rf /var/lib/apt/lists/*

# Mountpunkt für das Docker-Volume mit West-Workspace, venv und SDK.
# Gehört vscode, damit ein frisches Volume diese Rechte übernimmt.
RUN mkdir -p /workdir && chown vscode:vscode /workdir

# 'openocd'-Ersatz: leitet west flash/debug an OpenOCD auf dem Host weiter,
# weil Docker Desktop kein USB in den Container durchreicht.
COPY --chmod=755 openocd-remote /usr/local/bin/openocd-remote
