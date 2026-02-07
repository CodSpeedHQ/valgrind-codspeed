
include $(top_srcdir)/Makefile.tool-tests.am

SUBDIRS = .
DIST_SUBDIRS = .

dist_noinst_SCRIPTS = filter_stderr filter_trace

check_PROGRAMS = \
	test_basic.bin \
	test_marker.bin \
	test_instr_toggle.bin \
	test_toggle_collect.bin \
	test_foo_bar_baz.bin \
	test_inline.bin \
	test_enter_inlined.bin \
	test_nested_inlined.bin

AM_CFLAGS   += $(AM_FLAG_M3264_PRI)
AM_CXXFLAGS += $(AM_FLAG_M3264_PRI)

test_basic_bin_SOURCES = test_basic.c
test_marker_bin_SOURCES = test_marker.c
test_instr_toggle_bin_SOURCES = test_instr_toggle.c
test_toggle_collect_bin_SOURCES = test_toggle_collect.c
test_foo_bar_baz_bin_SOURCES = test_foo_bar_baz.c
test_inline_bin_SOURCES = test_inline.c
test_inline_bin_CFLAGS = $(AM_CFLAGS) -O2 -g
test_enter_inlined_bin_SOURCES = test_enter_inlined.c
test_enter_inlined_bin_CFLAGS = $(AM_CFLAGS) -O2 -g
test_nested_inlined_bin_SOURCES = test_nested_inlined.c
test_nested_inlined_bin_CFLAGS = $(AM_CFLAGS) -O1 -g

EXTRA_DIST = \
	test_basic.vgtest test_basic.stderr.exp test_basic.post.exp \
	test_marker.vgtest test_marker.stderr.exp test_marker.post.exp \
	test_instr_toggle.vgtest test_instr_toggle.stderr.exp test_instr_toggle.post.exp \
	test_toggle_collect.vgtest test_toggle_collect.stderr.exp test_toggle_collect.post.exp \
	test_foo_bar_baz.vgtest test_foo_bar_baz.stderr.exp test_foo_bar_baz.post.exp \
	test_inline.vgtest test_inline.stderr.exp test_inline.post.exp \
	test_enter_inlined.vgtest test_enter_inlined.stderr.exp test_enter_inlined.post.exp \
	test_nested_inlined.vgtest test_nested_inlined.stderr.exp test_nested_inlined.post.exp \
	test_schema.vgtest test_schema.stderr.exp test_schema.post.exp
