/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# ifndef   _GLFS_OPERATIONS_H
# define   _GLFS_OPERATIONS_H   1

# include  <stdio.h>
# include  <stdlib.h>
# include  <stdbool.h>
# include  <errno.h>

# include  <glusterfs/api/glfs.h>



typedef struct glusterBlockDef {
  char   *volume;
  char   *host;     /* TODO: use proper Transport Object */
  char   *filename;
  size_t size;
  bool   status;
} glusterBlockDef;
typedef glusterBlockDef *glusterBlockDefPtr;


int glusterBlockCreateEntry(glusterBlockDefPtr blk);

int glusterBlockDeleteEntry(glusterBlockDefPtr blk);

#endif /* _GLFS_OPERATIONS_H */
