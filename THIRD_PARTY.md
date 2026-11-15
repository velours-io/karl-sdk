# Third-party software

The SDK's own files are under [LICENSE](LICENSE). The build image
(`docker/`) adds open source software under its own licenses:

| Software | License | Source |
|---|---|---|
| Debian 12 (bookworm) base, GCC 12 aarch64 cross compiler, glibc, libstdc++ | GPL / LGPL with the GCC runtime exception | Debian source packages, https://sources.debian.org |
| OpenSceneGraph 3.7 headers and libraries (as built for KARL) | OpenSceneGraph Public License (LGPL based) | https://github.com/openscenegraph/OpenSceneGraph |
| OpenThreads | OpenSceneGraph Public License | same repository |
| nlohmann/json 3.11 (headers) | MIT | https://github.com/nlohmann/json |
| Mesa OpenGL headers | MIT | https://gitlab.freedesktop.org/mesa/mesa |
| CMake, Ninja | BSD-3-Clause, Apache-2.0 | Debian source packages |

Plugins link to OpenSceneGraph dynamically, against the libraries on the KARL
itself, which keeps the LGPL terms simple for plugin authors.
