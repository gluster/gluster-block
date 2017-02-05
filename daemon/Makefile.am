sbin_PROGRAMS = gluster-blockd

gluster_blockd_SOURCES = gluster-blockd.c

gluster_blockd_CFLAGS = -I$(top_srcdir)/utils/ -I$(top_srcdir)/rpc

gluster_blockd_LDADD = $(top_srcdir)/rpc/libgbxdr.la $(top_srcdir)/utils/libgb.la

DISTCLEANFILES = Makefile.in

CLEANFILES = *~

