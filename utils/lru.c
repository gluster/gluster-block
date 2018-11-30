/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

# include "lru.h"
# include "utils.h"


static struct list_head Cache;
static int lruCount;

typedef struct Entry {
  char volume[255];
  glfs_t *glfs;

  struct list_head list;
} Entry;

int
glusterBlockSetLruCount(const size_t lruCount)
{
  if (!lruCount || (lruCount > LRU_COUNT_MAX)) {
    MSG(stderr, "glfsLruCount should be [0 < COUNT < %d]\n",
        LRU_COUNT_MAX);
    LOG("mgmt", GB_LOG_ERROR,
        "glfsLruCount should be [0 < COUNT < %d]\n",
        LRU_COUNT_MAX);
    return -1;
  }

  LOCK(gbConf.lock);
  gbConf.glfsLruCount = lruCount;
  UNLOCK(gbConf.lock);

  LOG("mgmt", GB_LOG_CRIT,
      "glfsLruCount now is %lu\n", lruCount);
  return 0;
}


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


  LOCK(gbConf.lock);
  if (lruCount == gbConf.glfsLruCount) {
    releaseColdEntry();
  }

  if (GB_ALLOC(tmp) < 0) {
    UNLOCK(gbConf.lock);
    return -1;
  }
  GB_STRCPYSTATIC(tmp->volume, volname);
  tmp->glfs = fs;

  list_add(&(tmp->list), &Cache);

  lruCount++;
  UNLOCK(gbConf.lock);

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
