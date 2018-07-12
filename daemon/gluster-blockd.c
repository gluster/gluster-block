/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

# define   _GNU_SOURCE
# include  <fcntl.h>
# include  <dirent.h>
# include  <sys/stat.h>
# include  <pthread.h>
# include  <rpc/pmap_clnt.h>
# include  <signal.h>

# include  "config.h"
# include  "common.h"
# include  "lru.h"
# include  "block.h"
# include  "block_svc.h"
# include  "capabilities.h"

# define   GB_TGCLI_GLOBALS     "targetcli set "                               \
                                "global auto_add_default_portal=false "        \
                                "auto_enable_tpgt=false loglevel_file=info "   \
                                "logfile=%s auto_save_on_exit=false"


extern size_t glfsLruCount;
extern const char *argp_program_version;


static void
glusterBlockDHelp(void)
{
  MSG("%s",
      "gluster-blockd ("PACKAGE_VERSION")\n"
      "usage:\n"
      "  gluster-blockd [--glfs-lru-count <COUNT>] [--log-level <LOGLEVEL>]\n"
      "\n"
      "commands:\n"
      "  --glfs-lru-count <COUNT>\n"
      "        glfs objects cache capacity [max: 512] [default: 5]\n"
      "  --log-level <LOGLEVEL>\n"
      "        Logging severity. Valid options are,\n"
      "        TRACE, DEBUG, INFO, WARNING, ERROR and NONE [default: INFO]\n"
      "  --help\n"
      "        show this message and exit.\n"
      "  --version\n"
      "        show version info and exit.\n"
      "\n"
     );
}


void *
glusterBlockCliThreadProc (void *vargp)
{
  register SVCXPRT *transp = NULL;
  struct sockaddr_un saun = {0, };
  int sockfd = RPC_ANYSOCK;


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
  GB_STRCPYSTATIC(saun.sun_path, GB_UNIX_ADDRESS);

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

  if (sockfd != RPC_ANYSOCK) {
    close(sockfd);
  }

  return NULL;
}


void *
glusterBlockServerThreadProc(void *vargp)
{
  register SVCXPRT *transp = NULL;
  struct sockaddr_in sain = {0, };
  int sockfd = RPC_ANYSOCK;
  int opt = 1;
  char errMsg[2048] = {0};


  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    snprintf(errMsg, sizeof (errMsg), "TCP socket creation failed (%s)",
             strerror (errno));
    goto out;
  }

  if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)) < 0) {
    snprintf (errMsg, sizeof (errMsg), "Setting option to re-use address "
              "failed (%s)", strerror (errno));
    goto out;
  }

  sain.sin_family = AF_INET;
  sain.sin_addr.s_addr = INADDR_ANY;
  sain.sin_port = htons(GB_TCP_PORT);

  if (bind(sockfd, (struct sockaddr *) &sain, sizeof (sain)) < 0) {
    snprintf(errMsg, sizeof (errMsg), "bind on port %d failed (%s)",
             GB_TCP_PORT, strerror (errno));
    goto out;
  }

  transp = svctcp_create(sockfd, 0, 0);
  if (!transp) {
    snprintf(errMsg, sizeof (errMsg), "%s", "RPC service transport create "
             "failed for tcp");
    goto out;
  }

  if (!svc_register(transp, GLUSTER_BLOCK, GLUSTER_BLOCK_VERS,
                    gluster_block_1, IPPROTO_TCP)) {
    snprintf (errMsg, sizeof (errMsg), "%s", "Please check if rpcbind "
              "service is running.");
    goto out;
  }

  svc_run ();

 out:
  if (transp) {
    svc_destroy(transp);
  }

  if (sockfd != RPC_ANYSOCK) {
    close(sockfd);
  }

  if (errMsg[0]) {
    LOG ("mgmt", GB_LOG_ERROR, "%s", errMsg);
    MSG("%s\n", errMsg);
    exit(EXIT_FAILURE);
  }
  return NULL;
}


static int
glusterBlockDParseArgs(int count, char **options)
{
  size_t optind = 1;
  size_t opt = 0;
  int ret = 0;


  while (optind < count) {
    opt = glusterBlockDaemonOptEnumParse(options[optind++]);
    if (!opt || opt >= GB_DAEMON_OPT_MAX) {
      MSG("unknown option: %s\n", options[optind-1]);
      return -1;
    }

    switch (opt) {
    case GB_DAEMON_HELP:
    case GB_DAEMON_USAGE:
      if (count != 2) {
        MSG("undesired options for: '%s'\n", options[optind-1]);
        ret = -1;
      }
      glusterBlockDHelp();
      exit(ret);

    case GB_DAEMON_VERSION:
      if (count != 2) {
        MSG("undesired options for: '%s'\n", options[optind-1]);
        ret = -1;
      }
      MSG("%s\n", argp_program_version);
      exit(ret);

    case GB_DAEMON_GLFS_LRU_COUNT:
      if (count - optind  < 1) {
        MSG("option '%s' needs argument <COUNT>\n", options[optind-1]);
        return -1;
      }
      if (sscanf(options[optind], "%zu", &glfsLruCount) != 1) {
        MSG("option '%s' expect argument type integer <COUNT>\n",
            options[optind-1]);
        return -1;
      }
      if (!glfsLruCount || (glfsLruCount > LRU_COUNT_MAX)) {
        MSG("glfs-lru-count argument should be [0 < COUNT < %d]\n",
            LRU_COUNT_MAX);
        LOG("mgmt", GB_LOG_ERROR,
            "glfs-lru-count argument should be [0 < COUNT < %d]\n",
            LRU_COUNT_MAX);
        return -1;
      }
      break;

    case GB_DAEMON_LOG_LEVEL:
      if (count - optind  < 1) {
        MSG("option '%s' needs argument <LOG-LEVEL>\n", options[optind-1]);
        return -1;
      }
      gbConf.logLevel = blockLogLevelEnumParse(options[optind]);
      if (gbConf.logLevel >= GB_LOG_MAX) {
        MSG("unknown LOG-LEVEL: '%s'\n", options[optind]);
        return -1;
      }
      break;
    }

    optind++;
  }

  return 0;
}

static int
blockNodeSanityCheck(void)
{
  int ret;
  char *global_opts;


  /* Check if tcmu-runner is running */
  ret = gbRunner("ps aux ww | grep -w '[t]cmu-runner' > /dev/null");
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "tcmu-runner not running");
    return ESRCH;
  }

  /* Check targetcli has user:glfs handler listed */
  ret = gbRunner("targetcli /backstores/user:glfs ls > /dev/null");
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "%s",
        "tcmu-runner running, but targetcli doesn't list user:glfs handler");
    return  ENODEV;
  }

  if (ret == EKEYEXPIRED) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "targetcli not found");
    return EKEYEXPIRED;
  }

  if (GB_ASPRINTF(&global_opts, GB_TGCLI_GLOBALS, gbConf.configShellLogFile) == -1) {
    return ENOMEM;
  }
  /* Set targetcli globals */
  ret = gbRunner(global_opts);
  GB_FREE(global_opts);
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "%s",
        "targetcli set global attr failed");
    return  -1;
  }

  return 0;
}


static int
initDaemonCapabilities(void)
{

  gbSetCapabilties();
  if (!globalCapabilities) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "capabilities fetching failed");
    return -1;
  }

  return 0;
}


int
main (int argc, char **argv)
{
  int fd;
  pthread_t cli_thread;
  pthread_t server_thread;
  struct flock lock = {0, };
  int errnosv = 0;


  if(initLogging()) {
    exit(EXIT_FAILURE);
  }

  if (glusterBlockDParseArgs(argc, argv)) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glusterBlockDParseArgs() failed");
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

  errnosv = blockNodeSanityCheck();
  if (errnosv) {
    exit(errnosv);
  }

  if (initDaemonCapabilities()) {
    exit(EXIT_FAILURE);
  }
  LOG("mgmt", GB_LOG_INFO, "%s", "capabilities fetched successfully");

  initCache();

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
