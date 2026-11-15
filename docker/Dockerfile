# The KARL plugin build image: the compiler and libraries KARL itself uses.
# Built by Velours from a KARL OS sysroot (docker/prepare.sh).
#   docker run --rm -v "$PWD":/src ghcr.io/velours-io/karl-sdk:abi1
# builds the CMake project in the current folder into ./dist/<name>.so.
# Built and run as linux/amd64 (--platform): the cross compiler is x86_64.
FROM debian:bookworm-slim
# Links the ghcr.io package to the SDK repo (and its license).
LABEL org.opencontainers.image.source="https://github.com/velours-io/karl-sdk" \
      org.opencontainers.image.description="Build image for KARL plugins" \
      org.opencontainers.image.licenses="LicenseRef-KARL-SDK"

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      g++-aarch64-linux-gnu cmake ninja-build file libgl-dev ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# OpenSceneGraph, OpenThreads and nlohmann json as on KARL (prepare.sh), and
# the GL headers OpenSceneGraph includes.
COPY docker/sysroot/ /opt/karl-sysroot/
RUN cp -r /usr/include/GL /usr/include/KHR /opt/karl-sysroot/usr/local/include/

COPY include/ /opt/karl-sdk/include/
COPY cmake/ /opt/karl-sdk/cmake/
COPY examples/ /opt/karl-sdk/examples/
COPY LICENSE THIRD_PARTY.md README.md /opt/karl-sdk/
COPY docker/karl-build /usr/local/bin/karl-build

ENV KARL_SDK=/opt/karl-sdk
WORKDIR /src
ENTRYPOINT ["karl-build"]
