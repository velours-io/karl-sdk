# The KARL plugin build image: the compiler and libraries KARL itself uses,
# and a simulator to try plugins without a KARL.
# Built by Velours from the monorepo's software/ folder (just sdk-image):
#   docker build --platform linux/amd64 -f sdk/docker/Dockerfile software
# Users only run it (README):
#   docker run --rm -v "$PWD":/src ghcr.io/velours-io/karl-sdk:abi1          builds dist/<name>.so
#   docker run --rm -it -p 6080:6080 -v "$PWD":/src ... karl-sim              the simulator

# --- The simulator: KARL's app built for Linux x86_64. This stage holds the
# app's source and is not part of the published image; only the binary is.
FROM debian:bookworm-slim AS sim-build

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build ca-certificates curl xz-utils pkg-config \
      libopenscenegraph-dev libboost-dev zlib1g-dev nlohmann-json3-dev libgl-dev \
 && rm -rf /var/lib/apt/lists/*

# QuickJS and RtMidi where the app's CMake looks for them on Linux. RtMidi
# without any MIDI system (no ALSA headers, which it would pick up whatever its
# options say): a container has none, and the app runs without USB MIDI then.
RUN curl -fsSL https://bellard.org/quickjs/quickjs-2024-01-13.tar.xz | tar xJ -C /tmp \
 && make -C /tmp/quickjs-2024-01-13 -j"$(nproc)" libquickjs.a \
 && mkdir -p /usr/local/include/quickjs /usr/local/lib/quickjs \
 && cp /tmp/quickjs-2024-01-13/quickjs.h /tmp/quickjs-2024-01-13/quickjs-libc.h /usr/local/include/quickjs/ \
 && cp /tmp/quickjs-2024-01-13/libquickjs.a /usr/local/lib/quickjs/
RUN curl -fsSL https://github.com/thestk/rtmidi/archive/refs/tags/6.0.0.tar.gz | tar xz -C /tmp \
 && cmake -S /tmp/rtmidi-6.0.0 -B /tmp/rtmidi-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DRTMIDI_API_ALSA=OFF -DRTMIDI_API_JACK=OFF -DRTMIDI_BUILD_TESTING=OFF \
 && cmake --build /tmp/rtmidi-build && cmake --install /tmp/rtmidi-build

COPY app/ /build/app/
COPY schema/ /build/schema/
COPY sdk/include/ /build/sdk/include/
RUN cmake -S /build/app -B /build/out -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /build/out \
 && strip /build/out/bin/karl
COPY sdk/docker/karl-manifest.cpp /build/
RUN g++ -std=c++17 -O2 -I/build/sdk/include /build/karl-manifest.cpp -o /build/karl-manifest -ldl

# --- The published image.
FROM debian:bookworm-slim

# Links the ghcr.io package to the SDK repo (and its license).
LABEL org.opencontainers.image.source="https://github.com/velours-io/karl-sdk" \
      org.opencontainers.image.description="Build image and simulator for KARL plugins" \
      org.opencontainers.image.licenses="LicenseRef-KARL-SDK"

# The cross compiler for KARL; the native one, OpenSceneGraph and the rest for
# the simulator's copy of a plugin; and the virtual screen behind the browser
# view (Xvfb, Mesa's software OpenGL, x11vnc, noVNC).
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      g++-aarch64-linux-gnu g++ cmake ninja-build file ca-certificates python3 \
      libopenscenegraph-dev nlohmann-json3-dev libgl-dev \
      xvfb x11vnc novnc python3-websockify libgl1-mesa-dri libglx-mesa0 \
 && rm -rf /var/lib/apt/lists/*

# OpenSceneGraph, OpenThreads and nlohmann json as on KARL (prepare.sh), and
# the GL headers OpenSceneGraph includes.
COPY sdk/docker/sysroot/ /opt/karl-sysroot/
RUN cp -r /usr/include/GL /usr/include/KHR /opt/karl-sysroot/usr/local/include/

# The simulator: the app and what it reads at start.
COPY --from=sim-build /build/out/bin/karl /opt/karl-sim/karl
COPY --from=sim-build /usr/local/lib/librtmidi.so* /usr/local/lib/
COPY --from=sim-build /build/karl-manifest /usr/local/bin/karl-manifest
COPY app/fonts/ /opt/karl-sim/fonts/
COPY app/images/ /opt/karl-sim/images/
RUN ldconfig

COPY sdk/include/ /opt/karl-sdk/include/
COPY sdk/cmake/ /opt/karl-sdk/cmake/
COPY sdk/examples/ /opt/karl-sdk/examples/
COPY sdk/LICENSE sdk/THIRD_PARTY.md sdk/README.md /opt/karl-sdk/
COPY sdk/docker/karl-build sdk/docker/karl-sim sdk/docker/karl-ctl sdk/docker/karl-sim-config /usr/local/bin/

ENV KARL_SDK=/opt/karl-sdk
WORKDIR /src
ENTRYPOINT ["karl-build"]
