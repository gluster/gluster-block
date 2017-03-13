/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

# ifndef _BLOCK_SVC_H
# define _BLOCK_SVC_H

void
gluster_block_cli_1(struct svc_req *rqstp, register SVCXPRT *transp);

void
gluster_block_1(struct svc_req *rqstp, register SVCXPRT *transp);

# endif /* _BLOCK_SVC_H */
