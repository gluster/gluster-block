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


/* Other dependencies versions */
# define  GB_MIN_TCMURUNNER_VERSION            "1.1.3"
# define  GB_MIN_TARGETCLI_VERSION             "2.1.fb49"

# define  GB_MIN_TCMURUNNER_VERSION_CODE       65795
# define  GB_MIN_TARGETCLI_VERSION_CODE        131377


# endif /* _VERSION_H */
