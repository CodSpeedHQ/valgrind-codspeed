# codspeed_capstone.m4 -- CodSpeed additions to Valgrind's configure.

# CODSPEED_CAPSTONE
# -----------------
# Export CAPSTONE_CFLAGS / CAPSTONE_LIBS for callgrind/Makefile.am. The vendored
# third_party/capstone submodule is compiled by third_party/Makefile.am;
# --with-capstone=PATH (or CAPSTONE_DIR) selects a prebuilt install instead.
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
   # libcapstone.a is not tested for: it does not exist yet at configure time.
   if test ! -f "$srcdir/third_party/capstone/cs.c"; then
      AC_MSG_ERROR([third_party/capstone is empty. Run:
   git submodule update --init third_party/capstone
or pass --with-capstone=PATH to use a prebuilt Capstone.])
   fi
   CAPSTONE_INCLUDES='-I$(top_srcdir)/third_party/capstone/include'
   CAPSTONE_LIBS='$(top_builddir)/third_party/libcapstone.a'
   AC_MSG_NOTICE([Callgrind cycle estimation enabled with the vendored Capstone])
else
   capstone_lib=
   for d in lib lib64 "lib/$host_cpu-linux-gnu"; do
      if test -f "$capstone_dir/$d/libcapstone.a"; then
         capstone_lib="$capstone_dir/$d/libcapstone.a"
         break
      fi
   done
   if test -z "$capstone_lib" -o ! -f "$capstone_dir/include/capstone/capstone.h"; then
      AC_MSG_ERROR([--with-capstone=$capstone_dir: libcapstone.a or capstone.h not found])
   fi
   CAPSTONE_INCLUDES="-I$capstone_dir/include"
   CAPSTONE_LIBS="$capstone_lib"
   AC_MSG_NOTICE([Callgrind cycle estimation enabled with Capstone at $capstone_dir])
fi

# Fortify off: the tool links -nodefaultlibs, so glibc's __*_chk wrappers are
# unavailable and our libc shims must be definitions, not macro-expansions.
CAPSTONE_CFLAGS="-DCLG_WITH_CAPSTONE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 $CAPSTONE_INCLUDES"
AC_SUBST([CAPSTONE_CFLAGS])
AC_SUBST([CAPSTONE_LIBS])
])

# CODSPEED_C_STD_GNU17
# --------------------
# Export CODSPEED_C_STD, the dialect the Callgrind tool is compiled with. Under
# C23 -- what AC_PROG_CC picks on GCC 15+ -- glibc 2.42+ defines strchr/strrchr/
# strstr as _Generic macros, which clash with the definitions in
# cycledecode_capstone.c. It stays out of CFLAGS so the rest of the tree, the
# test programs included, keeps the compiler's own default.
#
# Must be called after AC_PROG_CC.
AC_DEFUN([CODSPEED_C_STD_GNU17], [
AC_MSG_CHECKING([whether $CC accepts -std=gnu17])
codspeed_save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -std=gnu17"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])],
   [AC_MSG_RESULT([yes])
    CODSPEED_C_STD="-std=gnu17"],
   [AC_MSG_RESULT([no])
    CODSPEED_C_STD=])
CFLAGS="$codspeed_save_CFLAGS"
AC_SUBST([CODSPEED_C_STD])
])
