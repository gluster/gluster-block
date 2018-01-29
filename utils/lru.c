/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

# include "lru.h"


static struct list_head Cache;
static int lruCount;
size_t glfsLruCount = 5;  /* default lru cache size */

typedef struct Entry {
  char volume[255];
  glfs_t *glfs;

  struct list_head list;
} Entry;


static void
releaseColdEntry(void)
{
  Entry *tmp;
  struct list_head *pos, *q = &Cache;


  list_for_each_prev(pos, q) {
    tmp = list_entry(pos, Entry, list);
    list_del(pos);

    glfs_fini(tmp->glfs);
    GB_FREE(tmp);
    lruCount--;

    break;
  }
}


int
appendNewEntry(const char *volname, glfs_t *fs)
{
  Entry *tmp;


  if (lruCount == glfsLruCount) {
    releaseColdEntry();
  }

  if (GB_ALLOC(tmp) < 0) {
    return -1;
  }
  GB_STRCPYSTATIC(tmp->volume, volname);
  tmp->glfs = fs;

  list_add(&(tmp->list), &Cache);

  lruCount++;

  return 0;
}


static void
boostEntryWarmness(const char *volname)
{
  Entry *tmp;
  struct list_head *pos, *q;


  list_for_each_safe(pos, q, &Cache){
    tmp = list_entry(pos, Entry, list);
    if (!strcmp(tmp->volume, volname)) {
      list_del(pos);
      list_add(&(tmp->list), &Cache);
      break;
    }
  }
}


glfs_t *
queryCache(const char *volname)
{
  Entry *tmp;
  struct list_head *pos, *q, *r = &Cache;


  list_for_each_safe(pos, q, r){
    tmp = list_entry(pos, Entry, list);
    if (!strcmp(tmp->volume, volname)) {
      boostEntryWarmness(volname);
      return tmp->glfs;
    }
  }

  return NULL;
}


void
initCache(void)
{
  INIT_LIST_HEAD(&Cache);
}
