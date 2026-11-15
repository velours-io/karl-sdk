#!/bin/zsh
# Copies from a KARL OS sysroot what plugins compile and link against into
# docker/sysroot/ (not in git), for `docker build`. Velours only: users pull
# the image.
#   RPI5_SYSROOT=~/rpi5-sysroot software/sdk/docker/prepare.sh
set -euo pipefail

SRC=${RPI5_SYSROOT:?set RPI5_SYSROOT to the KARL OS sysroot}/usr/local
OUT=${0:A:h}/sysroot/usr/local
rm -rf ${0:A:h}/sysroot
mkdir -p $OUT/include $OUT/lib $OUT/share/cmake

for dir in $SRC/include/osg* $SRC/include/OpenThreads $SRC/include/nlohmann; do
  cp -R $dir $OUT/include/
done
# Every OpenSceneGraph library, as the symlinks and the files they point to.
cp -P $SRC/lib/libosg*.so* $SRC/lib/libOpenThreads.so* $OUT/lib/
cp -R $SRC/share/cmake/nlohmann_json $OUT/share/cmake/
echo "sysroot ready: $(du -sh ${0:A:h}/sysroot | cut -f1)"
