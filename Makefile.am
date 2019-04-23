ACLOCAL_AMFLAGS = -I m4

SUBDIRS = rpc utils cli daemon systemd docs extras tests

DISTCLEANFILES = Makefile.in gluster-block.spec autom4te.cache

noinst_HEADERS = version.h config.h

CLEANFILES = *~ gluster-block.spec

EXTRA_DIST = autogen.sh README.md COPYING-GPLV2 COPYING-LGPLV3 \
						 gluster-block.spec INSTALL NEWS

if USE_SYSTEMD
RPM_WITH_FLAGS = --with systemd
else
RPM_WITH_FLAGS = --with initd
endif

rpms: prep
	rpmbuild --define '_topdir $(abs_top_builddir)/build/rpmbuild' \
		--define '_sourcedir $(abs_top_builddir)' \
		$(RPM_WITH_FLAGS) \
		-ba gluster-block.spec

srpm: prep
	rpmbuild --define '_topdir $(abs_top_builddir)/build/rpmbuild' \
		--define '_sourcedir $(abs_top_builddir)' \
		-bs gluster-block.spec

prep: dist
	@mkdir -p build/rpmbuild/{BUILD,SPECS,RPMS,SRPMS,SOURCES}

install-data-local:
	mkdir -p $(DESTDIR)/$(localstatedir)/log/gluster-block

uninstall-local:
	cd $(DESTDIR)$(bindir) && rm -f gluster-block gluster-blockd

clean-local:
	rm -rf build rpmbuild *.rpm

gitclean: clean distclean
	find . -path gnulib -prune -name Makefile.in -exec rm -f {} \;
	find . -path gnulib -prune -name Makefile -exec rm -f {} \;
	rm -f aclocal.m4 compile config.* configure depcomp install-sh libtool \
		ltmain.sh missing stamp-h1
	rm -rf autom4te.cache ar-lib *.tar.gz m4

dist-hook: gen-ChangeLog gen-VERSION

.PHONY: gen-ChangeLog gen-VERSION

gen-ChangeLog:
	(cd $(srcdir) && git diff && echo ===== git log ==== && git log) > $(distdir)/ChangeLog

gen-VERSION:
	(cd $(srcdir) && git describe --always --tags | cut -f2 -d'v' | cut -f1 -d'-') > $(distdir)/VERSION
