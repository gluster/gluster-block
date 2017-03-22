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

# include  "block.h"



typedef struct NodeInfo {
  char addr[255];
  char status[16];
} NodeInfo;

typedef struct MetaInfo {
  char   volume[255];
  char   gbid[38];
  size_t size;
  size_t mpath;
  char   entry[16];

  size_t nhosts;
  NodeInfo **list;
} MetaInfo;


struct glfs *
glusterBlockVolumeInit(char *volume, int *errCode, char **errMsg);

int
glusterBlockCreateEntry(struct glfs *glfs, blockCreateCli *blk, char *gbid,
                        int *errCode, char **errMsg);

int
glusterBlockDeleteEntry(struct glfs *glfs, char *volume, char *gbid);

struct glfs_fd *
glusterBlockCreateMetaLockFile(struct glfs *glfs, char *volume, int *errCode,
                               char **errMsg);

int
glusterBlockDeleteMetaFile(struct glfs *glfs, char *volume, char *blockname);

int
blockGetMetaInfo(struct glfs* glfs, char* metafile, MetaInfo *info,
                 int *errCode);

void
blockFreeMetaInfo(MetaInfo *info);

#endif /* _GLFS_OPERATIONS_H */
