# syntax=docker/dockerfile:1
ARG UBUNTU_TAG=22.04
FROM ubuntu:${UBUNTU_TAG}

ARG CMAKE_VERSION=3.26.4
ARG CMAKE_BASE_URL=https://github.com/Kitware/CMake/releases/download
ARG GITHUB_TOKEN

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential git curl wget tar unzip zip pkg-config sudo ca-certificates \
 && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y \
    gdb \
    perl \
    python3 \
    python3-pip \
    python3-venv \
    valgrind \
 && rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    ARCH="$(uname -m)"; \
    case "$ARCH" in \
      x86_64) CMAKE_ARCH=linux-x86_64 ;; \
      aarch64) CMAKE_ARCH=linux-aarch64 ;; \
      *) echo "Unsupported arch: $ARCH" >&2; exit 1 ;; \
    esac; \
    apt-get update && apt-get install -y wget tar && rm -rf /var/lib/apt/lists/*; \
    wget -q "${CMAKE_BASE_URL}/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-${CMAKE_ARCH}.tar.gz" -O /tmp/cmake.tgz; \
    tar --strip-components=1 -xzf /tmp/cmake.tgz -C /usr/local; \
    rm -f /tmp/cmake.tgz

RUN useradd --create-home --shell /bin/bash dev && \
    echo "dev ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers && \
    mkdir -p /workspace && chown dev:dev /workspace

USER dev
WORKDIR /workspace

RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --upgrade pip
ENV PATH="/opt/venv/bin:${PATH}"

RUN set -eux; \
    git clone --depth 1 --branch v2.3.0 --single-branch "https://github.com/lexbor/lexbor.git" "lexbor"; \
    cd "lexbor"; \
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${PREFIX:-/usr/local} -DLEXBOR_BUILD_TESTS=OFF -DLEXBOR_BUILD_EXAMPLES=OFF && \
    cmake --build build -j"$(nproc)" && \
    ${SUDO}cmake --install build
; \
    cd ..; \
    rm -rf "lexbor"

RUN set -eux; \
    git clone --depth 1 --single-branch "https://github.com/benmcollins/libjwt.git" "libjwt"; \
    cd "libjwt"; \
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${PREFIX:-/usr/local} && \
    cmake --build build -j"$(nproc)" && \
    ${SUDO}cmake --install build
; \
    cd ..; \
    rm -rf "libjwt"

RUN set -eux; \
    git clone --depth 1 --branch v1.48.0 --single-branch "https://github.com/libuv/libuv.git" "libuv"; \
    cd "libuv"; \
    cmake -S . -B build -DCMAKE_INSTALL_PREFIX=${PREFIX:-/usr/local} -DBUILD_TESTING=OFF && \
    cmake --build build -j"$(nproc)" && \
    ${SUDO}cmake --install build
; \
    cd ..; \
    rm -rf "libuv"

RUN set -eux; \
    git clone --depth 1 --branch master --single-branch "https://github.com/sqlite/sqlite.git" "sqlite3"; \
    cd "sqlite3"; \
    ./configure --prefix=${PREFIX:-/usr/local} CFLAGS="-O2 -DSQLITE_ENABLE_FTS5=1" && \
    make -j"$(nproc)" && \
    ${SUDO}make install
; \
    cd ..; \
    rm -rf "sqlite3"

RUN set -eux; \
    git clone --depth 1 --single-branch "https://github.com/contactandyc/the-macro-library.git" "the-macro-library"; \
    cd "the-macro-library"; \
    ./build.sh clean && \
    ./build.sh install
; \
    cd ..; \
    rm -rf "the-macro-library"

RUN set -eux; \
    git clone --depth 1 --single-branch "https://github.com/contactandyc/a-memory-library.git" "a-memory-library"; \
    cd "a-memory-library"; \
    ./build.sh clean && \
    ./build.sh install
; \
    cd ..; \
    rm -rf "a-memory-library"

RUN set -eux; \
    git clone --depth 1 --single-branch "https://github.com/contactandyc/a-json-sax-library.git" "a-json-sax-library"; \
    cd "a-json-sax-library"; \
    ./build.sh clean && \
    ./build.sh install
; \
    cd ..; \
    rm -rf "a-json-sax-library"

RUN set -eux; \
    git clone --depth 1 --single-branch "https://github.com/contactandyc/a-json-library.git" "a-json-library"; \
    cd "a-json-library"; \
    ./build.sh clean && \
    ./build.sh install
; \
    cd ..; \
    rm -rf "a-json-library"

RUN set -eux; \
    git clone --depth 1 --single-branch "https://github.com/contactandyc/the-lz4-library.git" "the-lz4-library"; \
    cd "the-lz4-library"; \
    ./build.sh clean && \
    ./build.sh install
; \
    cd ..; \
    rm -rf "the-lz4-library"

RUN set -eux; \
    git clone --depth 1 --single-branch "https://github.com/contactandyc/a-curl-library.git" "a-curl-library"; \
    cd "a-curl-library"; \
    ./build.sh clean && \
    ./build.sh install
; \
    cd ..; \
    rm -rf "a-curl-library"

RUN set -eux; \
    git clone --depth 1 --branch v2.2.6 --single-branch "https://github.com/h2o/h2o.git" "h2o"; \
    cd "h2o"; \
    cmake -S . -B build -DCMAKE_INSTALL_PREFIX=${PREFIX:-/usr/local} -DWITH_MRUBY=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 && \
    cmake --build build -j"$(nproc)" && \
    ${SUDO}cmake --install build
; \
    cd ..; \
    rm -rf "h2o"

RUN set -eux; \
    git clone --depth 1 --single-branch "https://github.com/contactandyc/h2o-c-library.git" "h2o-c-library"; \
    cd "h2o-c-library"; \
    ./build.sh clean && \
    ./build.sh install
; \
    cd ..; \
    rm -rf "h2o-c-library"

RUN set -eux; \
    git clone --depth 1 --single-branch "https://github.com/contactandyc/the-io-library.git" "the-io-library"; \
    cd "the-io-library"; \
    ./build.sh clean && \
    ./build.sh install
; \
    cd ..; \
    rm -rf "the-io-library"

RUN set -eux; \
    git clone --depth 1 --single-branch "https://github.com/contactandyc/a-curl-gcloud-plugin.git" "a-curl-gcloud-plugin"; \
    cd "a-curl-gcloud-plugin"; \
    ./build.sh clean && \
    ./build.sh install
; \
    cd ..; \
    rm -rf "a-curl-gcloud-plugin"


COPY --chown=dev:dev . /workspace/mail
RUN mkdir -p /workspace/build/mail && \
    cd /workspace/build/mail && \
    cmake /workspace/mail -DCMAKE_BUILD_TYPE=Release && \
    make -j"$(nproc)" && sudo make install

# Default behavior for an application stack is to run the binary
CMD ["mail"]
