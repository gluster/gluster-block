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
# include "config.h"

# define  GB_LB_ATTR_PREFIX  "user.block"
# define  GB_ZEROS_BUF_SIZE  4194304  /* 4MiB */


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
        gbConf->volServer, strerror(*errCode));
    return NULL;
  }

  ret = glfs_set_volfile_server(glfs, "tcp", gbConf->volServer, 24007);
  if (ret) {
    *errCode = errno;
    GB_ASPRINTF (errMsg, "Not able to add Volfile server for volume %s[%s]",
                 volume, strerror(*errCode));
    LOG("gfapi", GB_LOG_ERROR, "glfs_set_volfile_server(%s) of %s "
        "failed[%s]", gbConf->volServer, volume, strerror(*errCode));
    goto out;
  }

  ret = glfs_set_logging(glfs, gbConf->gfapiLogFile, GFAPI_LOG_LEVEL);
  if (ret) {
    *errCode = errno;
    GB_ASPRINTF (errMsg, "Not able to add logging for volume %s[%s]", volume,
                 strerror(*errCode));
    LOG("gfapi", GB_LOG_ERROR, "glfs_set_logging(%s, %d) on %s failed[%s]",
        gbConf->gfapiLogFile, GFAPI_LOG_LEVEL, volume, strerror(*errCode));
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
glusterBlockCheckAvailableSpace(struct glfs *glfs,
                                char *volume, size_t blockSize, char **errMsg)
{
  struct statvfs buf = {'\0', };
  int errSave = 0;


  if (!glfs_statvfs(glfs, "/", &buf)) {
    if ((buf.f_bfree * buf.f_bsize) >= GB_METASTORE_RESERVE + blockSize) {
      return 0;
    }
    LOG("gfapi", GB_LOG_ERROR,
        "glfs_statvfs('%s'): Low space on volume => "
        "Total size: %lu, Free space: %lu, Block request space: %lu", volume,
        buf.f_blocks * buf.f_bsize, buf.f_bfree * buf.f_bsize, blockSize);
    GB_ASPRINTF(errMsg, "Low space on the volume %s\n", volume);
    errSave = ENOSPC;
  } else {
    errSave = errno;
    LOG("gfapi", GB_LOG_ERROR,
        "glfs_statvfs('%s'): couldn't get file-system statistics", volume);
    GB_ASPRINTF(errMsg,
                "couldn't get file-system statistics on volume %s\n", volume);
  }
  errno = errSave;

  return -1;
}


int
glusterBlockZeroFill(struct glfs_fd *tgfd, off_t offset, size_t size)
{
  struct iovec iov[4];
  char *zerodata = NULL;
  ssize_t len;
  size_t rest;
  int ret = -1;
  int i;


  LOG("gfapi", GB_LOG_INFO,
      "zerofill is not supported for this volume type, slow zeroing will be used");

  if (GB_ALLOC_N(zerodata, GB_ZEROS_BUF_SIZE) < 0) {
    LOG("gfapi", GB_LOG_ERROR, "Alloc failed");
    goto out;
  }

  for(i = 0; i < 4; ++i) {
    iov[i].iov_base = zerodata;
    iov[i].iov_len = GB_ZEROS_BUF_SIZE;
  }

  if (glfs_lseek(tgfd, offset, SEEK_SET) < 0) {
    LOG("gfapi", GB_LOG_ERROR,
        "glfs_lseek() failed: %s", strerror(errno));
    goto out;
  }

  while (size >= GB_ZEROS_BUF_SIZE * 4) {
    len = glfs_writev(tgfd, iov, 4, 0);
    if (len < 0) {
      LOG("gfapi", GB_LOG_ERROR,
          "glfs_writev() failed to write zeros: %s", strerror(errno));
      goto out;
    }
    size -= len;
  }

  if (size == 0) {
    ret = 0;
    goto out;
  }

  /* Calculate the rest */
  len = size / GB_ZEROS_BUF_SIZE;
  rest = size % GB_ZEROS_BUF_SIZE;
  iov[len].iov_len = rest;

  if (glfs_writev(tgfd, iov, rest ? len + 1 : len, 0) < 0) {
    LOG("gfapi", GB_LOG_ERROR,
        "glfs_writev() failed to write zeros: %s", strerror(errno));
    goto out;
  }

  ret = 0;

 out:
  GB_FREE(zerodata);
  return ret;
}


int
glusterBlockCreateEntry(struct glfs *glfs, blockCreateCli *blk, char *gbid,
                        int *errCode, char **errMsg)
{
  struct glfs_fd *tgfd;
  struct stat st;
  char *tmp;
  int ret = -1;


  if (glusterBlockCheckAvailableSpace(glfs, blk->volume, blk->size, errMsg)) {
    *errCode = errno;
    goto out;
  }

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

  if (strlen(blk->storage)) {
    ret = glfs_stat(glfs, blk->storage, &st);
    if (ret) {
      *errCode = errno;
      if (*errCode == ENOENT) {
        LOG("mgmt", GB_LOG_ERROR,
            "storage file '/block-store/%s' doesn't exist in volume %s",
            blk->storage, blk->volume);
        GB_ASPRINTF(errMsg,
                    "storage file '/block-store/%s' doesn't exist in volume %s\n",
                    blk->storage, blk->volume);
      } else {
        LOG("mgmt", GB_LOG_ERROR,
            "glfs_stat failed on /block-store/%s in volume %s [%s]",
            blk->storage, blk->volume, strerror(*errCode));
        GB_ASPRINTF(errMsg,
                    "glfs_stat failed on /block-store/%s in volume %s [%s]",
                    blk->storage, blk->volume, strerror(*errCode));
      }
      goto out;
    }
    blk->size = st.st_size;

    if (st.st_nlink == 1) {
      ret = glfs_link(glfs, blk->storage, gbid);
      if (ret) {
        *errCode=errno;
        LOG("mgmt", GB_LOG_ERROR,
            "glfs_link(%s, %s) on volume %s for block %s failed [%s]",
            blk->storage, gbid, blk->volume, blk->block_name, strerror(errno));
        GB_ASPRINTF(errMsg,
                    "glfs_link(%s, %s) on volume %s for block %s failed [%s]",
                    blk->storage, gbid, blk->volume, blk->block_name, strerror(errno));
        goto out;
      }
    } else {
      *errCode = EBUSY;
      LOG("mgmt", GB_LOG_ERROR,
          "storage file /block-store/%s is already in use in volume %s [%s]",
          blk->storage, blk->volume, strerror(*errCode));
      GB_ASPRINTF(errMsg,
                  "storage file /block-store/%s is already in use in volume %s [%s]\n"
                  "hint: delete the hardlink file, make sure file is not in use\n",
                  blk->storage, blk->volume, strerror(*errCode));
      ret = -1;
      goto out;
    }
    return 0;
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
#if GFAPI_VERSION760
    ret = glfs_ftruncate(tgfd, blk->size, NULL, NULL);
#else
    ret = glfs_ftruncate(tgfd, blk->size);
#endif
    if (ret) {
      *errCode = errno;
      LOG("gfapi", GB_LOG_ERROR,
          "glfs_ftruncate(%s): on volume %s for block %s "
          "of size %zu failed[%s]", gbid, blk->volume, blk->block_name,
          blk->size, strerror(errno));
      goto unlink;
    }

    if (blk->prealloc) {
      ret = glfs_zerofill(tgfd, 0, blk->size);
      if (ret && errno == ENOTSUP) {
        if (glusterBlockZeroFill(tgfd, 0, blk->size)) {
          *errCode = errno;
          LOG("gfapi", GB_LOG_ERROR, "glusterBlockZeroFill(%s) on "
              "volume: %s block: %s of size %zu failed [%s]",
              gbid, blk->volume, blk->block_name, blk->size, strerror(errno));
          ret = -1;
          goto unlink;
        }
        ret = 0;
      } else if (ret) {
        *errCode = errno;
        LOG("gfapi", GB_LOG_ERROR, "glfs_zerofill(%s): on "
            "volume %s for block %s of size %zu failed [%s]",
            gbid, blk->volume, blk->block_name, blk->size, strerror(errno));
        ret = -1;
        goto unlink;
      }
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
    if (errMsg && !(*errMsg)) {
      GB_ASPRINTF (errMsg, "Not able to create storage for %s/%s [%s]",
                   blk->volume, blk->block_name, strerror(*errCode));
    }

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
glusterBlockResizeEntry(struct glfs *glfs, blockModifySize *blk,
                        int *errCode, char **errMsg)
{
  char fpath[PATH_MAX] = {0};
  struct glfs_fd *tgfd;
  struct stat sb = {0, };
  int ret;

  snprintf(fpath, sizeof fpath, "%s/%s", GB_STOREDIR, blk->gbid);
  tgfd = glfs_open(glfs, fpath, O_WRONLY | O_SYNC);
  if (!tgfd) {
    *errCode = errno;
    LOG("gfapi", GB_LOG_ERROR, "glfs_open(%s) failed[%s]", blk->gbid,
        strerror(errno));
    ret = -1;
    goto out;
  } else {
    ret = glfs_stat (glfs, fpath, &sb);
    if (ret == -1) {
      *errCode = errno;
      LOG("gfapi", GB_LOG_ERROR,
          "glfs_stat(%s): on volume %s for block %s "
          "of size %zu failed[%s]", blk->gbid, blk->volume, blk->block_name,
          blk->size, strerror(errno));
      ret = -1;
      goto close;
    }

    /* skip changing file size */
    if (blk->size == sb.st_size) {
      ret = 0;
      goto close;
    }

    if (glusterBlockCheckAvailableSpace(glfs, blk->volume, blk->size - sb.st_size, errMsg)) {
      *errCode = errno;
      ret = -1;
      goto close;
    }

#if GFAPI_VERSION760
    ret = glfs_ftruncate(tgfd, blk->size, NULL, NULL);
#else
    ret = glfs_ftruncate(tgfd, blk->size);
#endif
    if (ret) {
      *errCode = errno;
      LOG("gfapi", GB_LOG_ERROR,
          "glfs_ftruncate(%s): on volume %s for block %s "
          "of size %zu failed[%s]", blk->gbid, blk->volume, blk->block_name,
          blk->size, strerror(errno));
      goto close;
    }

    /* dirty hack to check if the file is zerofilled ? */
    if ((blk->size > sb.st_size) && (sb.st_size <= 512 * sb.st_blocks)) {
      ret = glfs_zerofill(tgfd, sb.st_size, blk->size - sb.st_size);
      if (ret && errno == ENOTSUP) {
        if (glusterBlockZeroFill(tgfd, sb.st_size, blk->size - sb.st_size)) {
          *errCode = errno;
          LOG("gfapi", GB_LOG_ERROR, "glusterBlockZeroFill(%s) on "
              "volume %s for block %s of size %zu failed [%s]",
              blk->gbid, blk->volume, blk->block_name, blk->size, strerror(errno));
          ret = -1;
          goto close;
        }
        ret = 0;
      } else if (ret) {
        *errCode = errno;
        LOG("gfapi", GB_LOG_ERROR, "glfs_zerofill(%s): on "
            "volume %s for block %s of size %zu failed [%s]",
            blk->gbid, blk->volume, blk->block_name, blk->size, strerror(errno));
        ret = -1;
        goto close;
      }
    }
  }

 close:
  if (tgfd && glfs_close(tgfd) != 0) {
    if (!(*errCode)) {
      *errCode = errno;
    }
    LOG("gfapi", GB_LOG_ERROR,
        "glfs_close(%s): on volume %s for block %s failed[%s]",
        blk->gbid, blk->volume, blk->block_name, strerror(errno));
    ret = -1;
  }


 out:
  if (ret) {
    if (errMsg && !(*errMsg)) {
      GB_ASPRINTF (errMsg, "Not able to resize storage for %s/%s [%s]",
                   blk->volume, blk->block_name, strerror(*errCode));
    }

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
  int i, j;


  if (!info)
    return;

  for (i = 0; i < info->nhosts; i++) {
    if (info->list[i]) {
      for (j = 0; j < info->list[i]->nenties; j++) {
        GB_FREE(info->list[i]->st_journal[j]);
      }
      GB_FREE(info->list[i]->st_journal);
      GB_FREE(info->list[i]);
    }
  }

  GB_FREE(info->list);
  GB_FREE(info);
}


static void
blockParseRSstatus(MetaInfo *info)
{
  size_t i;
  char *s;


  for (i = 0; i < info->nhosts; i++) {
    if ( (s = strchr(info->list[i]->status, '-')) ) {
      *s = '\0';
      s++;
      sscanf(s, "%zu", &info->list[i]->size);
    }
  }
}


static int
blockStuffMetaInfo(MetaInfo *info, char *line)
{
  char *tmp = strdup(line);
  char *opt = strtok(tmp, ":");
  bool flag = 0;
  int  ret = -1;
  size_t i;


  if (!opt) {
    goto out;
  }

  switch (blockMetaKeyEnumParse(opt)) {
  case GB_META_VOLUME:
    GB_STRCPYSTATIC(info->volume, strchr(line, ' ') + 1);
    break;
  case GB_META_GBID:
     GB_STRCPYSTATIC(info->gbid, strchr(line, ' ') + 1);
    break;
  case GB_META_SIZE:
    sscanf(strchr(line, ' '), "%zu", &info->size);
    if (!info->initial_size)
      info->initial_size = info->size;
    break;
  case GB_META_RINGBUFFER:
    sscanf(strchr(line, ' '), "%zu", &info->rb_size);
    break;
  case GB_META_IO_TIMEOUT:
    sscanf(strchr(line, ' '), "%lu", &info->io_timeout);
    break;
  case GB_META_BLKSIZE:
    sscanf(strchr(line, ' '), "%zu", &info->blk_size);
    break;
  case GB_META_HA:
    sscanf(strchr(line, ' '), "%zu", &info->mpath);
    break;
  case GB_META_ENTRYCREATE:
    GB_STRCPYSTATIC(info->entry, strchr(line, ' ') + 1);
    break;
  case GB_META_PASSWD:
    GB_STRCPYSTATIC(info->passwd, strchr(line, ' ') + 1);
    break;
  case GB_META_PRIOPATH:
    GB_STRCPYSTATIC(info->prio_path, strchr(line, ' ') + 1);
    break;

  default:
    if (info->list) {
      if(GB_REALLOC_N(info->list, info->nhosts+1) < 0)
        goto out;
      for (i = 0; i < info->nhosts; i++) {
        if(!strcmp(info->list[i]->addr, opt)) {
          GB_STRCPYSTATIC(info->list[i]->status, strchr(line, ' ') + 1);
          if (GB_REALLOC_N(info->list[i]->st_journal, info->list[i]->nenties + 1) < 0)
            goto out;
          if (GB_STRDUP(info->list[i]->st_journal[info->list[i]->nenties], strchr(line, ' ') + 1) < 0)
            goto out;
          info->list[i]->nenties++;
          flag = 1;
          break;
        }
      }
    } else {
      if(GB_ALLOC(info->list) < 0)
        goto out;
    }

    if (!flag) {
      i = info->nhosts;
      if(GB_ALLOC(info->list[i]) < 0)
        goto out;
      GB_STRCPYSTATIC(info->list[i]->addr, opt);
      GB_STRCPYSTATIC(info->list[i]->status, strchr(line, ' ') + 1);
      if(GB_ALLOC(info->list[i]->st_journal) < 0)
        goto out;
      if (GB_STRDUP(info->list[i]->st_journal[info->list[i]->nenties], strchr(line, ' ') + 1) < 0)
        goto out;
      info->list[i]->nenties++;
      info->nhosts++;
    }
    break;
  }

  ret = 0;

 out:
  GB_FREE(tmp);

  return ret;
}


int
blockParseValidServers(struct glfs* glfs, char *metafile,
                       int *errCode, blockServerDefPtr *savelist, char *skiphost)
{
  blockServerDefPtr list = *savelist;
  char fpath[PATH_MAX] = {0};
  struct glfs_fd *tgmfd = NULL;
  char line[1024];
  int ret = -1;
  char *h, *s, *sep;
  size_t i, count = 0;
  bool match;


  snprintf(fpath, sizeof fpath, "%s/%s", GB_METADIR, metafile);
  tgmfd = glfs_open(glfs, fpath, O_RDONLY);
  if (!tgmfd) {
    if (errCode) {
      *errCode = errno;
    }
    LOG("gfapi", GB_LOG_ERROR, "glfs_open(%s) failed[%s]", metafile,
                               strerror(errno));
    goto out;
  }

  while ((ret = glfs_read (tgmfd, line, sizeof(line), 0)) > 0) {
    /* clip till current line */
    h = line;
    sep = strchr(h, '\n');
    *sep = '\0';

    count += strlen(h) + 1;

    /* Part before ':' */
    sep = strchr(h, ':');
    *sep = '\0';

    switch (blockMetaKeyEnumParse(h)) {
    case GB_META_VOLUME:
    case GB_META_GBID:
    case GB_META_SIZE:
    case GB_META_HA:
    case GB_META_ENTRYCREATE:
    case GB_META_PASSWD:
      break;
    default:
      if (skiphost && !strcmp(h, skiphost)) {
        break; /* switch case */
      }
      /* Part after ':' and before '\n' */
      s = sep + 1;
      while(*s == ' ') {
        s++;
      }

      if (!list) {
        if (blockhostIsValid(s)) {
          if (GB_ALLOC(list) < 0)
            goto out;
          if (GB_ALLOC(list->hosts) < 0)
            goto out;
          if (GB_STRDUP(list->hosts[0], h) < 0)
            goto out;

          list->nhosts = 1;
        }
      } else {
        match = false;
        for (i = 0; i < list->nhosts; i++) {
          if (!strcmp(list->hosts[i], h)) {
            match = true;
            break; /* for loop */
          }
        }
        if (!match && blockhostIsValid(s)){
          if(GB_REALLOC_N(list->hosts, list->nhosts+1) < 0)
            goto out;
          if (GB_STRDUP(list->hosts[list->nhosts], h) < 0)
            goto out;

          list->nhosts++;
        }
      }
      break; /* switch case */
    }

    glfs_lseek(tgmfd, count, SEEK_SET);
  }

  if (ret < 0 && errCode) { /*Failure from glfs_read*/
    *errCode = errno;
    goto out;
  }

  *savelist = list;
  list = NULL;
  ret = 0;

 out:
  if (tgmfd && glfs_close(tgmfd) != 0) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_close(%s): failed[%s]",
        metafile, strerror(errno));
  }
  blockServerDefFree(list);

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
    if (!tmp) {
      continue;
    }
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
  blockParseRSstatus(info);

 out:
  if (tgmfd && glfs_close(tgmfd) != 0) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_close(%s): on volume %s failed[%s]",
        metafile, info->volume, strerror(errno));
  }

  return ret;
}


void
blockGetPrioPath(struct glfs* glfs, char *volume, blockServerDefPtr list,
                 char *prio_path, size_t prio_len)
{
  struct glfs_fd *pfd = NULL;
  char attr[256];
  char buf[1024];
  int index = 0;
  size_t count = 0;
  size_t min = 0;
  bool flag = true;
  int i;
  int ret = -1;


  pfd = glfs_creat(glfs, GB_PRIO_FILE, O_RDONLY | O_CREAT | O_SYNC,
                   S_IRUSR | S_IWUSR);
  if (!pfd) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_creat(%s) on volume %s failed[%s]",
        GB_PRIO_FILE, volume, strerror(errno));
    return;
  }

  for (i = 0; i < list->nhosts; i++) {
    memset(attr, '\0', sizeof(attr));
    snprintf(attr, sizeof(attr), "%s.%s", GB_LB_ATTR_PREFIX, list->hosts[i]);

    memset(buf, '\0', sizeof(buf));
    if (glfs_fgetxattr(pfd, attr, buf, sizeof(buf)) < 0) {
      if (errno != ENODATA) {
        LOG("gfapi", GB_LOG_ERROR,
            "glfs_fgetxattr(%s) on volume %s for prio file %s failed[%s]",
            attr, volume, GB_PRIO_FILE, strerror(errno));
        goto out;
      } else {
        index = i;
        min = 0;
        break;
      }
    }

    sscanf(buf, "%zu", &count);
    if (flag || min > count) {
      min = count;
      index = i;
      flag = false;
    }
  }

  if (list->nhosts) {
    ret = 0;
  }

 out:
  if (pfd && glfs_close(pfd) != 0) {
    LOG("gfapi", GB_LOG_ERROR, "glfs_close(%s): on volume %s failed[%s]",
        GB_PRIO_FILE, volume, strerror(errno));
  }

  if (!ret) {
    if (strlen(list->hosts[index]) < sizeof(attr) - strlen(GB_LB_ATTR_PREFIX)) {
      GB_STRCPY(prio_path, list->hosts[index], prio_len);
    }
  }

  return;
}


void
blockIncPrioAttr(struct glfs* glfs, char *volume, char *addr)
{
  size_t count;
  char buf[1024] = {'\0', };
  char attr[256] = {'\0', };


  snprintf(attr, sizeof(attr), "%s.%s", GB_LB_ATTR_PREFIX, addr);
  if (glfs_getxattr(glfs, GB_PRIO_FILE, attr, buf, sizeof(buf)) < 0) {
    if (errno != ENODATA) {
      LOG("gfapi", GB_LOG_ERROR,
          "glfs_getxattr(%s) on volume %s for prio file %s failed[%s]",
          attr, volume, GB_PRIO_FILE, strerror(errno));
      return;
    } else {
      count = 0;
    }
  } else {
    sscanf(buf, "%zu", &count);
  }

  memset(buf, '\0', sizeof(buf));
  snprintf(buf, sizeof(buf), "%zu", count + 1);
  if (glfs_setxattr(glfs, GB_PRIO_FILE, attr, buf, sizeof(buf), 0) < 0) {
    LOG("gfapi", GB_LOG_ERROR,
        "glfs_setxattr(%s) on volume %s for prio file %s failed[%s]",
        attr, volume, GB_PRIO_FILE, strerror(errno));
    return;
  }

  return;
}


void
blockDecPrioAttr(struct glfs* glfs, char *volume, char *addr)
{
  size_t count;
  char buf[1024] = {'\0', };
  char attr[256] = {'\0', };


  snprintf(attr, sizeof(attr), "%s.%s", GB_LB_ATTR_PREFIX, addr);
  if (glfs_getxattr(glfs, GB_PRIO_FILE, attr, buf, sizeof(buf)) < 0) {
    if (errno != ENODATA) {
      LOG("gfapi", GB_LOG_ERROR,
          "glfs_getxattr(%s) on volume %s for prio file %s failed[%s]",
          attr, volume, GB_PRIO_FILE, strerror(errno));
      return;
    } else {
      count = 0;
    }
  } else {
    sscanf(buf, "%zu", &count);
  }

  if (count != 0) {
    memset(buf, '\0', sizeof(buf));
    snprintf(buf, sizeof(buf), "%zu", count - 1);
    if (glfs_setxattr(glfs, GB_PRIO_FILE, attr, buf, sizeof(buf), 0) < 0) {
      LOG("gfapi", GB_LOG_ERROR,
          "glfs_setxattr(%s) on volume %s for prio file %s failed[%s]",
          attr, volume, GB_PRIO_FILE, strerror(errno));
      return;
    }
  }

  return;
}


int
blockGetAddrStatusFromInfo(MetaInfo *info, char *addr)
{
  size_t i;


  if (!addr) {
    goto out;
  }

  for (i = 0; i < info->nhosts; i++) {
    if (!strcmp(addr, info->list[i]->addr)) {
      return blockMetaStatusEnumParse(info->list[i]->status);
    }
  }

 out:
  return GB_METASTATUS_MAX;
}
