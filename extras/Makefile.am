EXTRA_DIST = replace-node.sh wait-for-bricks.sh

DISTCLEANFILES = Makefile.in

CLEANFILES = *~

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR);                           \
	$(INSTALL_DATA) -m 755 $(top_srcdir)/extras/wait-for-bricks.sh               \
		$(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR)/wait-for-bricks.sh

uninstall-local:
	rm -f $(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR)/wait-for-bricks.sh
