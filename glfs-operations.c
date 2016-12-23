/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include "utils.h"
# include "glfs-operations.h"

# define  LOG_FILE         "/var/log/gluster-block/block.log"
# define  LOG_LEVEL        7



int
glusterBlockCreateEntry(glusterBlockDefPtr blk)
{
  struct glfs *glfs;
  struct glfs_fd *fd;
  int ret = 0;

  glfs = glfs_new(blk->volume);
  if (!glfs) {
    ERROR("%s", "glfs_new: returned NULL");
    return -1;
  }

  ret = glfs_set_volfile_server(glfs, "tcp", blk->host, 24007);
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

  fd = glfs_creat(glfs, blk->filename,
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
glusterBlockDeleteEntry(glusterBlockDefPtr blk)
{
  struct glfs *glfs;
  int ret = 0;

  glfs = glfs_new(blk->volume);
  if (!glfs) {
    ERROR("%s", "glfs_new: returned NULL");
    return -1;
  }

  ret = glfs_set_volfile_server(glfs, "tcp", blk->host, 24007);
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

  ret = glfs_unlink(glfs, blk->filename);
  if (ret) {
    ERROR("%s", "glfs_unlink: failed");
    goto out;
  }

 out:
  glfs_fini(glfs);
  return ret;
}
