noinst_LTLIBRARIES = libgb.la

libgb_la_SOURCES = common.c utils.c

noinst_HEADERS = common.h utils.h

libgb_la_CFLAGS = -DDATADIR=\"$(localstatedir)\" -I$(top_builddir)/rpc/rpcl

libgb_ladir = $(includedir)/gluster-block/utils

DISTCLEANFILES = Makefile.in

CLEANFILES = *~
