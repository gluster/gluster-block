DISTCLEANFILES = Makefile.in gluster-blockd.service

CLEANFILES = *~ gluster-blockd.service

EXTRA_DIST = gluster-blockd.service.in

if USE_SYSTEMD
# systemddir is already defined through configure.ac
systemd_DATA = gluster-blockd.service
endif
