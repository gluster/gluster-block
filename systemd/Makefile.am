DISTCLEANFILES = Makefile.in gluster-blockd.service

CLEANFILES = *~ gluster-blockd.service

EXTRA_DIST = gluster-blockd.service.in gluster-blockd.sysconfig

if USE_SYSTEMD
# systemddir is already defined through configure.ac
systemd_DATA = gluster-blockd.service
endif

install-data-local:
	$(MKDIR_P) $(DESTDIR)${sysconfigdir};              \
	$(INSTALL_DATA) gluster-blockd.sysconfig           \
		$(DESTDIR)${sysconfigdir}/gluster-blockd;

uninstall-local:
	rm -f $(DESTDIR)${sysconfigdir}/gluster-blockd
