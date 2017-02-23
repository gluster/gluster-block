dnl  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
dnl  This file is part of gluster-block.
dnl
dnl  This file is licensed to you under your choice of the GNU Lesser
dnl  General Public License, version 3 or any later version (LGPLv3 or
dnl  later), or the GNU General Public License, version 2 (GPLv2), in all
dnl  cases as published by the Free Software Foundation.


AC_PREREQ([2.69])
AC_INIT([gluster-block],
        m4_esyscmd(echo -n `git describe --always --tags`),
        [pkalever@redhat.com],,
        [https://github.com/gluster/gluster-block.git])

AC_SUBST([PACKAGE_RELEASE], 1)

AC_ARG_WITH(systemddir,
            [--with-systemddir=DIR systemd service files in DIR @<:@PREFIX/lib/systemd/system@:>@],
            [systemddir=$withval],
            [systemddir='${prefix}/lib/systemd/system'])
AC_SUBST(systemddir)
AM_CONDITIONAL([USE_SYSTEMD], test [ -d '/usr/lib/systemd/system' ])

AM_INIT_AUTOMAKE([-Wall -Werror foreign nostdinc silent-rules])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([gluster-block.spec
                 Makefile
                 rpc/Makefile
                 cli/Makefile
                 daemon/Makefile
                 utils/Makefile
                 systemd/Makefile
                 systemd/gluster-blockd.service])
AC_CONFIG_MACRO_DIR([m4])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AM_MAINTAINER_MODE

# Configure libtool
LT_INIT

# Checks for programs.
AC_PROG_CC

# Checks for header files.
AC_CHECK_HEADERS([stdio.h stdlib.h string.h stdbool.h stddef.h \
                  unistd.h errno.h memory.h time.h             \
                  netdb.h netinet/in.h sys/socket.h            \
                  pthread.h uuid/uuid.h glusterfs/api/glfs.h   \
                  rpc/pmap_clnt.h ])

# Checks for libraries.
# glusterfs-api versions have a prefix of "7."
PKG_CHECK_MODULES([GFAPI], [glusterfs-api >= 7.3.6],,
                  [AC_MSG_ERROR([gfapi library >= 3.6 is required to build gluster-block])])
AC_SUBST(GFAPI_CFLAGS)
AC_SUBST(GFAPI_LIBS)

AC_CHECK_LIB([uuid], [uuid_generate], [UUID="-luuid"],
             AC_MSG_ERROR([uuid library is required to build gluster-block]))
AC_SUBST(UUID)
AC_CHECK_LIB([pthread], [pthread_mutex_init],[PTHREAD="-lpthread"],
             AC_MSG_ERROR([Posix threads library is required to build gluster-block]))
AC_SUBST(PTHREAD)

# Dirty hacky stuff to make STATEDIR work
if test "x$prefix" = xNONE; then
   test $localstatedir = '${prefix}/var' && localstatedir=$ac_default_prefix/var
   localstatedir=/var
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_STRTOD
AC_CHECK_FUNCS([gethostbyname memset socket strchr strdup strerror])
AC_OUTPUT

cat <<EOF

------------------ Summary ------------------
 $PACKAGE_NAME version $PACKAGE_VERSION
  Prefix.........: $prefix
  C Compiler.....: $CC $CFLAGS $CPPFLAGS
  Linker.........: $LD $LDFLAGS $LIBS
---------------------------------------------

Check the above options and compile with:
 ${MAKE-make}

EOF
