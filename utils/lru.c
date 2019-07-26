/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

# define _GNU_SOURCE

# include <stdio.h>
# include  <pthread.h>
# include "lru.h"
# include "utils.h"


static LIST_HEAD(Cache);
static int lruCount;
static pthread_mutex_t lru_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct Entry {
  char volume[255];
  glfs_t *glfs;

  struct list_head list;
} Entry;


void
glusterBlockUpdateLruLogdir(const char *logPath)
{
  struct list_head *pos, *q;
  Entry *tmp;
  char *logs = NULL;
  char *onelog = NULL;


  LOCK(lru_lock);
  if (!logPath || list_empty(&Cache)) {
      UNLOCK(lru_lock);
      return;
  }

  list_for_each_safe(pos, q, &Cache){
    tmp = list_entry(pos, Entry, list);
    if (glfs_set_logging(tmp->glfs, logPath, GFAPI_LOG_LEVEL)) {

      /* using LOG() will lead to Thread dead lock here, as lru_lock is acquired
       * and now calling LOG() will try acquiring lock on gbConf.lock
       */
      GB_ASPRINTF(&logs, "%sglfs_set_logging(%s, %d) on %s failed[%s]\n",
                  onelog ? onelog : "", logPath, GFAPI_LOG_LEVEL,
                  ((Entry *)tmp)->volume, strerror(errno));
      GB_FREE(onelog);
      onelog = logs;
    }
  }
  UNLOCK(lru_lock);

  if (logs) {
    LOG("mgmt", GB_LOG_WARNING, "%s", logs);
    GB_FREE(logs);
  }
}


int
glusterBlockSetLruCount(const size_t lruCount)
{
  if (!lruCount || (lruCount > LRU_COUNT_MAX)) {
    MSG(stderr, "glfsLruCount should be [0 < COUNT < %d]",
        LRU_COUNT_MAX);
    LOG("mgmt", GB_LOG_ERROR,
        "glfsLruCount should be [0 < COUNT < %d]",
        LRU_COUNT_MAX);
    return -1;
  }

  LOCK(gbConf->lock);
  if (gbConf->glfsLruCount == lruCount) {
    UNLOCK(gbConf->lock);
    LOG("mgmt", GB_LOG_DEBUG,
        "No changes to current glfsLruCount: %lu, skipping it.",
        gbConf->glfsLruCount);
    return 0;
  }
  gbConf->glfsLruCount = lruCount;
  UNLOCK(gbConf->lock);

  LOG("mgmt", GB_LOG_CRIT,
      "glfsLruCount now is %lu", lruCount);

  return 0;
}


static void
releaseColdEntry(void)
{
  Entry *tmp;
  struct list_head *pos, *q = &Cache;


  LOCK(lru_lock);
  list_for_each_prev(pos, q) {
    tmp = list_entry(pos, Entry, list);
    list_del(pos);

    glfs_fini(tmp->glfs);
    GB_FREE(tmp);
    lruCount--;

    break;
  }
  UNLOCK(lru_lock);
}


int
appendNewEntry(const char *volname, glfs_t *fs)
{
  Entry *tmp;


  LOCK(gbConf->lock);
  if (lruCount == gbConf->glfsLruCount) {
    releaseColdEntry();
  }

  if (GB_ALLOC(tmp) < 0) {
    UNLOCK(gbConf->lock);
    return -1;
  }
  GB_STRCPYSTATIC(tmp->volume, volname);
  tmp->glfs = fs;

  LOCK(lru_lock);
  list_add(&(tmp->list), &Cache);
  UNLOCK(lru_lock);

  lruCount++;
  UNLOCK(gbConf->lock);

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


  LOCK(lru_lock);
  list_for_each_safe(pos, q, r){
    tmp = list_entry(pos, Entry, list);
    if (!strcmp(tmp->volume, volname)) {
      boostEntryWarmness(volname);
      UNLOCK(lru_lock);
      return tmp->glfs;
    }
  }
  UNLOCK(lru_lock);

  return NULL;
}


void
initCache(void)
{
  INIT_LIST_HEAD(&Cache);
}
