/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# ifndef   _COMMON_H
# define   _COMMON_H   1

# include "utils.h"

# define  GB_UNIX_ADDRESS  "/var/run/gluster-block.socket"
# define  GB_TCP_PORT      24006

# define  DAEMON_LOG_FILE  "/var/log/gluster-block/gluster-blockd.log"
# define  CLI_LOG_FILE     "/var/log/gluster-block/gluster-block-cli.log"

# define  GFAPI_LOG_FILE   "/var/log/gluster-block/gluster-block-gfapi.log"
# define  GFAPI_LOG_LEVEL  7

# define  GB_METADIR       "/block-meta"
# define  GB_STOREDIR      "/block-store"
# define  GB_TXLOCKFILE    "meta.lock"


ssize_t glusterBlockCreateParseSize(char *value);

# endif /* _COMMON_H */
