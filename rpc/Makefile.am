SUBDIRS = rpcl

noinst_LTLIBRARIES = libgbrpc.la

libgbrpc_la_SOURCES = block_svc_routines.c block_info.c block_list.c           \
                      block_create.c block_delete.c block_modify.c             \
                      block_replace.c block_version.c block_genconfig.c        \
                      block_reload.c block_common.h glfs-operations.c

noinst_HEADERS = glfs-operations.h

libgbrpc_la_CFLAGS = $(GFAPI_CFLAGS) $(JSONC_CFLAGS) $(TIRPC_CFLAGS)    \
                     -DDATADIR=\"$(localstatedir)\" -I$(top_builddir)/  \
                     -I$(top_srcdir)/utils/ -I$(top_builddir)/rpc/rpcl

libgbrpc_la_LIBADD = $(GFAPI_LIBS) $(JSONC_LIBS) $(UUID) $(TIRPC_LIBS) \
                     rpcl/libgbrpcxdr.la

libgbrpc_ladir = $(includedir)/gluster-block/rpc

DISTCLEANFILES = Makefile.in

CLEANFILES = *~
