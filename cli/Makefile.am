sbin_PROGRAMS = gluster-block

gluster_block_SOURCES = gluster-block.c

gluster_block_LDADD = $(TIRPC_LIBS)                                  \
											$(top_builddir)/rpc/libgbrpc.la                \
											$(top_builddir)/utils/libgb.la

gluster_block_CFLAGS = $(TIRPC_CFLAGS) -DDATADIR=\"$(localstatedir)\" \
											 -I$(top_srcdir)/ -I$(top_srcdir)/utils/			  \
											 -I$(top_srcdir)/rpc -I$(top_builddir)/rpc/rpcl

DISTCLEANFILES = Makefile.in

CLEANFILES = *~
