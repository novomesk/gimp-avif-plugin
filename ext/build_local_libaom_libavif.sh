#!/bin/bash

if ! [ -x "$(command -v cmake)" ]; then
  echo 'Error: cmake is not installed.' >&2
  exit 1
fi

if ! [ -x "$(command -v ninja)" ]; then
  echo 'Error: ninja is not installed.' >&2
  exit 1
fi

RELATIVE_PATH=`dirname "$BASH_SOURCE"`
cd "$RELATIVE_PATH"

if ! [ -f libavif/ext/aom/build.libavif/libaom.a ]; then
  echo 'We are going to build libaom.a'
  cd libavif/ext/aom
  mkdir -p build.libavif
  cd build.libavif

  cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_DOCS=0 -DENABLE_EXAMPLES=0 -DENABLE_TESTDATA=0 -DENABLE_TESTS=0 -DENABLE_TOOLS=0 ..
  ninja

  if ! [ -f libaom.a ]; then
    echo 'Error: libaom.a build failed!' >&2
    exit 1
  fi
  cd ../../../..
fi


if ! [ -f libavif/build/libavif.a ]; then
  echo 'We are going to build libavif.a'
  cd libavif
  mkdir -p build
  cd build

  cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DAVIF_CODEC_AOM=ON -DAVIF_LOCAL_AOM=ON ..
  ninja

  if ! [ -f libavif.a ]; then
    echo 'Error: libavif.a build failed!' >&2
    exit 1
  fi
  cd ../..
fi

exit 0
