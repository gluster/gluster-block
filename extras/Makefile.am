EXTRA_DIST = replace-node.sh wait-for-bricks.sh upgrade_activities.sh

DISTCLEANFILES = Makefile.in

CLEANFILES = *~

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR);                           \
	$(INSTALL_DATA) -m 755 $(top_srcdir)/extras/wait-for-bricks.sh               \
		$(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR)/wait-for-bricks.sh;                 \
	$(INSTALL_DATA) -m 755 $(top_srcdir)/extras/upgrade_activities.sh            \
		$(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR)/upgrade_activities.sh

uninstall-local:
	rm -f $(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR)/wait-for-bricks.sh              \
		$(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR)/upgrade_activities.sh
