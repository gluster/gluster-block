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



struct glfs *
glusterBlockVolumeInit(char *volume, int *errCode, char **errMsg)
{
  struct glfs *glfs;
  int ret;

  glfs = queryCache(volume);
  if (glfs) {
    return glfs;
  }

  glfs = glfs_new(volume);
  if (!glfs) {
    *errCode = errno;
    GB_ASPRINTF (errMsg, "Not able to Initialize volume %s [%s]", volume,
                 strerror(*errCode));
    LOG("gfapi", GB_LOG_ERROR, "glfs_new(%s) from %s failed[%s]", volume,
        "localhost", strerror(*errCode));
    return NULL;
  }

  ret = glfs_set_volfile_server(glfs, "tcp", "localhost", 24007);
  if (ret) {
    *errCode = errno;
    GB_ASPRINTF (errMsg, "Not able to add Volfile server for volume %s[%s]",
                 volume, strerror(*errCode));
    LOG("gfapi", GB_LOG_ERROR, "glfs_set_volfile_server(%s) of %s "
        "failed[%s]", "localhost", volume, strerror(*errCode));
    goto out;
  }

  ret = glfs_set_logging(glfs, GFAPI_LOG_FILE, GFAPI_LOG_LEVEL);
  if (ret) {
    *errCode = errno;
    GB_ASPRINTF (errMsg, "Not able to add logging for volume %s[%s]", volume,
                 strerror(*errCode));
    LOG("gfapi", GB_LOG_ERROR, "glfs_set_logging(%s, %d) on %s failed[%s]",
        GFAPI_LOG_FILE, GFAPI_LOG_LEVEL, volume, strerror(*errCode));
    goto out;
  }

  ret = glfs_init(glfs);
  if (ret) {
    *errCode = errno;
    if (*errCode == ENOENT) {
      GB_ASPRINTF (errMsg, "Volume %s does not exist", volume);
    } else if (*errCode == EIO) {
      GB_ASPRINTF (errMsg, "Check if volume %s is operational", volume);
    } else {
      GB_ASPRINTF (errMsg, "Not able to initialize volume %s[%s]", volume,
                   strerror(*errCode));
    }
    LOG("gfapi", GB_LOG_ERROR, "glfs_init() on %s failed[%s]", volume,
        strerror(*errCode));
    goto out;
  }

  if (appendNewEntry(volume, glfs)) {
    *errCode = ENOMEM;
    LOG("gfapi", GB_LOG_ERROR, "allocation failed in appendNewEntry(%s)", volume);
    goto out;
  }

  return glfs;

 out:
  glfs_fini(glfs);

  return NULL;
}


int
glusterBlockCreateEntry(struct glfs *glfs, blockCreateCli *blk, char *gbid,
                        int *errCode, char **errMsg)
{
  struct glfs_fd *tgfd;
  char *tmp;
  int ret;

  ret = glfs_mkdir (glfs, GB_STOREDIR, 0);
  if (ret && errno != EEXIST) {
    *errCode = errno;
    LOG("gfapi", GB_LOG_ERROR,
        "glfs_mkdir(%s) on volume %s for block %s failed[%s]",
        GB_STOREDIR, blk->volume, blk->block_name, strerror(errno));
    goto out;
  }

  ret = glfs_chdir (glfs, GB_STOREDIR);
  if (ret) {
    *errCode = errno;
    LOG("gfapi", GB_LOG_ERROR,
        "glfs_chdir(%s) on volume %s for block %s failed[%s]",
        GB_STOREDIR, blk->volume, blk->block_name, strerror(errno));
    goto out;
  }

  tgfd = glfs_creat(glfs, gbid,
                    O_WRONLY | O_CREAT | O_EXCL | O_SYNC,
                    S_IRUSR | S_IWUSR);
  if (!tgfd) {
    *errCode = errno;
    LOG("gfapi", GB_LOG_ERROR,
        "glfs_creat(%s) on volume %s for block %s failed[%s]",
        gbid, blk->volume, blk->block_name, strerror(errno));
    ret = -1;
    goto out;
  } else {
    ret = glfs_ftruncate(tgfd, blk->size);
    if (ret) {
      *errCode = errno;
      LOG("gfapi", GB_LOG_ERROR,
          "glfs_ftruncate(%s): on volume %s for block %s "
          "of size %zu failed[%s]", gbid, blk->volume, blk->block_name,
          blk->size, strerror(errno));
      goto unlink;
    }

    if (blk->prealloc && glfs_zerofill(tgfd, 0, blk->size)) {
      *errCode = errno;
      LOG("gfapi", GB_LOG_ERROR,
          "glfs_zerofill(%s): on volume %s for block %s "
          "of size %zu failed[%s]", gbid, blk->volume, blk->block_name,
          blk->size, strerror(errno));
      ret = -1;
      goto unlink;
    }
  }


unlink:
  if (tgfd && glfs_close(tgfd) != 0) {
    *errCode = errno;
    LOG("gfapi", GB_LOG_ERROR,
        "glfs_close(%s): on volume %s for block %s failed[%s]",
        gbid, blk->volume, blk->block_name, strerror(errno));
    ret = -1;
  }

  if (ret && glfs_unlink(glfs, gbid) && errno != ENOENT) {
    *errCode = errno;
    LOG("gfapi", GB_LOG_ERROR,
        "glfs_unlink(%s) on volume %s for block %s failed[%s]",
        gbid, blk->volume, blk->block_name, strerror(errno));
  }

 out:
  if (ret) {
    GB_ASPRINTF (errMsg, "Not able to create storage for %s/%s [%s]",
                 blk->volume, blk->block_name, strerror(*errCode));

    GB_ASPRINTF(&tmp, "%s/%s", GB_METADIR, blk->block_name);

    if (glfs_unlink(glfs, tmp) && errno != ENOENT) {
      LOG("gfapi", GB_LOG_ERROR,
          "glfs_unlink(%s) on volume %s for block %s failed[%s]",
          tmp, blk->volume, blk->block_name, strerror(errno));
    }
    GB_FREE(tmp);
  }

  return ret;
}


int
glusterBlockDeleteEntry(struct glfs *glfs, char *volume, char *gbid)
{
  int ret;


  ret = glfs_chdir (glfs, GB_STOREDIR);
  if (ret) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_chdir(%s) on volume %s failed[%s]",
        GB_STOREDIR, volume, strerror(errno));
    goto out;
  }

  ret = glfs_unlink(glfs, gbid);
  if (ret && errno != ENOENT) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_unlink(%s) on volume %s failed[%s]",
        gbid, volume, strerror(errno));
  }

 out:
  return ret;
}


struct glfs_fd *
glusterBlockCreateMetaLockFile(struct glfs *glfs, char *volume, int *errCode,
                               char **errMsg)
{
  struct glfs_fd *lkfd;
  int ret;


  ret = glfs_mkdir (glfs, GB_METADIR, 0);
  if (ret && errno != EEXIST) {
    *errCode = errno;
    LOG("gfapi", GB_LOG_ERROR, "glfs_mkdir(%s) on volume %s failed[%s]",
        GB_METADIR, volume, strerror(*errCode));
    goto out;
  }

  ret = glfs_chdir (glfs, GB_METADIR);
  if (ret) {
    *errCode = errno;
    LOG("gfapi", GB_LOG_ERROR, "glfs_chdir(%s) on volume %s failed[%s]",
        GB_METADIR, volume, strerror(*errCode));
    goto out;
  }

  lkfd = glfs_creat(glfs, GB_TXLOCKFILE, O_RDWR, S_IRUSR | S_IWUSR);
  if (!lkfd) {
    *errCode = errno;
    LOG("gfapi", GB_LOG_ERROR, "glfs_creat(%s) on volume %s failed[%s]",
        GB_TXLOCKFILE, volume, strerror(*errCode));
    goto out;
  }

  return lkfd;

 out:
  GB_ASPRINTF (errMsg, "Not able to create Metadata on volume %s[%s]", volume,
               strerror (*errCode));
  return NULL;
}

int
glusterBlockDeleteMetaFile(struct glfs *glfs,
                               char *volume, char *blockname)
{
  int ret;


  ret = glfs_chdir (glfs, GB_METADIR);
  if (ret) {
    LOG("gfapi", GB_LOG_ERROR,
        "glfs_chdir(%s) on volume %s for block %s failed[%s]",
        GB_METADIR, volume, blockname, strerror(errno));
    goto out;
  }

  ret = glfs_unlink(glfs, blockname);
  if (ret && errno != ENOENT) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_unlink(%s) on volume %s failed[%s]",
        blockname, volume, strerror(errno));
    goto out;
  }

 out:
  return ret;
}


void
blockFreeMetaInfo(MetaInfo *info)
{
  int i;


  if (!info)
    return;

  for (i = 0; i < info->nhosts; i++) {
    GB_FREE(info->list[i]);
  }

  GB_FREE(info->list);
  GB_FREE(info);
}


static int
blockStuffMetaInfo(MetaInfo *info, char *line)
{
  char *tmp = strdup(line);
  char *opt = strtok(tmp, ":");
  bool flag = 0;
  int  ret = -1;
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
  case GB_META_PASSWD:
    strcpy(info->passwd, strchr(line, ' ')+1);
    break;

  default:
    if(!info->list) {
      if(GB_ALLOC(info->list) < 0)
        goto out;
      if(GB_ALLOC(info->list[0]) < 0)
        goto out;
      strcpy(info->list[0]->addr, opt);
      strcpy(info->list[0]->status, strchr(line, ' ')+1);
      info->nhosts = 1;
    } else {
      if(GB_REALLOC_N(info->list, info->nhosts+1) < 0)
        goto out;
      for (i = 0; i < info->nhosts; i++) {
        if(!strcmp(info->list[i]->addr, opt)) {
          strcpy(info->list[i]->status, strchr(line, ' ')+1);
          flag = 1;
          break;
        }
      }
      if (!flag) {
        if(GB_ALLOC(info->list[info->nhosts]) < 0)
          goto out;
        strcpy(info->list[info->nhosts]->addr, opt);
        strcpy(info->list[info->nhosts]->status, strchr(line, ' ')+1);
        info->nhosts++;
      }
    }
    break;
  }

  ret = 0;

 out:
  GB_FREE(tmp);

  return ret;
}


int
blockGetMetaInfo(struct glfs* glfs, char* metafile, MetaInfo *info,
                 int *errCode)
{
  size_t count = 0;
  struct glfs_fd *tgmfd = NULL;
  char line[1024];
  char fpath[PATH_MAX] = {0};
  char *tmp;
  int ret;

  snprintf(fpath, sizeof fpath, "%s/%s", GB_METADIR, metafile);
  tgmfd = glfs_open(glfs, fpath, O_RDONLY);
  if (!tgmfd) {
    if (errCode) {
      *errCode = errno;
    }
    LOG("gfapi", GB_LOG_ERROR, "glfs_open(%s) failed[%s]", metafile,
                               strerror(errno));
    ret = -1;
    goto out;
  }

  while ((ret = glfs_read (tgmfd, line, sizeof(line), 0)) > 0) {
    tmp = strtok(line,"\n");
    count += strlen(tmp) + 1;
    ret = blockStuffMetaInfo(info, tmp);
    if (ret) {
      if (errCode) {
        *errCode = errno;
      }
      LOG("gfapi", GB_LOG_ERROR,
          "blockStuffMetaInfo: on volume %s for block %s failed[%s]",
          info->volume, metafile, strerror(errno));
      goto out;
    }
    glfs_lseek(tgmfd, count, SEEK_SET);
  }
  if (ret < 0 && errCode) {/*Failure from glfs_read*/
    *errCode = errno;
    goto out;
  }

 out:
  if (tgmfd && glfs_close(tgmfd) != 0) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_close(%s): on volume %s failed[%s]",
        metafile, info->volume, strerror(errno));
  }

  return ret;
}
