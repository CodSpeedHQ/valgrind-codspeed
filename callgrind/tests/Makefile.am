
include $(top_srcdir)/Makefile.tool-tests.am

SUBDIRS = .
DIST_SUBDIRS = .

dist_noinst_SCRIPTS = filter_stderr filter_inline

EXTRA_DIST = \
	ann1.post.exp ann1.stderr.exp ann1.vgtest \
	ann2.post.exp ann2.stderr.exp ann2.vgtest \
	clreq.vgtest clreq.stderr.exp \
	bug497723.stderr.exp bug497723.post.exp bug497723.vgtest \
	simwork1.vgtest simwork1.stdout.exp simwork1.stderr.exp \
	simwork2.vgtest simwork2.stdout.exp simwork2.stderr.exp \
	simwork3.vgtest simwork3.stdout.exp simwork3.stderr.exp \
	simwork-both.vgtest simwork-both.stdout.exp simwork-both.stderr.exp \
	simwork-branch.vgtest simwork-branch.stdout.exp simwork-branch.stderr.exp \
	simwork-cache.vgtest simwork-cache.stdout.exp simwork-cache.stderr.exp \
	notpower2.vgtest notpower2.stderr.exp \
	notpower2-wb.vgtest notpower2-wb.stderr.exp \
	notpower2-hwpref.vgtest notpower2-hwpref.stderr.exp \
	notpower2-use.vgtest notpower2-use.stderr.exp \
	threads.vgtest threads.stderr.exp \
	threads-use.vgtest threads-use.stderr.exp \
	find-source.vgtest find-source.stderr.exp find-source.post.exp \
	inline-samefile.vgtest inline-samefile.stderr.exp inline-samefile.stdout.exp inline-samefile.post.exp \
	inline-crossfile.vgtest inline-crossfile.stderr.exp inline-crossfile.stdout.exp inline-crossfile.post.exp \
	inline-crossfile-helper1.h inline-crossfile-helper2.h filter_inline

check_PROGRAMS = clreq simwork threads inline-samefile inline-crossfile

AM_CFLAGS   += $(AM_FLAG_M3264_PRI)
AM_CXXFLAGS += $(AM_FLAG_M3264_PRI)

# Inline tests need -O2 to enable inlining and -g for debug info
inline_samefile_CFLAGS = $(AM_CFLAGS) -O2 -g
inline_crossfile_CFLAGS = $(AM_CFLAGS) -O2 -g

threads_LDADD = -lpthread
