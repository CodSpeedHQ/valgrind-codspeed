include $(top_srcdir)/Makefile.tool.am

EXTRA_DIST =

#----------------------------------------------------------------------------
# Headers, etc
#----------------------------------------------------------------------------

pkginclude_HEADERS = tracegrind.h

noinst_HEADERS = \
	costs.h \
	events.h \
	global.h

#----------------------------------------------------------------------------
# tracegrind-<platform>
#----------------------------------------------------------------------------

noinst_PROGRAMS  = tracegrind-@VGCONF_ARCH_PRI@-@VGCONF_OS@
if VGCONF_HAVE_PLATFORM_SEC
noinst_PROGRAMS += tracegrind-@VGCONF_ARCH_SEC@-@VGCONF_OS@
endif

TRACEGRIND_SOURCES_COMMON = \
	bb.c \
	bbcc.c \
	callstack.c \
	clo.c \
	context.c \
	costs.c \
	debug.c \
	dump.c \
	events.c \
	fn.c \
	jumps.c \
	main.c \
	sim.c \
	threads.c

# We sneakily include "cg_branchpred.c" and "cg_arch.c" from cachegrind
TRACEGRIND_CFLAGS_COMMON = -I$(top_srcdir)/cachegrind

tracegrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_SOURCES      = \
	$(TRACEGRIND_SOURCES_COMMON)
tracegrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
tracegrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS       = $(LTO_CFLAGS) \
	$(AM_CFLAGS_@VGCONF_PLATFORM_PRI_CAPS@) $(TRACEGRIND_CFLAGS_COMMON)
tracegrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_PRI_CAPS@)
tracegrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_PRI_CAPS@)
tracegrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
tracegrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LINK = \
	$(top_builddir)/coregrind/link_tool_exe_@VGCONF_OS@ \
	@VALT_LOAD_ADDRESS_PRI@ \
	$(LINK) \
	$(tracegrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS) \
	$(tracegrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS)

if VGCONF_HAVE_PLATFORM_SEC
tracegrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_SOURCES      = \
	$(TRACEGRIND_SOURCES_COMMON)
tracegrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
tracegrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS       = $(LTO_CFLAGS) \
	$(AM_CFLAGS_@VGCONF_PLATFORM_SEC_CAPS@) $(TRACEGRIND_CFLAGS_COMMON)
tracegrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_SEC_CAPS@)
tracegrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_SEC_CAPS@)
tracegrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
tracegrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LINK = \
	$(top_builddir)/coregrind/link_tool_exe_@VGCONF_OS@ \
	@VALT_LOAD_ADDRESS_SEC@ \
	$(LINK) \
	$(tracegrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS) \
	$(tracegrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS)
endif
