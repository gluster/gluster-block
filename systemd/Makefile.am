DISTCLEANFILES = Makefile.in gluster-blockd.service gluster-block-target.service

CLEANFILES = *~ gluster-blockd.service gluster-block-target.service

EXTRA_DIST = gluster-blockd.service.in gluster-block-target.service.in   \
             gluster-blockd.sysconfig

if USE_SYSTEMD
# systemddir is already defined through configure.ac
systemd_DATA = gluster-blockd.service gluster-block-target.service
endif

install-data-local:
	$(MKDIR_P) $(DESTDIR)${sysconfigdir};              \
	$(INSTALL_DATA) gluster-blockd.sysconfig           \
		$(DESTDIR)${sysconfigdir}/gluster-blockd;

uninstall-local:
	rm -f $(DESTDIR)${sysconfigdir}/gluster-blockd
