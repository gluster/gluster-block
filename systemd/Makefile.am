DISTCLEANFILES = Makefile.in gluster-blockd.service gluster-block-target.service \
                 gluster-blockd.initd

CLEANFILES = *~ gluster-blockd.service gluster-block-target.service \
             gluster-blockd.initd

EXTRA_DIST = gluster-blockd.service.in gluster-block-target.service.in \
             gluster-blockd.initd.in gluster-blockd.sysconfig

if USE_SYSTEMD
# systemddir is already defined through configure.ac
systemd_DATA = gluster-blockd.service gluster-block-target.service
else
# initddir is already defined through configure.ac
initd_DATA = gluster-blockd.initd
endif

install-data-local:
	$(MKDIR_P) $(DESTDIR)${sysconfigdir};              \
	$(INSTALL_DATA) gluster-blockd.sysconfig           \
		$(DESTDIR)${sysconfigdir}/gluster-blockd;

uninstall-local:
	rm -f $(DESTDIR)${sysconfigdir}/gluster-blockd
