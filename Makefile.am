ACLOCAL_AMFLAGS = -I m4

SUBDIRS = rpc utils cli daemon systemd

DISTCLEANFILES = Makefile.in gluster-block.spec autom4te.cache

CLEANFILES = *~ gluster-block.spec

EXTRA_DIST = autogen.sh README.md COPYING-GPLV2 COPYING-LGPLV3

rpms: prep
	rpmbuild --define '_topdir $(abs_top_builddir)/build/rpmbuild' \
		--define '_sourcedir $(abs_top_builddir)' \
		-ba gluster-block.spec

srpm: prep
	rpmbuild --define '_topdir $(abs_top_builddir)/build/rpmbuild' \
		--define '_sourcedir $(abs_top_builddir)' \
		-bs gluster-block.spec

prep: dist
	@mkdir -p build/rpmbuild/{BUILD,SPECS,RPMS,SRPMS,SOURCES}

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

dist-hook: gen-ChangeLog

.PHONY: gen-ChangeLog

gen-ChangeLog:
	(cd $(srcdir) && git diff && echo ===== git log ==== && git log) > $(distdir)/ChangeLog
