/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  <fcntl.h>
# include  <dirent.h>
# include  <sys/stat.h>
# include  <pthread.h>
# include  <rpc/pmap_clnt.h>

# include  "common.h"
# include  "block.h"
# include  "block_svc.h"



static bool
glusterBlockLogdirCreate(void)
{
  DIR* dir = opendir(GB_LOGDIR);


  if (dir) {
    closedir(dir);
  } else if (errno == ENOENT) {
    if (mkdir(GB_LOGDIR, 0755) == -1) {
      LOG("mgmt", GB_LOG_ERROR, "mkdir(%s) failed (%s)",
          GB_LOGDIR, strerror (errno));
      return FALSE;
    }
  } else {
    LOG("mgmt", GB_LOG_ERROR, "opendir(%s) failed (%s)",
        GB_LOGDIR, strerror (errno));
    return FALSE;
  }

  return TRUE;
}


void *
glusterBlockCliThreadProc (void *vargp)
{
  register SVCXPRT *transp = NULL;
  struct sockaddr_un saun = {0, };
  int sockfd = -1;


  if (strlen(GB_UNIX_ADDRESS) > SUN_PATH_MAX) {
    LOG("mgmt", GB_LOG_ERROR,
        "%s: path length is more than SUN_PATH_MAX: (%zu > %zu chars)",
        GB_UNIX_ADDRESS, strlen(GB_UNIX_ADDRESS), SUN_PATH_MAX);
    goto out;
  }

  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, IPPROTO_IP)) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "UNIX socket creation failed (%s)",
        strerror (errno));
    goto out;
  }

  saun.sun_family = AF_UNIX;
  strcpy(saun.sun_path, GB_UNIX_ADDRESS);

  if (unlink(GB_UNIX_ADDRESS) && errno != ENOENT) {
    LOG("mgmt", GB_LOG_ERROR, "unlink(%s) failed (%s)",
        GB_UNIX_ADDRESS, strerror (errno));
    goto out;
  }

  if (bind(sockfd, (struct sockaddr *) &saun,
           sizeof(struct sockaddr_un)) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "bind on '%s' failed (%s)",
        GB_UNIX_ADDRESS, strerror (errno));
    goto out;
  }

  transp = svcunix_create(sockfd, 0, 0, GB_UNIX_ADDRESS);
  if (!transp) {
    LOG("mgmt", GB_LOG_ERROR,
        "RPC service transport create failed for unix (%s)",
        strerror (errno));
    goto out;
  }

  if (!svc_register(transp, GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS,
                    gluster_block_cli_1, IPPROTO_IP)) {
		LOG("mgmt", GB_LOG_ERROR,
        "unable to register (GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS: %s)",
        strerror (errno));
    goto out;
	}

  svc_run ();

 out:
  if (transp) {
    svc_destroy(transp);
  }

  if (sockfd != -1) {
    close(sockfd);
  }

  return NULL;
}


void *
glusterBlockServerThreadProc(void *vargp)
{
  register SVCXPRT *transp = NULL;
  struct sockaddr_in sain = {0, };
  int sockfd;
  int opt = 1;


  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "TCP socket creation failed (%s)",
        strerror (errno));
    goto out;
  }

  if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)) < 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "setsockopt() for SO_REUSEADDR failed (%s)", strerror (errno));
    goto out;
  }

  sain.sin_family = AF_INET;
  sain.sin_addr.s_addr = INADDR_ANY;
  sain.sin_port = htons(GB_TCP_PORT);

  if (bind(sockfd, (struct sockaddr *) &sain, sizeof (sain)) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "bind on port %d failed (%s)",
        GB_TCP_PORT, strerror (errno));
    goto out;
  }

  transp = svctcp_create(sockfd, 0, 0);
  if (!transp) {
    LOG("mgmt", GB_LOG_ERROR,
        "RPC service transport create failed for tcp (%s)",
        strerror (errno));
    goto out;
  }

	if (!svc_register(transp, GLUSTER_BLOCK, GLUSTER_BLOCK_VERS,
                    gluster_block_1, IPPROTO_TCP)) {
		LOG("mgmt", GB_LOG_ERROR,
        "unable to register (GLUSTER_BLOCK, GLUSTER_BLOCK_VERS: %s)",
        strerror (errno));
    goto out;
	}

  svc_run ();

 out:
  if (transp) {
    svc_destroy(transp);
  }

  if (sockfd != -1) {
    close(sockfd);
  }

  return NULL;
}


int
main (int argc, char **argv)
{
  int fd;
  pthread_t cli_thread;
  pthread_t server_thread;
  struct flock lock = {0, };
  int errnosv = 0;


  if (!glusterBlockLogdirCreate()) {
    return -1;
  }

  /* is gluster-blockd running ? */
  fd = creat(GB_LOCK_FILE, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    LOG("mgmt", GB_LOG_ERROR, "creat(%s) failed[%s]",
        GB_LOCK_FILE, strerror(errno));
    goto out;
  }

  lock.l_type = F_WRLCK;
  if (fcntl(fd, F_SETLK, &lock) == -1) {
    errnosv = errno;
    LOG("mgmt", GB_LOG_ERROR, "%s",
        "gluster-blockd is already running...");
    close(fd);
    exit(errnosv);
  }

  /* set signal */
  signal(SIGPIPE, SIG_IGN);

  pmap_unset(GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS);
  pmap_unset(GLUSTER_BLOCK, GLUSTER_BLOCK_VERS);

  pthread_create(&cli_thread, NULL, glusterBlockCliThreadProc, NULL);
  pthread_create(&server_thread, NULL, glusterBlockServerThreadProc, NULL);

  pthread_join(cli_thread, NULL);
  pthread_join(server_thread, NULL);

  LOG("mgmt", GB_LOG_ERROR, "svc_run returned (%s)", strerror (errno));

  lock.l_type = F_UNLCK;
  if (fcntl(fd, F_SETLK, &lock) == -1) {
    LOG("mgmt", GB_LOG_ERROR, "fcntl(UNLCK) on pidfile %s failed[%s]",
        GB_LOCK_FILE, strerror(errno));
  }

  close(fd);

 out:
  exit (1);
  /* NOTREACHED */
}
