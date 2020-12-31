#!/bin/bash

if ! [ -x "$(command -v meson)" ]; then
  echo 'Error: meson is not installed.' >&2
  exit 1
fi

if ! [ -x "$(command -v ninja)" ]; then
  echo 'Error: ninja is not installed.' >&2
  exit 1
fi

RELATIVE_PATH=`dirname "$BASH_SOURCE"`
cd "$RELATIVE_PATH"

if ! [ -f builddir/src/file-avif ]; then
  #we clean previous incomplete build
  if [ -d builddir ]; then
    rm -r builddir
  fi

  meson setup --buildtype=release builddir
  cd builddir
  ninja

  if ! [ -f src/file-avif ]; then
    echo 'Error: we failed to compile file-avif!' >&2
    exit 1
  fi

  cd ..
fi

cp builddir/src/file-avif .

echo 'file-avif build sucess.'
echo 'Info how to install the plug-in for local users of GIMP 2.99:'
echo '  mkdir ~/.config/GIMP/2.99/plug-ins/file-avif'
echo '  cp file-avif ~/.config/GIMP/2.99/plug-ins/file-avif/'
echo 'Info how to install the plug-in for all users (needs root permission) of GIMP 2.99 on local machine:'
echo '  mkdir /usr/lib64/gimp/2.99/plug-ins/file-avif'
echo '  cp file-avif /usr/lib64/gimp/2.99/plug-ins/file-avif/'

exit 0
