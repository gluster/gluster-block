/********************************************************************#
#                                                                    #
# Copyright (c) 2019 Red Hat, Inc. <http://www.redhat.com>           #
# This file is part of gluster-block.                                #
#                                                                    #
# This file is licensed to you under your choice of the GNU Lesser   #
# General Public License, version 3 or any later version (LGPLv3 or  #
# later), or the GNU General Public License, version 2 (GPLv2), in   #
# all cases as published by the Free Software Foundation.            #
#                                                                    #
#                                                                    #
# Compile:                                                           #
# $ gcc -lgfapi ./tests/gfapi-test.c -o ./tests/gfapi-test           #
#                                                                    #
# Run:                                                               #
# $ ./tests/gfapi-test <hosting-volname> <ip-address>                #
#                                                                    #
#********************************************************************/


# include  <stdio.h>
# include  <stdlib.h>
# include  <errno.h>
# include  <string.h>
# include  <glusterfs/api/glfs.h>

# define GB_TEST_FILE  "/gbtestfile"

int
main(int argc, char *argv[])
{
  glfs_t    *fs = NULL;
  int        ret = -1;
  struct glfs_fd *tgfd = NULL;


  if (argc != 3) {
    fprintf (stderr, "expecting 2 arguments in fixed order.\n"
                     "./gfapi-test <hosting-volname> <ip-address>\n");
    return -1;
  }

  fs = glfs_new (argv[1]);
  if (!fs) {
    fprintf (stderr,
             "glfs_new(%s): returned NULL: %s\n", argv[1], strerror(errno));
    goto fail;
  }

  ret = glfs_set_volfile_server (fs, "tcp", argv[2], 24007);
  if (ret) {
    fprintf (stderr,
             "glfs_set_volfile_server(%s) ret = %d: %s\n",
             argv[2], ret, strerror(errno));
    goto out;
  }

  ret = glfs_set_logging(fs, "/var/log/gluster-block/gluster-block-gfapi.log", 7);
  if (ret) {
    fprintf (stderr, "glfs_set_logging() ret = %d: %s\n", ret, strerror(errno));
    goto out;
  }

  ret = glfs_init(fs);
  if (ret) {
    fprintf (stderr, "glfs_init() ret = %d: %s\n", ret, strerror(errno));
    goto out;
  }
  ret = -1;

  tgfd = glfs_creat(fs, GB_TEST_FILE,
                    O_WRONLY | O_CREAT | O_EXCL | O_SYNC,
                    S_IRUSR | S_IWUSR);
  if (!tgfd) {
    fprintf (stderr, "glfs_creat() failed: %s", strerror(errno));
    goto out;
  }

  if (glfs_close(tgfd) != 0) {
    fprintf (stderr, "glfs_close() failed: %s", strerror(errno));
    goto out;
  }

  tgfd = glfs_open(fs, GB_TEST_FILE, O_RDONLY);
  if (!tgfd) {
    fprintf (stderr, "glfs_open() failed: %s", strerror(errno));
    goto out;
  }

  fprintf (stdout, "Test works as expected!\n");
  ret = 0;

out:
  if (tgfd && glfs_close(tgfd) != 0) {
    fprintf (stderr, "glfs_close() failed: %s", strerror(errno));
  }

  if (glfs_unlink(fs, GB_TEST_FILE) && errno != ENOENT) {
    fprintf (stderr, "glfs_unlink() failed: %s", strerror(errno));
  }

  if (glfs_fini(fs) < 0) {
    fprintf (stderr, "glfs_fini() failed: %s", strerror(errno));
  }

fail:
  if (ret) {
    fprintf (stderr, "Test failed!\n");
  }

  return ret;
}
