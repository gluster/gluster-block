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
glusterBlockVolumeInit(char *volume, char *volfileserver)
{
  struct glfs *glfs;
  int ret = 0;

  glfs = glfs_new(volume);
  if (!glfs) {
    ERROR("%s", "glfs_new: returned NULL");
    return NULL;
  }

  ret = glfs_set_volfile_server(glfs, "tcp", volfileserver, 24007);
  if (ret) {
    ERROR("%s", "glfs_set_volfile_server: failed");
    goto out;
  }

  ret = glfs_set_logging(glfs, LOG_FILE, LOG_LEVEL);
  if (ret) {
    ERROR("%s", "glfs_set_logging: failed");
    goto out;
  }

  ret = glfs_init(glfs);
  if (ret) {
    ERROR("%s", "glfs_init: failed");
    goto out;
  }

  return glfs;

 out:
  glfs_fini(glfs);
  return NULL;
}


int
glusterBlockCreateEntry(blockCreateCli *blk, char *gbid)
{
  struct glfs *glfs;
  struct glfs_fd *fd;
  int ret = -1;

  glfs = glusterBlockVolumeInit(blk->volume, blk->volfileserver);
  if (!glfs) {
    ERROR("%s", "glusterBlockVolumeInit: failed");
    goto out;
  }

  fd = glfs_creat(glfs, gbid,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR);
  if (!fd) {
    ERROR("%s", "glfs_creat: failed");
    ret = -errno;
  } else {
    ret = glfs_ftruncate(fd, blk->size);
    if (ret) {
      ERROR("%s", "glfs_ftruncate: failed");
      goto out;
    }

    if (glfs_close(fd) != 0) {
      ERROR("%s", "glfs_close: failed");
      ret = -errno;
    }
  }

 out:
  glfs_fini(glfs);
  return ret;
}


int
glusterBlockDeleteEntry(char *volume, char *gbid)
{
  struct glfs *glfs;
  int ret = -1;

  glfs = glusterBlockVolumeInit(volume, "localhost");
  if (!glfs) {
    ERROR("%s", "glusterBlockVolumeInit: failed");
    goto out;
  }

  ret = glfs_unlink(glfs, gbid);
  if (ret) {
    ERROR("%s", "glfs_unlink: failed");
    goto out;
  }

 out:
  glfs_fini(glfs);
  return ret;
}


struct glfs_fd *
glusterBlockCreateMetaLockFile(struct glfs *glfs)
{
  struct glfs_fd *lkfd;
  int ret;

  ret = glfs_mkdir (glfs, "/block-meta", 0);
  if (ret && errno != EEXIST) {
    ERROR("%s", "glfs_mkdir: failed");
    goto out;
  }

  ret = glfs_chdir (glfs, "/block-meta");
  if (ret) {
    ERROR("%s", "glfs_chdir: failed");
    goto out;
  }

  lkfd = glfs_creat(glfs, "meta.lock", O_RDWR, S_IRUSR | S_IWUSR);
  if (!lkfd) {
    ERROR("%s", "glfs_creat: failed");
    goto out;
  }

  return lkfd;

 out:
  return NULL;
}


static int
blockEnumParse(const char *opt)
{
    int i;

    if (!opt) {
        return METAKEY__MAX;
    }

    for (i = 0; i < METAKEY__MAX; i++) {
        if (!strcmp(opt, MetakeyLookup[i])) {
            return i;
        }
    }

    return i;
}

void
blockFreeMetaInfo(MetaInfo *info)
{
  int i;

  for (i = 0; i< info->nhosts; i++)
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

  switch (blockEnumParse(opt)) {
  case GBID:
    strcpy(info->gbid, strchr(line, ' ')+1);
    break;
  case SIZE:
    sscanf(strchr(line, ' ')+1, "%zu", &info->size);
    break;
  case HA:
    sscanf(strchr(line, ' ')+1, "%zu", &info->mpath);
    break;
  case ENTRYCREATE:
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

void
blockGetMetaInfo(struct glfs* glfs, char* metafile, MetaInfo *info)
{
  size_t count = 0;
  struct glfs_fd *tgfd;
  char line[48];
  char *tmp;

  tgfd = glfs_open(glfs, metafile, O_RDWR);
  if (!tgfd) {
    ERROR("%s", "glfs_open failed");
  }

  while (glfs_read (tgfd, line, 48, 0) > 0) {
    tmp = strtok(line,"\n");
    count += strlen(tmp) + 1;
    blockStuffMetaInfo(info, tmp);
    glfs_lseek(tgfd, count, SEEK_SET);
  }

  glfs_close(tgfd);
}
