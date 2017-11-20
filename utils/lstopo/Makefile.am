# Copyright © 2009-2018 Inria.  All rights reserved.
# Copyright © 2009-2012, 2014 Université Bordeaux
# Copyright © 2009-2014 Cisco Systems, Inc.  All rights reserved.
#
# See COPYING in top-level directory.

# This makefile is only reached when building in standalone mode

AM_CFLAGS = $(HWLOC_CFLAGS)
# Add -I of utils/hwloc for misc.h
AM_CPPFLAGS = $(HWLOC_CPPFLAGS) -I$(top_srcdir)/utils/hwloc
AM_LDFLAGS = $(HWLOC_LDFLAGS)

bin_PROGRAMS = \
        lstopo-no-graphics

EXTRA_DIST = \
        test-lstopo.output

noinst_LTLIBRARIES = liblstopo-base.la
liblstopo_base_la_SOURCES = \
        lstopo.h \
        lstopo-core.c \
        lstopo-draw.c \
        lstopo-fig.c \
        lstopo-ascii.c \
        lstopo-text.c \
        lstopo-xml.c
liblstopo_base_la_LIBADD = $(HWLOC_top_builddir)/hwloc/libhwloc.la -lm $(HWLOC_TERMCAP_LIBS)

lstopo_no_graphics_SOURCES = lstopo.c
lstopo_no_graphics_LDADD = $(HWLOC_top_builddir)/hwloc/libhwloc.la liblstopo-base.la

if HWLOC_HAVE_CAIRO
noinst_LTLIBRARIES += liblstopo-cairo.la
liblstopo_cairo_la_SOURCES = lstopo-cairo.c
liblstopo_cairo_la_CFLAGS = $(AM_CFLAGS) $(HWLOC_CAIRO_CFLAGS)
liblstopo_cairo_la_CPPFLAGS = $(AM_CPPFLAGS) $(HWLOC_X11_CPPFLAGS)
liblstopo_cairo_la_LIBADD = $(HWLOC_CAIRO_LIBS) $(HWLOC_X11_LIBS) $(HWLOC_top_builddir)/hwloc/libhwloc.la liblstopo-base.la liblstopo-base.la

bin_PROGRAMS += lstopo
lstopo_SOURCES = lstopo.c
lstopo_CFLAGS = $(AM_CFLAGS) $(HWLOC_CAIRO_CFLAGS)
lstopo_CPPFLAGS = $(AM_CPPFLAGS) -DLSTOPO_HAVE_GRAPHICS
lstopo_LDADD = $(HWLOC_top_builddir)/hwloc/libhwloc.la liblstopo-base.la liblstopo-cairo.la
endif

if HWLOC_HAVE_WINDOWS
noinst_LTLIBRARIES += liblstopo-windows.la
liblstopo_windows_la_SOURCES = lstopo-windows.c

bin_PROGRAMS += lstopo lstopo-win
lstopo_SOURCES = lstopo.c
lstopo_CPPFLAGS = $(AM_CPPFLAGS) -DLSTOPO_HAVE_GRAPHICS
lstopo_LDADD = $(HWLOC_top_builddir)/hwloc/libhwloc.la liblstopo-base.la liblstopo-windows.la
lstopo_LDFLAGS = -mwindows
if HWLOC_HAVE_USER32
lstopo_LDADD += -luser32
endif
lstopo_win_SOURCES = $(lstopo_SOURCES)
lstopo_win_CPPFLAGS = $(lstopo_CPPFLAGS)
lstopo_win_CFLAGS = $(AM_CFLAGS)
lstopo_win_LDADD = $(lstopo_LDADD)
lstopo_win_LDFLAGS = -mwindows
endif

man1_pages = lstopo-no-graphics.1
EXTRA_DIST += $(man1_pages:.1=.1in)
nodist_man_MANS = $(man1_pages)

if !HWLOC_HAVE_MINGW32
TESTS = \
        test-lstopo.sh
endif !HWLOC_HAVE_MINGW32

if HWLOC_HAVE_CAIRO
# only installed when lstopo is built with Cairo/X11 support
# so that no terminal is required
APPLICATIONSdir = @datarootdir@/applications
dist_APPLICATIONS_DATA = lstopo.desktop
endif

SEDMAN = $(SED) -e 's/\#PACKAGE_NAME\#/@PACKAGE_NAME@/g' \
	  -e 's/\#PACKAGE_VERSION\#/@PACKAGE_VERSION@/g' \
	  -e 's/\#HWLOC_DATE\#/@HWLOC_RELEASE_DATE@/g'

.1in.1:
	@ echo Creating $@ man page...
	@ $(SEDMAN) \
	  > $@ < $<

install-exec-hook:
	rm -f $(DESTDIR)$(bindir)/hwloc-ls$(EXEEXT)
	cd $(DESTDIR)$(bindir) && $(LN_S) lstopo-no-graphics$(EXEEXT) hwloc-ls$(EXEEXT)
if !HWLOC_HAVE_WINDOWS
if !HWLOC_HAVE_CAIRO
	rm -f $(DESTDIR)$(bindir)/lstopo
	cd $(DESTDIR)$(bindir) && $(LN_S) lstopo-no-graphics$(EXEEXT) lstopo$(EXEEXT) || true
endif
endif

install-data-hook:
	rm -f $(DESTDIR)$(man1dir)/hwloc-ls.1
	cd $(DESTDIR)$(man1dir) && $(LN_S) lstopo-no-graphics.1 hwloc-ls.1
	rm -f $(DESTDIR)$(man1dir)/lstopo.1
	cd $(DESTDIR)$(man1dir) && $(LN_S) lstopo-no-graphics.1 lstopo.1

uninstall-local:
	rm -f $(DESTDIR)$(bindir)/hwloc-ls$(EXEEXT)
if !HWLOC_HAVE_WINDOWS
if !HWLOC_HAVE_CAIRO
	rm -f $(DESTDIR)$(bindir)/lstopo$(EXEEXT)
endif
endif
	rm -f $(DESTDIR)$(man1dir)/hwloc-ls.1 $(DESTDIR)$(man1dir)/lstopo.1

distclean-local:
	rm -f $(nodist_man_MANS)
