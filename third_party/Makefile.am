include $(top_srcdir)/Makefile.all.am

# CodSpeed: the vendored Capstone decoder for Callgrind cycle estimation.
# Skipped when configure was given a prebuilt one (--with-capstone=PATH).
if BUILD_VENDORED_CAPSTONE

noinst_LIBRARIES = libcapstone.a

# Only x86 and arm64: the other targets' instruction printers reference libc
# symbols a Valgrind tool does not shim, so linking them breaks the tool.
capstone_sources = \
	cs.c \
	MCInst.c \
	MCInstrDesc.c \
	MCRegisterInfo.c \
	Mapping.c \
	SStream.c \
	utils.c \
	arch/X86/X86ATTInstPrinter.c \
	arch/X86/X86Disassembler.c \
	arch/X86/X86DisassemblerDecoder.c \
	arch/X86/X86InstPrinterCommon.c \
	arch/X86/X86IntelInstPrinter.c \
	arch/X86/X86Mapping.c \
	arch/X86/X86Module.c \
	arch/AArch64/AArch64BaseInfo.c \
	arch/AArch64/AArch64Disassembler.c \
	arch/AArch64/AArch64InstPrinter.c \
	arch/AArch64/AArch64Mapping.c \
	arch/AArch64/AArch64Module.c

# Compiled by the rules below rather than through _SOURCES: automake writes
# every object, .deps and .dirstamp next to its source, i.e. into the submodule
# checkout. These mirror the source tree under capstone-obj/ in the build dir.
capstone_objects = $(capstone_sources:%.c=capstone-obj/%.$(OBJEXT))

libcapstone_a_SOURCES =
libcapstone_a_LIBADD = $(capstone_objects)

# The preprocessor state of a CMake build with -DCAPSTONE_ARCHITECTURE_DEFAULT=OFF
# -DCAPSTONE_X86_SUPPORT=ON -DCAPSTONE_ARM64_SUPPORT=ON.
capstone_cppflags = \
	-I$(srcdir)/capstone/include \
	-DCAPSTONE_X86_SUPPORT \
	-DCAPSTONE_HAS_X86 \
	-DCAPSTONE_ARM64_SUPPORT \
	-DCAPSTONE_HAS_ARM64 \
	-DCAPSTONE_USE_SYS_DYN_MEM

# Capstone goes into the primary Callgrind tool, so it gets that platform's
# flags: AM_CFLAGS_BASE carries -fno-stack-protector (a canary read faults in a
# tool, which runs without glibc's TLS) and the right word size. -DNDEBUG is
# load bearing: assert() would pull in __assert_fail.
capstone_cflags = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_PRI_CAPS@) \
	-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
	-DNDEBUG \
	-w

capstone-obj/%.$(OBJEXT): $(srcdir)/capstone/%.c
	$(AM_V_at)$(MKDIR_P) $(@D)
	$(AM_V_CC)$(CC) $(capstone_cppflags) $(capstone_cflags) \
		-MMD -MP -MF $(@:.$(OBJEXT)=.d) -c -o $@ $<

-include $(capstone_objects:.$(OBJEXT)=.d)

clean-local:
	-rm -rf capstone-obj

endif

# `make dist` tarballs are unpacked outside a git checkout, so the sources above
# and everything they include are shipped explicitly. Only the compiled-in
# architectures, which keeps this at ~18M of the submodule's 76M.
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
