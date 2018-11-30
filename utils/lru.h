/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# ifndef   _LRU_H
# define   _LRU_H   1

# include  <glusterfs/api/glfs.h>

# include  "common.h"
# include  "list.h"

# define   LRU_COUNT_MAX   512
# define   LRU_COUNT_DEF   5

void
initCache(void);

glfs_t *
queryCache(const char *volname);

int
appendNewEntry(const char *volname, glfs_t *glfs);

int
glusterBlockSetLruCount(const size_t lruCount);

void
glusterBlockUpdateLruLogdir(const char *logDir);

# endif /* _LRU_H */
