dnl  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
dnl  This file is part of gluster-block.
dnl
dnl  This file is licensed to you under your choice of the GNU Lesser
dnl  General Public License, version 3 or any later version (LGPLv3 or
dnl  later), or the GNU General Public License, version 2 (GPLv2), in all
dnl  cases as published by the Free Software Foundation.


AC_PREREQ([2.69])
AC_INIT([gluster-block],
        m4_esyscmd(echo -n $(cat VERSION 2>/dev/null || git describe --always --tags | cut -f2 -d'v' | cut -f1 -d'-')),
        [pkalever@redhat.com],,
        [https://github.com/gluster/gluster-block.git])

AC_SUBST([PACKAGE_RELEASE], 1)

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES(yes)])
if make --help 2>&1 | grep -q no-print-directory; then
    AM_MAKEFLAGS="$AM_MAKEFLAGS --no-print-directory"
fi
AC_SUBST(AM_MAKEFLAGS)

AC_ARG_WITH(systemddir,
            AC_HELP_STRING([--with-systemddir=DIR],
                           [systemd service files in DIR @<:@PREFIX/lib/systemd/system@:>@]),
            [systemddir=$withval],
            [systemddir='${prefix}/lib/systemd/system'])
AC_SUBST(systemddir)
AM_CONDITIONAL([USE_SYSTEMD], test [ -d '/usr/lib/systemd/system' ])
if test -d '/usr/lib/systemd/system'; then
    AC_DEFINE([USE_SYSTEMD], [1], [use systemd])
fi

AC_ARG_WITH(initddir,
            AC_HELP_STRING([--with-initddir=DIR],
                           [initd service files in DIR @<:@/etc/rc.d/init.d@:>@]),
            [initddir=$withval],
            [initddir='/etc/rc.d/init.d'])
AC_SUBST(initddir)

AC_ARG_WITH(sysconfigdir,
            AC_HELP_STRING([--with-sysconfigdir=DIR],
                           [system service configuration in DIR @<:@/etc/sysconfig@:>@]),
            [sysconfigdir=$withval],
            [sysconfigdir='/etc/sysconfig'])
AC_SUBST(sysconfigdir)

AC_ARG_ENABLE([tirpc],
              AC_HELP_STRING([--enable-tirpc],
                             [enable use of tirpc [default: yes]]))

AM_INIT_AUTOMAKE([-Wall -Werror foreign nostdinc])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_FILES([gluster-block.spec
                 Makefile
                 rpc/Makefile
                 rpc/rpcl/Makefile
                 cli/Makefile
                 daemon/Makefile
                 utils/Makefile
                 systemd/Makefile
                 systemd/gluster-blockd.service
                 systemd/gluster-block-target.service
                 systemd/gluster-blockd.initd
                 docs/Makefile
                 extras/Makefile
                 tests/Makefile])
AC_CONFIG_MACRO_DIR([m4])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AM_MAINTAINER_MODE

# Configure libtool
LT_INIT

# Checks for programs.
AC_PROG_CC

# For legacy reasons.
AM_PROG_CC_C_O

AC_CHECK_PROG(RPCGEN, rpcgen, yes, no)
if test "x$RPCGEN" = "xno"; then
  AC_MSG_ERROR([rpcgen not found, needed for building])
fi

# Checks for header files.
AC_CHECK_HEADERS([stdio.h stdlib.h string.h stdbool.h stddef.h \
                  unistd.h errno.h memory.h time.h             \
                  netdb.h netinet/in.h sys/socket.h            \
                  pthread.h uuid/uuid.h glusterfs/api/glfs.h   \
                  rpc/pmap_clnt.h ])

# Checks for libraries.
# glusterfs-api versions have a prefix of "7."
PKG_CHECK_MODULES([GFAPI], [glusterfs-api >= 7.6],
                  [AC_DEFINE(GFAPI_VERSION760, 1, [Define to 1 if gfapi >=7.6.0])],
                  [PKG_CHECK_MODULES([GFAPI], [glusterfs-api >= 7.3.6],,
                                     [AC_MSG_ERROR([gfapi library >= 3.6 is required to build gluster-block])])
                  ]
                 )
AC_SUBST(GFAPI_CFLAGS)
AC_SUBST(GFAPI_LIBS)

PKG_CHECK_MODULES([JSONC], [json-c],,
                  [AC_MSG_ERROR([json-c library is required to build gluster-block])])
AC_SUBST(JSONC_CFLAGS)
AC_SUBST(JSONC_LIBS)

if test "$enable_tirpc" != "no"; then
    enable_tirpc="yes";
    AC_CHECK_HEADERS([rpc/rpc.h])
    PKG_CHECK_MODULES([TIRPC], [libtirpc >= 0.2.4],
                      [AC_DEFINE([HAVE_LIBTIRPC], [1],
                                 [Define to 1 if you have and wish to use libtirpc.])],
                      [AC_MSG_ERROR([libtirpc is required to build gluster-block])])
    AC_SUBST(TIRPC_CFLAGS)
    AC_SUBST(TIRPC_LIBS)
fi

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

localstatedir="$(eval echo ${localstatedir})"
LOCALSTATEDIR=${localstatedir}

GLUSTER_BLOCKD_WORKDIR="${LOCALSTATEDIR}/lib/gluster-block"
AC_SUBST(GLUSTER_BLOCKD_WORKDIR)

if test "x$prefix" = xNONE; then
   test $libexecdir = '${prefix}/libexec' && localstatedir=$ac_default_prefix/libexec
   libexecdir=/usr/libexec
else
   libexecdir='${prefix}/libexec'
fi

GLUSTER_BLOCKD_LIBEXECDIR="$(eval echo ${libexecdir}/gluster-block)"
AC_SUBST(GLUSTER_BLOCKD_LIBEXECDIR)

GLUSTER_BLOCKD_LOGROTATEDIR="$(eval echo /etc/logrotate.d)"
AC_SUBST(GLUSTER_BLOCKD_LOGROTATEDIR)

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
  Using Tirpc....: $enable_tirpc
---------------------------------------------

Check the above options and compile with:
 ${MAKE-make}

EOF
