FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       ca-certificates \
       curl \
       git \
       zip \
       unzip \
       tar \
       pkg-config \
       cmake \
       ninja-build \
       python3 \
       python3-pip \
       clang-format \
    gdb \
       gcc-13 \
       g++-13 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 130 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 130 \
    && rm -rf /var/lib/apt/lists/*

ENV VCPKG_ROOT=/opt/vcpkg

RUN mkdir -p /opt/vcpkg \
    && chown -R vscode:vscode /opt/vcpkg
