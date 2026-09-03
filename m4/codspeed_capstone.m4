# codspeed_capstone.m4 -- CodSpeed additions to Valgrind's configure.

# CODSPEED_CAPSTONE
# -----------------
# Locate the Capstone decoder that Callgrind's per-instruction cycle
# estimation (--cycle-estimation=yes) links against, and export
# CAPSTONE_CFLAGS / CAPSTONE_LIBS for callgrind/Makefile.am.
#
# By default the vendored submodule in third_party/capstone is compiled by
# third_party/Makefile.am as part of `make`, so a plain
# `./autogen.sh && ./configure && make && make install` is all that is needed.
# --with-capstone=PATH (or the CAPSTONE_DIR environment variable, which `nix
# develop` sets) selects a prebuilt Capstone install instead.
AC_DEFUN([CODSPEED_CAPSTONE], [
AC_ARG_WITH([capstone],
   [AS_HELP_STRING([--with-capstone=PATH],
      [use a prebuilt Capstone install for Callgrind cycle estimation instead
       of the vendored third_party/capstone submodule. Defaults to the
       CAPSTONE_DIR environment variable])],
   [capstone_dir="$withval"],
   [capstone_dir="$CAPSTONE_DIR"])

AM_CONDITIONAL([BUILD_VENDORED_CAPSTONE], [test -z "$capstone_dir"])

if test -z "$capstone_dir"; then
   # Built by third_party/Makefile.am. Note there is deliberately no check
   # for libcapstone.a here: it does not exist yet at configure time.
   if test ! -f "$srcdir/third_party/capstone/cs.c"; then
      AC_MSG_ERROR([third_party/capstone is empty. Run:
   git submodule update --init third_party/capstone
or pass --with-capstone=PATH to use a prebuilt Capstone.])
   fi
   CAPSTONE_INCLUDES='-I$(top_srcdir)/third_party/capstone/include'
   CAPSTONE_LIBS='$(top_builddir)/third_party/libcapstone.a'
   AC_MSG_NOTICE([Callgrind cycle estimation enabled with the vendored Capstone])
else
   if test ! -f "$capstone_dir/lib/libcapstone.a" \
        -o ! -f "$capstone_dir/include/capstone/capstone.h"; then
      AC_MSG_ERROR([--with-capstone=$capstone_dir: libcapstone.a or capstone.h not found])
   fi
   CAPSTONE_INCLUDES="-I$capstone_dir/include"
   CAPSTONE_LIBS="$capstone_dir/lib/libcapstone.a"
   AC_MSG_NOTICE([Callgrind cycle estimation enabled with Capstone at $capstone_dir])
fi

# Fortify off: the tool links -nodefaultlibs, so glibc's __*_chk fortify
# wrappers are unavailable, and our libc shims (sprintf/snprintf/...) must
# be real definitions, not fortify macro-expansions.
CAPSTONE_CFLAGS="-DCLG_WITH_CAPSTONE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 $CAPSTONE_INCLUDES"
AC_SUBST([CAPSTONE_CFLAGS])
AC_SUBST([CAPSTONE_LIBS])
])

# CODSPEED_C_STD_GNU17
# --------------------
# Pin the C dialect to gnu17.
#
# AC_PROG_CC under Autoconf 2.70+ selects the newest dialect the compiler
# supports, which is -std=gnu23 on GCC 15+. Under C23, glibc 2.42+ defines
# strchr/strrchr/strstr as _Generic macros, which clash with Callgrind's own
# definitions of those symbols. Appending to CFLAGS is enough to win: Autoconf
# puts its own -std= into $CC, and CFLAGS comes later on the command line.
#
# Must be called after AC_PROG_CC.
AC_DEFUN([CODSPEED_C_STD_GNU17], [
AC_MSG_CHECKING([whether $CC accepts -std=gnu17])
codspeed_save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -std=gnu17"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])],
   [AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])
    CFLAGS="$codspeed_save_CFLAGS"])
])
