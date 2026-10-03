# syntax=docker/dockerfile:1.7
ARG BASE_IMAGE=cgr.dev/chainguard/wolfi-base:latest

FROM ${BASE_IMAGE} AS deps

RUN apk add --no-cache \
        build-base \
        ca-certificates \
        curl \
        flatbuffers \
        flatbuffers-dev \
        ninja-build \
        pkgconf \
        rabbitmq-c-dev \
        sqlite-dev \
        tini

ENV PATH=/usr/lib/ninja-build/bin:${PATH}
RUN ninja --version

ARG CMAKE_VERSION=4.4.2
RUN arch="$(uname -m)" \
    && case "${arch}" in \
         x86_64) cmake_arch=linux-x86_64 ;; \
         aarch64) cmake_arch=linux-aarch64 ;; \
         *) echo "unsupported architecture: ${arch}" >&2; exit 1 ;; \
       esac \
    && curl -fsSL -o /tmp/cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-${cmake_arch}.tar.gz" \
    && mkdir -p /opt/cmake \
    && tar -xzf /tmp/cmake.tar.gz -C /opt/cmake --strip-components=1 \
    && rm /tmp/cmake.tar.gz

ENV PATH=/opt/cmake/bin:${PATH}
