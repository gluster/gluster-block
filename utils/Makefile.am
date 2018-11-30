noinst_LTLIBRARIES = libgb.la

libgb_la_SOURCES = common.c utils.c lru.c capabilities.c dyn-config.c

noinst_HEADERS = common.h utils.h lru.h list.h capabilities.h

libgb_la_CFLAGS = $(GFAPI_CFLAGS) $(TIRPC_CFLAGS)                              \
                  -DDATADIR=\"$(localstatedir)\"                               \
                  -DCONFDIR=\"$(GLUSTER_BLOCKD_WORKDIR)\"                      \
                  -I$(top_builddir)/ -I$(top_builddir)/rpc/rpcl

libgb_la_LIBADD = $(GFAPI_LIBS) $(TIRPC_LIBS) $(PTHREAD)

libgb_la_LDFLAGS = -lpthread

libgb_ladir = $(includedir)/gluster-block/utils

EXTRA_DIST = gluster-block-caps.info

DISTCLEANFILES = Makefile.in

CLEANFILES = *~

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(GLUSTER_BLOCKD_WORKDIR);                              \
	$(INSTALL_DATA) gluster-block-caps.info                                      \
		$(DESTDIR)$(GLUSTER_BLOCKD_WORKDIR)/gluster-block-caps.info;

uninstall-local:
	rm -f $(DESTDIR)$(GLUSTER_BLOCKD_WORKDIR)/gluster-block-caps.info
