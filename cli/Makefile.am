sbin_PROGRAMS = gluster-block

gluster_block_SOURCES = gluster-block.c

gluster_block_LDADD = $(top_srcdir)/rpc/libgbxdr.la                  \
											$(top_srcdir)/utils/libgb.la

gluster_block_CFLAGS = -DDATADIR=\"$(localstatedir)\"                \
											 -I$(top_srcdir)/ -I$(top_srcdir)/utils/			 \
											 -I$(top_srcdir)/rpc

DISTCLEANFILES = Makefile.in

CLEANFILES = *~
