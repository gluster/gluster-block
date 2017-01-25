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

# include  "rpc/block.h"


typedef enum Metakey {
  GBID = 0,
  SIZE = 1,
  HA   = 2,
  ENTRYCREATE = 3,

  METAKEY__MAX = 4      /* Updata this when add new Key */
} Metakey;


typedef struct NodeInfo {
  char addr[255];
  char status[16];
} NodeInfo;

typedef struct MetaInfo {
  char   gbid[38];
  size_t size;
  size_t mpath;
  char   entry[16];

  size_t nhosts;
  NodeInfo **list;
} MetaInfo;

static const char *const MetakeyLookup[] = {
    [GBID] = "GBID",
    [SIZE] = "SIZE",
    [HA] = "HA",
    [ENTRYCREATE] = "ENTRYCREATE",
    [METAKEY__MAX] = NULL,
};


struct glfs *
glusterBlockVolumeInit(char *volume, char *volfileserver);

int
glusterBlockCreateEntry(blockCreateCli *blk, char *gbid);

int
glusterBlockDeleteEntry(blockCreate *blk);

struct glfs_fd *
glusterBlockCreateMetaLockFile(struct glfs *glfs);

void
blockGetMetaInfo(struct glfs *glfs, char *metafile, MetaInfo *info);

void
blockFreeMetaInfo(MetaInfo *info);

#endif /* _GLFS_OPERATIONS_H */
