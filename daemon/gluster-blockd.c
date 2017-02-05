/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  <unistd.h>
# include  <pthread.h>
# include  <rpc/pmap_clnt.h>

# include  "block.h"



void *
glusterBlockCliThreadProc (void *vargp)
{
  register SVCXPRT *transp;
  struct sockaddr_un saun;
  int sockfd, len;


  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, IPPROTO_IP)) < 0) {
    perror("server: socket");
    exit(1);
  }

  saun.sun_family = AF_UNIX;
  strcpy(saun.sun_path, ADDRESS);

  unlink(ADDRESS);
  len = sizeof(saun.sun_family) + strlen(saun.sun_path);

  if (bind(sockfd, (struct sockaddr *) &saun, len) < 0) {
    perror("server: bind");
    exit(1);
  }

  transp = svcunix_create(sockfd, 0, 0, ADDRESS);
  if (transp == NULL) {
    fprintf (stderr, "%s", "cannot create tcp service");
    exit(1);
  }
	
  if (!svc_register(transp, GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS, gluster_block_cli_1, IPPROTO_IP)) {
		fprintf (stderr, "%s", "unable to register (GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS, unix|local).");
		exit(1);
	}

  svc_run ();

  return NULL;
}


void *
glusterBlockServerThreadProc(void *vargp)
{
  register SVCXPRT *transp;
  struct sockaddr_in sain;
  int sockfd;


  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    perror("server: socket");
    exit(1);
  }

  sain.sin_family = AF_INET;
  sain.sin_addr.s_addr = INADDR_ANY;
  sain.sin_port = htons(24006);

  if (bind(sockfd, (struct sockaddr *) &sain, sizeof (sain)) < 0) {
    perror("server: bind");
    exit(1);
  }

  transp = svctcp_create(sockfd, 0, 0);
  if (transp == NULL) {
    fprintf (stderr, "%s", "cannot create tcp service");
    exit(1);
  }

	if (!svc_register(transp, GLUSTER_BLOCK, GLUSTER_BLOCK_VERS, gluster_block_1, IPPROTO_TCP)) {
		fprintf (stderr, "%s", "unable to register (GLUSTER_BLOCK, GLUSTER_BLOCK_VERS, tcp).");
		exit(1);
	}

  svc_run ();

  return NULL;
}


int
main (int argc, char **argv)
{
  pthread_t cli_thread;
  pthread_t server_thread;


	pmap_unset (GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS);
  pmap_unset (GLUSTER_BLOCK, GLUSTER_BLOCK_VERS);

  pthread_create(&cli_thread, NULL, glusterBlockCliThreadProc , NULL);
  pthread_create(&server_thread, NULL, glusterBlockServerThreadProc , NULL);

  pthread_join(cli_thread, NULL);
  pthread_join(server_thread, NULL);


  fprintf (stderr, "%s", "svc_run returned");
  exit (0);
  /* NOTREACHED */
}
