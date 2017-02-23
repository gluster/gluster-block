ACLOCAL_AMFLAGS = -I m4

SUBDIRS = rpc utils cli daemon systemd

DISTCLEANFILES = Makefile.in gluster-block.spec autom4te.cache

CLEANFILES = *~ gluster-block.spec

EXTRA_DIST = autogen.sh README.md COPYING-GPLV2 COPYING-LGPLV3

rpms:	prep
	QA_RPATHS=17 rpmbuild --define '_topdir $(abs_top_builddir)/build/rpmbuild' \
		-ba $(abs_top_builddir)/build/rpmbuild/SPECS/gluster-block.spec

prep: dist
	mkdir -p build/rpmbuild && cd build/rpmbuild \
		&& mkdir BUILD SPECS RPMS SRPM SOURCES && cd ../..
	rm -rf rpmbuild/SOURCES/*
	cp *.tar.gz build/rpmbuild/SOURCES
	cp gluster-block.spec build/rpmbuild/SPECS

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
