libgbrpcxdr_la_SOURCES = block_clnt.c block_xdr.c block_svc.c
libgbrpcxdr_la_CFLAGS = $(TIRPC_CFLAGS)
libgbrpcxdr_la_LDFLAGS = $(TIRPC_LIBS)

noinst_LTLIBRARIES = libgbrpcxdr.la
noinst_HEADERS = block.h block_svc.h rpc-pragmas.h

EXTRA_DIST = block.x
BUILT_SOURCES = block.h block_clnt.c block_svc.c block_xdr.c

DISTCLEANFILES = Makefile.in $(BUILT_SOURCES)
CLEANFILES = *~ $(BUILT_SOURCES)

block.h: block.x
	rpcgen -hM -o $(top_builddir)/rpc/rpcl/$@ $^
	$(SED) -i '0,/#endif/s//#endif\nextern struct timeval TIMEOUT;/' \
	          $(top_builddir)/rpc/rpcl/$@

block_xdr.c: block.x
	rpcgen -cM -o $(top_builddir)/rpc/rpcl/$@ $^

block_clnt.c: block.x
	rpcgen -lM -o $(top_builddir)/rpc/rpcl/$@ $^
	$(SED) -i '/TIMEOUT = { 25, 0 }/d' $(top_builddir)/rpc/rpcl/$@

block_svc.c: block.x
	rpcgen -mM -o $(top_builddir)/rpc/rpcl/$@ $^

dist-hook:
	find $(distdir) -type f \
		-not -name 'block.x' -not -name 'rpc-pragmas.h' \
		-not -name 'block_svc.h' -not -name 'Makefile.*' -delete
