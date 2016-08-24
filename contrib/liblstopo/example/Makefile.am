# Copyright © 2017 Inria.  All rights reserved.
#
# See COPYING in top-level directory.

# This makefile is only reached when building in standalone mode

AM_CFLAGS = $(HWLOC_CFLAGS)
AM_CPPFLAGS = $(HWLOC_CPPFLAGS) -I$(top_srcdir)/utils/hwloc -I$(top_srcdir)/utils/lstopo
AM_LDFLAGS = $(HWLOC_LDFLAGS)

check_PROGRAMS =

if HWLOC_HAVE_CAIRO_XLIB_SURFACE
check_PROGRAMS += example
example_SOURCES = example.c
example_LDADD = $(HWLOC_top_builddir)/hwloc/libhwloc.la $(HWLOC_top_builddir)/utils/lstopo/liblstopo-base.la $(HWLOC_top_builddir)/utils/lstopo/liblstopo-cairo.la
endif

