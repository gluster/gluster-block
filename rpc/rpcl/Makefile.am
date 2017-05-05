libgbrpcxdr_la_SOURCES = block_clnt.c block_xdr.c block_svc.c

noinst_LTLIBRARIES = libgbrpcxdr.la
noinst_HEADERS = block.h block_svc.h rpc-pragmas.h

EXTRA_DIST = block.x
BUILT_SOURCES = block.h block_clnt.c block_svc.c block_xdr.c

DISTCLEANFILES = Makefile.in $(BUILT_SOURCES)
CLEANFILES = *~ $(BUILT_SOURCES)

block.h: block.x
	rpcgen -h -o $(top_builddir)/rpc/rpcl/$@ $^

block_xdr.c: block.x
	rpcgen -c -o $(top_builddir)/rpc/rpcl/$@ $^

block_clnt.c: block.x
	rpcgen -l -o $(top_builddir)/rpc/rpcl/$@ $^
	$(SED) -i 's|TIMEOUT = { 25, 0 }|TIMEOUT = { 300, 0 }|' $(top_builddir)/rpc/rpcl/$@

block_svc.c: block.x
	rpcgen -m -o $(top_builddir)/rpc/rpcl/$@ $^
