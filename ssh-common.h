/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# ifndef   _SSH_COMMON_H
# define   _SSH_COMMON_H   1

# include  <libssh/libssh.h>



char *
glusterBlockSSHRun(char *host, char *cmd, bool console);

#endif /* _SSH_COMMON_H */
