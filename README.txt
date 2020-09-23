file-avif plug-in with AVIF format support for GIMP 2.99 and newer.
The plug-in doesn’t work with versions 2.10.x and older because they use different plug-in API.
Dependencies:
libavif 0.8.1 or newer
https://github.com/AOMediaCodec/libavif

In case your system has libavif library installed, the plug-in will be linked dynamically.
In case your system doesn’t have libavif, the build script will attempt to build libavif locally and static linking will be used. Another library libaom will be built during static local build, sources included.

How to build:
  ./build_file-avif.sh

If you are builing static version you may review build_local_libaom_libavif.sh in ext folder where you can see details how libaom and libavif are being prepared.
If you are sure you are going to make a static build, you may run build_local_libaom_libavif.sh in ext folder before running build_file-avif.sh,
you will compilation process of libaom and libavif.

yasm is needed to build libaom!

Examples of files in AVIF format:
https://github.com/AOMediaCodec/av1-avif/tree/master/testFiles

