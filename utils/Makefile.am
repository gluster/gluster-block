noinst_LTLIBRARIES = libgb.la

libgb_la_SOURCES = common.c utils.c lru.c capabilities.c

noinst_HEADERS = common.h utils.h lru.h list.h capabilities.h

libgb_la_CFLAGS = $(GFAPI_CFLAGS)                                              \
                  -DDATADIR=\"$(localstatedir)\" -DCONFDIR=\"$(sysconfigdir)\" \
                  -I$(top_builddir)/ -I$(top_builddir)/rpc/rpcl

libgb_la_LIBADD = $(GFAPI_LIBS)

libgb_ladir = $(includedir)/gluster-block/utils

EXTRA_DIST = gluster-block-caps.info

DISTCLEANFILES = Makefile.in

CLEANFILES = *~

install-data-local:
	$(MKDIR_P) $(DESTDIR)${sysconfigdir};                    \
	$(INSTALL_DATA) gluster-block-caps.info                  \
		$(DESTDIR)${sysconfigdir}/gluster-block-caps.info;

uninstall-local:
	rm -f $(DESTDIR)${sysconfigdir}/gluster-block-caps.info
