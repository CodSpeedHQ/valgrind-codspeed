include $(top_srcdir)/Makefile.all.am

# CodSpeed: the vendored Capstone decoder used by Callgrind's per-instruction
# cycle estimation, built as a convenience library so that a plain `make`
# produces everything Valgrind needs. Skipped when configure was given
# --with-capstone=PATH (or CAPSTONE_DIR), which points at a prebuilt Capstone.
if BUILD_VENDORED_CAPSTONE

noinst_LIBRARIES = libcapstone.a

# The submodule sources are not compiled where they live. Automake is
# configured with subdir-objects, so it writes an object next to its source --
# and in an in-tree build (the only kind Valgrind supports) that is inside the
# capstone checkout, which git would then permanently report as dirty. The .o
# files are covered by Capstone's own .gitignore, but the .deps directories and
# .dirstamp files automake drops beside them are not, and that .gitignore
# belongs to upstream Capstone, not to us.
#
# So each unit below is a one-line stub that #includes the matching
# submodule source, and every build artefact lands in capstone-stubs/, which
# belongs to this repository and is gitignored here. Capstone's own quoted
# #includes still resolve against its own directories, since a quoted include
# is looked up relative to the file that holds the directive.
#
# Only x86 and arm64 are compiled in. This is not just about size: the
# instruction printers for the other targets reference libc symbols that a
# Valgrind tool does not shim, so linking them breaks the tool.
libcapstone_a_SOURCES = \
	capstone-stubs/cs.c \
	capstone-stubs/MCInst.c \
	capstone-stubs/MCInstrDesc.c \
	capstone-stubs/MCRegisterInfo.c \
	capstone-stubs/Mapping.c \
	capstone-stubs/SStream.c \
	capstone-stubs/utils.c \
	capstone-stubs/X86ATTInstPrinter.c \
	capstone-stubs/X86Disassembler.c \
	capstone-stubs/X86DisassemblerDecoder.c \
	capstone-stubs/X86InstPrinterCommon.c \
	capstone-stubs/X86IntelInstPrinter.c \
	capstone-stubs/X86Mapping.c \
	capstone-stubs/X86Module.c \
	capstone-stubs/AArch64BaseInfo.c \
	capstone-stubs/AArch64Disassembler.c \
	capstone-stubs/AArch64InstPrinter.c \
	capstone-stubs/AArch64Mapping.c \
	capstone-stubs/AArch64Module.c

# The same preprocessor state Capstone's own CMake build produces for
# -DCAPSTONE_ARCHITECTURE_DEFAULT=OFF -DCAPSTONE_X86_SUPPORT=ON
# -DCAPSTONE_ARM64_SUPPORT=ON: the arch loop emits both CAPSTONE_<arch>_SUPPORT
# and CAPSTONE_HAS_<arch>, and CAPSTONE_USE_DEFAULT_ALLOC defaults to ON.
# Callgrind replaces the allocator at runtime via cs_option(CS_OPT_MEM).
libcapstone_a_CPPFLAGS = \
	-I$(srcdir)/capstone/include \
	-DCAPSTONE_X86_SUPPORT \
	-DCAPSTONE_HAS_X86 \
	-DCAPSTONE_ARM64_SUPPORT \
	-DCAPSTONE_HAS_ARM64 \
	-DCAPSTONE_USE_SYS_DYN_MEM

# Capstone is linked into the primary (native) Callgrind tool, so it must be
# compiled with that platform's flags. AM_CFLAGS_BASE already carries
# -fno-stack-protector (a canary read faults in a tool, which runs without
# glibc's TLS) and the right word size; fortify is turned off here for the
# same reason as in CAPSTONE_CFLAGS. -w because this is third-party code whose
# warnings are not ours to fix.
#
# -DNDEBUG matches what Capstone's own CMake Release build sets, and is load
# bearing: assert() would pull in __assert_fail, which a tool cannot resolve.
libcapstone_a_CFLAGS = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_PRI_CAPS@) \
	-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
	-DNDEBUG \
	-w

endif

# Ship the decoder in `make dist` tarballs: they are unpacked outside a git
# checkout, where the submodule cannot be initialised. Only the stubs above
# are _SOURCES, so everything they pull in has to be named here; listing the
# two arch directories also picks up the .inc tables those sources #include.
# Only the compiled-in architectures are shipped, so this stays at ~18M of the
# submodule's 76M.
EXTRA_DIST = \
	capstone/cs.c \
	capstone/MCInst.c \
	capstone/MCInstrDesc.c \
	capstone/MCRegisterInfo.c \
	capstone/Mapping.c \
	capstone/SStream.c \
	capstone/utils.c \
	capstone/include \
	capstone/arch/X86 \
	capstone/arch/AArch64 \
	capstone/LEB128.h \
	capstone/MCDisassembler.h \
	capstone/MCFixedLenDisassembler.h \
	capstone/MCInst.h \
	capstone/MCInstrDesc.h \
	capstone/MCRegisterInfo.h \
	capstone/Mapping.h \
	capstone/MathExtras.h \
	capstone/SStream.h \
	capstone/cs_priv.h \
	capstone/cs_simple_types.h \
	capstone/utils.h \
	capstone/LICENSE.TXT \
	capstone/LICENSE_LLVM.TXT
