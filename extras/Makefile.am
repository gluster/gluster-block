EXTRA_DIST = replace-node.sh wait-for-bricks.sh upgrade_activities.sh            \
			 gluster-block.logrotate

DISTCLEANFILES = Makefile.in

CLEANFILES = *~

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR);                           \
	$(INSTALL_DATA) -m 755 $(top_srcdir)/extras/wait-for-bricks.sh               \
		$(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR)/wait-for-bricks.sh;               \
	$(INSTALL_DATA) -m 755 $(top_srcdir)/extras/upgrade_activities.sh            \
		$(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR)/upgrade_activities.sh;            \
	$(MKDIR_P) $(DESTDIR)$(GLUSTER_BLOCKD_LOGROTATEDIR);                         \
	$(INSTALL_DATA) gluster-block.logrotate                               \
		$(DESTDIR)$(GLUSTER_BLOCKD_LOGROTATEDIR)/gluster-block;

uninstall-local:
	rm -f $(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR)/wait-for-bricks.sh              \
		$(DESTDIR)$(GLUSTER_BLOCKD_LIBEXECDIR)/upgrade_activities.sh             \
		$(DESTDIR)$(GLUSTER_BLOCKD_WORKDIR)/gb_upgrade.status                    \
		$(DESTDIR)$(GLUSTER_BLOCKD_LOGROTATEDIR)/gluster-block;
