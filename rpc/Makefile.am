SUBDIRS = rpcl

noinst_LTLIBRARIES = libgbrpc.la

libgbrpc_la_SOURCES = block_svc_routines.c glfs-operations.c

noinst_HEADERS = glfs-operations.h

libgbrpc_la_CFLAGS = $(GFAPI_CFLAGS) $(JSONC_CFLAGS) \
        -DDATADIR=\"$(localstatedir)\"  -include ${top_srcdir}/config.h \
	-I$(top_srcdir)/utils/ -I$(top_builddir)/rpc/rpcl

libgbrpc_la_LIBADD = $(GFAPI_LIBS) $(JSONC_LIBS) $(UUID) rpcl/libgbrpcxdr.la

libgbrpc_ladir = $(includedir)/gluster-block/rpc

DISTCLEANFILES = Makefile.in

CLEANFILES = *~
