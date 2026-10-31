#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cygpath -u "$AIPHONE_PROJECT_DIR")"
cache_dir="$(cygpath -u "$AIPHONE_BUILD_ROOT")"
source_dir="$cache_dir/pjproject-2.17"
prefix="$cache_dir/pj-install"
cd "$source_dir"

if ! cmp -s "$project_dir/dependencies/config_site.h" pjlib/include/pj/config_site.h ||
   [[ ! -f "$prefix/lib/pkgconfig/libpjproject.pc" ]]; then
    cp "$project_dir/dependencies/config_site.h" pjlib/include/pj/config_site.h
    ./configure --prefix="$prefix" \
        --disable-sound --disable-video --disable-upnp --disable-ssl \
        --disable-libsrtp --disable-speex-aec --disable-libwebrtc --disable-resample \
        --disable-l16-codec --disable-gsm-codec --disable-g722-codec \
        --disable-g7221-codec --disable-speex-codec --disable-ilbc-codec \
        --disable-opencore-amr --disable-silk --disable-opus --disable-bcg729 \
        --disable-lyra --disable-sdl --disable-ffmpeg --disable-v4l2 \
        --disable-openh264 --disable-vpx --disable-libyuv \
        CFLAGS='-O2 -DNDEBUG' CXXFLAGS='-O2 -DNDEBUG'
    make dep
    make -j6 lib
    make install
fi

pacman -Q > "$cache_dir/toolchain-packages.txt"
g++ --version | head -n 1
pkgconf --version
