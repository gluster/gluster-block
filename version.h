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


# define  DEPENDENCY_VERSION   KERNEL_VERSION


# define  GLUSTER_BLOCK_VERSION                "0.4"

# define  VERNUM_BUFLEN                        8

/* Other dependencies versions */
# define  GB_MIN_TCMURUNNER_VERSION            "1.1.3"
# define  GB_MIN_TARGETCLI_VERSION             "2.1.fb49"
# define  GB_MIN_RTSLIB_BLKSIZE_VERSION        "2.1.69"
# define  GB_MIN_TARGETCLI_RELOAD_VERSION      "2.1.fb50"
# define  GB_MIN_RTSLIB_RELOAD_VERSION         "2.1.71"

# define  GB_MIN_TCMURUNNER_VERSION_CODE       DEPENDENCY_VERSION(1, 1, 3)
# define  GB_MIN_TARGETCLI_VERSION_CODE        DEPENDENCY_VERSION(2, 1, 49)
# define  GB_MIN_RTSLIB_BLKSIZE_VERSION_CODE   DEPENDENCY_VERSION(2, 1, 69)
# define  GB_MIN_TARGETCLI_RELOAD_VERSION_CODE DEPENDENCY_VERSION(2, 1, 50)
# define  GB_MIN_RTSLIB_RELOAD_VERSION_CODE    DEPENDENCY_VERSION(2, 1, 71)

# endif /* _VERSION_H */
