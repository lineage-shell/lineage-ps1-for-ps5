# Build image for SwanStationPS5 and the Linux interface preview (Ubuntu 24.04, README "Building from source").
#   docker build -t swanstation-build tools/ui-preview
#   docker run --rm -u $(id -u):$(id -g) -e GH_TOKEN=$(gh auth token) -e GITHUB_REPOSITORY=darkxex/SwanStationPS5 -v $PWD:/work swanstation-build make
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      clang-18 lld-18 llvm-18 libclang-rt-18-dev make ninja-build ccache pkg-config python3 python3-venv python3-pip \
      tar unzip wget curl git ca-certificates xz-utils zstd patch cmake file bzip2 \
      glslang-tools gh libsdl2-dev python3-pil gcc g++ \
    && ln -sf /usr/bin/clang-18 /usr/local/bin/clang && ln -sf /usr/bin/clang++-18 /usr/local/bin/clang++ \
    && rm -rf /var/lib/apt/lists/*
ENV HOME=/tmp/home CCACHE_DIR=/work/.ccache
RUN mkdir -p /tmp/home && chmod 777 /tmp/home
WORKDIR /work
