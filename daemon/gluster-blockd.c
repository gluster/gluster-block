/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  <sys/stat.h>
# include  <pthread.h>
# include  <rpc/pmap_clnt.h>

# include  "common.h"
# include  "block.h"



static bool
glusterBlockLogdirCreate(void)
{
  struct stat st = {0};

  if (stat(GB_LOGDIR, &st) == -1) {
    if (mkdir(GB_LOGDIR, 0755) == -1) {
      LOG("mgmt", GB_LOG_ERROR, "mkdir(%s) failed (%s)",
          GB_LOGDIR, strerror (errno));

      return FALSE;
    }
  }

  return TRUE;
}


void *
glusterBlockCliThreadProc (void *vargp)
{
  register SVCXPRT *transp = NULL;
  struct sockaddr_un saun;
  int sockfd, len;


  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, IPPROTO_IP)) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "UNIX socket creation failed (%s)",
        strerror (errno));
    goto out;
  }

  saun.sun_family = AF_UNIX;
  strcpy(saun.sun_path, GB_UNIX_ADDRESS);

  unlink(GB_UNIX_ADDRESS);
  len = sizeof(saun.sun_family) + strlen(saun.sun_path);

  if (bind(sockfd, (struct sockaddr *) &saun, len) < 0) {
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
  struct sockaddr_in sain;
  int sockfd;


  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "TCP socket creation failed (%s)",
        strerror (errno));
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
  pthread_t cli_thread;
  pthread_t server_thread;

  if (glusterBlockLogdirCreate()) {
    return -1;
  }

	pmap_unset(GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS);
  pmap_unset(GLUSTER_BLOCK, GLUSTER_BLOCK_VERS);

  pthread_create(&cli_thread, NULL, glusterBlockCliThreadProc , NULL);
  pthread_create(&server_thread, NULL, glusterBlockServerThreadProc , NULL);

  pthread_join(cli_thread, NULL);
  pthread_join(server_thread, NULL);


  LOG("mgmt", GB_LOG_ERROR, "svc_run returned (%s)",
      strerror (errno));

  exit (errno);
  /* NOTREACHED */
}
