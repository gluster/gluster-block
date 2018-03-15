sbin_PROGRAMS = gluster-blockd

gluster_blockd_SOURCES = gluster-blockd.c

gluster_blockd_CFLAGS = $(GFAPI_CFLAGS) $(TIRPC_CFLAGS)                        \
                        -DDATADIR=\"$(localstatedir)\"                         \
                        -I$(top_builddir)/ -I$(top_srcdir)/utils/              \
                        -I$(top_srcdir)/rpc -I$(top_builddir)/rpc/rpcl

gluster_blockd_LDADD = $(TIRPC_LIBS) $(PTHREAD)                                \
                       $(top_builddir)/rpc/libgbrpc.la                         \
                       $(top_builddir)/utils/libgb.la

DISTCLEANFILES = Makefile.in

CLEANFILES = *~

