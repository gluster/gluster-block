noinst_LTLIBRARIES = libgbxdr.la

libgbxdr_la_SOURCES = block_clnt.c block_xdr.c block_svc.c           \
											block_svc_routines.c glfs-operations.c

noinst_HEADERS = block.h glfs-operations.h

libgbxdr_la_CFLAGS = $(GFAPI_CFLAGS) -DDATADIR=\"$(localstatedir)\"  \
										 -I$(top_srcdir)/utils/

libgbxdr_la_LIBADD = $(GFAPI_LIBS) $(UUID)

libgbxdr_ladir = $(includedir)/gluster-block/rpc

DISTCLEANFILES = Makefile.in

CLEANFILES = *~
