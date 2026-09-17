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
    # CodSpeed: check out the Capstone decoder that Callgrind's cycle estimation
    # links against, unless a prebuilt one was provided or it is already there.
    if test -z "$CAPSTONE_DIR" && test ! -f third_party/capstone/cs.c ; then
	run git submodule update --init third_party/capstone
    fi
else
    echo "skipping: git configuration"
fi
