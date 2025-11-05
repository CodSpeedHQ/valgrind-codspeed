# Builds a specific valgrind version
# Usage:
# - just build 3.24.0: Downloads the specified version from sourceware.org, builds and installs it
# - just build local: Builds the local source tree
build version:
    #!/usr/bin/env bash
    set -euo pipefail

    mkdir -p /tmp/valgrind-build
    rm -rf /tmp/valgrind-build/valgrind-{{ version }}*

    if [ "{{ version }}" = "local" ]; then
        cp -r . /tmp/valgrind-build/valgrind-local
    else
        wget -q -O /tmp/valgrind-build/valgrind-{{ version }}.tar.bz2 \
            https://sourceware.org/pub/valgrind/valgrind-{{ version }}.tar.bz2
        tar -xjf /tmp/valgrind-build/valgrind-{{ version }}.tar.bz2 \
            -C /tmp/valgrind-build
    fi

    just build-in "/tmp/valgrind-build/valgrind-{{ version }}"

build-in dir:
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{ dir }}"

    # Check if we need to run autogen.sh (for git checkouts)
    if [ -f "autogen.sh" ] && [ ! -f "configure" ]; then
        ./autogen.sh
    fi

    ./configure
    make include/vgversion.h
    make -j$(nproc) -C VEX
    make -j$(nproc) -C coregrind
    make -j$(nproc) -C callgrind


install version:
    #!/usr/bin/env bash
    set -euo pipefail

    cd "/tmp/valgrind-build/valgrind-{{ version }}"
    sudo make install

