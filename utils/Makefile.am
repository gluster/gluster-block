noinst_LTLIBRARIES = libgb.la

libgb_la_SOURCES = common.c utils.c lru.c

noinst_HEADERS = common.h utils.h lru.h list.h

libgb_la_CFLAGS = $(GFAPI_CFLAGS) -DDATADIR=\"$(localstatedir)\"          \
                  -I$(top_builddir)/ -I$(top_builddir)/rpc/rpcl

libgb_la_LIBADD = $(GFAPI_LIBS)

libgb_ladir = $(includedir)/gluster-block/utils

DISTCLEANFILES = Makefile.in

CLEANFILES = *~
