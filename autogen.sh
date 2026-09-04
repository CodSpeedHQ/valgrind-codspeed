#!/bin/sh

run ()
{
    echo "running: $*"
    eval $*

    if test $? != 0 ; then
	echo "error: while running '$*'"
	exit 1
    fi
}

run aclocal -I m4
run autoheader
run automake -a
run autoconf

# Valgrind-specific Git configuration, if appropriate.
if git rev-parse --is-inside-work-tree > /dev/null 2>&1 ; then
    echo "running: git configuration"
    git config blame.ignoreRevsFile .git-blame-ignore-revs
    # CodSpeed: the vendored Capstone decoder that Callgrind's cycle estimation
    # links against. A clone without --recurse-submodules leaves it empty.
    # Not fatal: a build using --with-capstone=PATH (or CAPSTONE_DIR) needs no
    # submodule, and must still work in a sandbox with no network. configure
    # reports the empty submodule if neither is available.
    echo "running: git submodule update --init third_party/capstone"
    if ! git submodule update --init third_party/capstone ; then
	echo "warning: could not check out third_party/capstone."
	echo "warning: pass --with-capstone=PATH to configure to use a prebuilt Capstone."
    fi
else
    echo "skipping: git configuration"
fi
