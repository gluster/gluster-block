/*
  Copyright (c) 2019 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# ifndef   _VERSION_H
# define   _VERSION_H   1

# include  <linux/version.h>

# include  "config.h"


# define  DEPENDENCY_VERSION   KERNEL_VERSION

# define  TARGETCLI_STR    "targetcli"
# define  RTSLIB_STR       "rtslib"
# define  CONFIGSHELL_STR  "configshell"
# define  TCMU_STR         "tcmu-runner"

# define  TARGETCLI_VERSION   "targetcli --version 2>&1 | awk -F' ' '{printf $NF}'"
# define  RTSLIB_VERSION      "python -c 'from rtslib_fb import __version__; print(__version__)'"
# define  TCMU_VERSION        "tcmu-runner --version 2>&1 | awk -F' ' '{printf $NF}'"
# define  CONFIGSHELL_VERSION "python -c 'from configshell_fb import __version__; print(__version__)'"

# define  GLUSTER_BLOCK_VERSION                PACKAGE_VERSION

# define  VERNUM_BUFLEN                        8

/* Other dependencies versions */
# define  GB_MIN_TCMURUNNER_VERSION            "1.1.3"
# define  GB_MIN_TARGETCLI_VERSION             "2.1.fb49"
# define  GB_MIN_RTSLIB_BLKSIZE_VERSION        "2.1.69"
# define  GB_MIN_TARGETCLI_RELOAD_VERSION      "2.1.fb50"
# define  GB_MIN_RTSLIB_RELOAD_VERSION         "2.1.71"
# define  GB_MIN_CONFIGSHELL_SEM_VERSION       "1.1.25"
# define  GB_MIN_TCMURUNNER_IO_TIMEOUT_VERSION "1.5.0"
# define  GB_MIN_TARGETCLI_DAEMON_VERSION      "2.1.fb51"

# define  GB_MIN_TCMURUNNER_VERSION_CODE            DEPENDENCY_VERSION(1, 1, 3)
# define  GB_MIN_TARGETCLI_VERSION_CODE             DEPENDENCY_VERSION(2, 1, 49)
# define  GB_MIN_RTSLIB_BLKSIZE_VERSION_CODE        DEPENDENCY_VERSION(2, 1, 69)
# define  GB_MIN_TARGETCLI_RELOAD_VERSION_CODE      DEPENDENCY_VERSION(2, 1, 50)
# define  GB_MIN_RTSLIB_RELOAD_VERSION_CODE         DEPENDENCY_VERSION(2, 1, 71)
# define  GB_MIN_CONFIGSHELL_SEM_VERSION_CODE       DEPENDENCY_VERSION(1, 1, 25)
# define  GB_MIN_TCMURUNNER_IO_TIMEOUT_VERSION_CODE DEPENDENCY_VERSION(1, 5, 0)
# define  GB_MIN_TARGETCLI_DAEMON_VERSION_CODE      DEPENDENCY_VERSION(2, 1, 51)

# endif /* _VERSION_H */
