/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include "common.h"
# include "glfs-operations.h"

# define  METADIR     "/block-meta"
# define  TXLOCKFILE  "meta.lock"



struct glfs *
glusterBlockVolumeInit(char *volume, char *volfileserver)
{
  struct glfs *glfs;
  int ret;


  glfs = glfs_new(volume);
  if (!glfs) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_new(%s) from %s failed[%s]", volume,
        volfileserver, strerror(errno));
    return NULL;
  }

  ret = glfs_set_volfile_server(glfs, "tcp", volfileserver, 24007);
  if (ret) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_set_volfile_server(%s) of %s "
        "failed[%s]", volfileserver, volume, strerror(errno));
    goto out;
  }

  ret = glfs_set_logging(glfs, GFAPI_LOG_FILE, GFAPI_LOG_LEVEL);
  if (ret) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_set_logging(%s, %d) on %s failed[%s]",
        GFAPI_LOG_FILE, GFAPI_LOG_LEVEL, volume, strerror(errno));
    goto out;
  }

  ret = glfs_init(glfs);
  if (ret) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_init() on %s failed[%s]", volume,
        strerror(errno) );
    goto out;
  }

  return glfs;

 out:
  glfs_fini(glfs);

  return NULL;
}


int
glusterBlockCreateEntry(struct glfs *glfs,
                        blockCreateCli *blk,
                        char *gbid)
{
  struct glfs_fd *tgfd;
  int ret = -1;


  tgfd = glfs_creat(glfs, gbid,
                    O_WRONLY | O_CREAT | O_EXCL,
                    S_IRUSR | S_IWUSR);
  if (!tgfd) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_creat(%s) on volume %s failed[%s]",
        gbid, blk->volume, strerror(errno));
  } else {
    ret = glfs_ftruncate(tgfd, blk->size);
    if (ret) {
      LOG("gfapi", GB_LOG_ERROR, "glfs_ftruncate(%s): on volume %s "
          "of size %zu failed[%s]", gbid, blk->volume, blk->size,
          strerror(errno));
      goto out;
    }

    if (glfs_close(tgfd) != 0) {
      LOG("gfapi", GB_LOG_ERROR, "glfs_close(%s): on volume %s failed[%s]",
          gbid, blk->volume, strerror(errno));
      goto out;
    }
  }

 out:
  return ret;
}


int
glusterBlockDeleteEntry(struct glfs *glfs, char *volume, char *gbid)
{
  int ret;


  ret = glfs_unlink(glfs, gbid);
  if (ret) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_unlink(%s) on volume %s failed[%s]",
        gbid, volume, strerror(errno));
  }

  return ret;
}


struct glfs_fd *
glusterBlockCreateMetaLockFile(struct glfs *glfs, char *volume)
{
  struct glfs_fd *lkfd;
  int ret;


  ret = glfs_mkdir (glfs, METADIR, 0);
  if (ret && errno != EEXIST) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_mkdir(%s) on volume %s failed[%s]",
        METADIR, volume, strerror(errno));
    goto out;
  }

  ret = glfs_chdir (glfs, METADIR);
  if (ret) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_chdir(%s) on volume %s failed[%s]",
        METADIR, volume, strerror(errno));
    goto out;
  }

  lkfd = glfs_creat(glfs, TXLOCKFILE, O_RDWR, S_IRUSR | S_IWUSR);
  if (!lkfd) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_creat(%s) on volume %s failed[%s]",
        TXLOCKFILE, volume, strerror(errno));
    goto out;
  }

  return lkfd;

 out:
  return NULL;
}


void
blockFreeMetaInfo(MetaInfo *info)
{
  int i;


  if (!info)
    return;

  for (i = 0; i < info->nhosts; i++)
    GB_FREE(info->list[i]);

  GB_FREE(info->list);
  GB_FREE(info);
}


static void
blockStuffMetaInfo(MetaInfo *info, char *line)
{
  char* tmp = strdup(line);
  char* opt = strtok(tmp,":");
  int Flag = 0;
  size_t i;


  switch (blockMetaKeyEnumParse(opt)) {
  case GB_META_VOLUME:
    strcpy(info->volume, strchr(line, ' ')+1);
    break;
  case GB_META_GBID:
    strcpy(info->gbid, strchr(line, ' ')+1);
    break;
  case GB_META_SIZE:
    sscanf(strchr(line, ' ')+1, "%zu", &info->size);
    break;
  case GB_META_HA:
    sscanf(strchr(line, ' ')+1, "%zu", &info->mpath);
    break;
  case GB_META_ENTRYCREATE:
    strcpy(info->entry, strchr(line, ' ')+1);
    break;

  default:
    if(!info->list) {
      if(GB_ALLOC(info->list) < 0)
        return;
      if(GB_ALLOC(info->list[0]) < 0)
        return;
      strcpy(info->list[0]->addr, opt);
      strcpy(info->list[0]->status, strchr(line, ' ')+1);
      info->nhosts = 1;
    } else {
      if(GB_REALLOC_N(info->list, info->nhosts+1) < 0)
        return;
      for (i = 0; i < info->nhosts; i++) {
        if(!strcmp(info->list[i]->addr, opt)) {
          strcpy(info->list[i]->status, strchr(line, ' ')+1);
          Flag = 1;
          break;
        }
      }
      if (!Flag) {
        if(GB_ALLOC(info->list[info->nhosts]) < 0)
          return;
        strcpy(info->list[info->nhosts]->addr, opt);
        strcpy(info->list[info->nhosts]->status, strchr(line, ' ')+1);
        info->nhosts++;
      }
    }
    break;
  }

  GB_FREE(tmp);
}


int
blockGetMetaInfo(struct glfs* glfs, char* metafile, MetaInfo *info)
{
  size_t count = 0;
  struct glfs_fd *tgmfd;
  char line[1024];
  char *tmp;


  tgmfd = glfs_open(glfs, metafile, O_RDONLY);
  if (!tgmfd) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_open(%s) on volume %s failed[%s]",
        metafile, info->volume, strerror(errno));
    return -1;
  }

  while (glfs_read (tgmfd, line, sizeof(line), 0) > 0) {
    tmp = strtok(line,"\n");
    count += strlen(tmp) + 1;
    blockStuffMetaInfo(info, tmp);
    glfs_lseek(tgmfd, count, SEEK_SET);
  }

  glfs_close(tgmfd);

  return 0;
}
