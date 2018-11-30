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
# include  <sys/utsname.h>
# include  <linux/version.h>

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

# define   VERNUM_BUFLEN        8

# define   GB_DISTRO_CHECK      "grep -P '(^ID=)' /etc/os-release"

extern const char *argp_program_version;
static gbConfig *gbCfg;

struct timeval TIMEOUT = {CLI_TIMEOUT_DEF, 0};  /* remote rpc call timeout, 5 mins */

static void
glusterBlockDHelp(void)
{
  MSG(stdout, "%s",
      "gluster-blockd ("PACKAGE_VERSION")\n"
      "usage:\n"
      "  gluster-blockd [--glfs-lru-count <COUNT>]\n"
      "                 [--log-level <LOGLEVEL>]\n"
      "                 [--no-remote-rpc]\n"
      "\n"
      "commands:\n"
      "  --glfs-lru-count <COUNT>\n"
      "        Glfs objects cache capacity [max: 512] [default: 5]\n"
      "  --log-level <LOGLEVEL>\n"
      "        Logging severity. Valid options are,\n"
      "        TRACE, DEBUG, INFO, WARNING, ERROR, CRIT and NONE [default: INFO]\n"
      "  --no-remote-rpc\n"
      "        Ignore remote rpc communication, capabilities check and\n"
      "        other node sanity checks\n"
      "  --help\n"
      "        Show this message and exit.\n"
      "  --version\n"
      "        Show version info and exit.\n"
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
    MSG(stderr, "%s\n", errMsg);
    exit(EXIT_FAILURE);
  }
  return NULL;
}


static int
glusterBlockDParseArgs(int count, char **options)
{
  size_t optind = 1;
  size_t opt = 0;
  ssize_t lruCount;
  int ret = 0;
  int logLevel;


  while (optind < count) {
    opt = glusterBlockDaemonOptEnumParse(options[optind++]);
    if (!opt || opt >= GB_DAEMON_OPT_MAX) {
      MSG(stderr, "unknown option: %s\n", options[optind-1]);
      return -1;
    }

    switch (opt) {
    case GB_DAEMON_HELP:
    case GB_DAEMON_USAGE:
      if (count != 2) {
        MSG(stderr, "undesired options for: '%s'\n", options[optind-1]);
        ret = -1;
      }
      glusterBlockDHelp();
      exit(ret);

    case GB_DAEMON_VERSION:
      if (count != 2) {
        MSG(stderr, "undesired options for: '%s'\n", options[optind-1]);
        ret = -1;
      }
      MSG(stdout, "%s\n", argp_program_version);
      exit(ret);

    case GB_DAEMON_GLFS_LRU_COUNT:
      if (count - optind  < 1) {
        MSG(stderr, "option '%s' needs argument <COUNT>\n", options[optind-1]);
        return -1;
      }
      if (sscanf(options[optind], "%zu", &lruCount) != 1) {
        MSG(stderr, "option '%s' expect argument type integer <COUNT>\n",
            options[optind-1]);
        return -1;
      }

      if (glusterBlockSetLruCount(lruCount)) {
        return -1;
      }
      break;

    case GB_DAEMON_LOG_LEVEL:
      if (count - optind  < 1) {
        MSG(stderr, "option '%s' needs argument <LOG-LEVEL>\n", options[optind-1]);
        return -1;
      }
      logLevel = blockLogLevelEnumParse(options[optind]);
      ret = glusterBlockSetLogLevel(logLevel);
      if (ret) {
        return ret;
      }
      break;

    case GB_DAEMON_NO_REMOTE_RPC:
      gbConf.noRemoteRpc = true;
      break;

    }

    optind++;
  }

  return 0;
}


static void
gbMinKernelVersionCheck(void)
{
  struct utsname verStr = {'\0', };
  char distro[32] = {'\0', };
  size_t vNum[VERNUM_BUFLEN] = {0, };
  FILE *fp = NULL;
  int i = 0;
  char *tptr;


  fp = popen(GB_DISTRO_CHECK, "r");
  if (fp) {
    size_t newLen = fread(distro, sizeof(char), 32, fp);
    if (ferror(fp)) {
      LOG("mgmt", GB_LOG_ERROR, "fread(%s) failed: %s",
          GB_DISTRO_CHECK, strerror(errno));
      goto fail;
    }
    distro[newLen++] = '\0';
    tptr = strchr(distro,'\n');
    if (tptr) {
      *tptr = '\0';
    }
  } else {
    LOG("mgmt", GB_LOG_ERROR, "popen(%s): failed: %s",
        GB_DISTRO_CHECK, strerror(errno));
    goto fail;
  }

  if (uname(&verStr) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "uname() failed: %s", strerror(errno));
    goto fail;
  }

  tptr = verStr.release;
  while (*tptr) {
    if (isdigit(*tptr)) {
      vNum[i] = strtol(tptr, &tptr, 10);
      i++;
    } else if (isalpha(*tptr)) {
      break;
    } else {
      tptr++;
    }

    if (i >= VERNUM_BUFLEN) {
      break;
    }
  }

  if (strstr(distro, "fedora")) {
    tptr = "4.12.0-1"; /* Minimum recommended fedora kernel version */
    if (KERNEL_VERSION(vNum[0], vNum[1], vNum[2]) < KERNEL_VERSION(4, 12, 0)) {
      goto out;
    } else if (KERNEL_VERSION(vNum[0], vNum[1], vNum[2]) == KERNEL_VERSION(4, 12, 0)) {
      if (KERNEL_VERSION(vNum[3], vNum[4], vNum[5]) < KERNEL_VERSION(1, 0, 0)) {
        goto out;
      }
    }
  } else if (strstr(distro, "rhel")) {
    tptr = "3.10.0-862.14.4"; /* Minimum recommended rhel kernel version */
    if (KERNEL_VERSION(vNum[0], vNum[1], vNum[2]) < KERNEL_VERSION(3, 10, 0)) {
      goto out;
    } else if (KERNEL_VERSION(vNum[0], vNum[1], vNum[2]) == KERNEL_VERSION(3, 10, 0)) {
      if (KERNEL_VERSION(vNum[3], vNum[4], vNum[5]) < KERNEL_VERSION(862, 14, 4)) {
        goto out;
      }
    }
  } else {
    LOG("mgmt", GB_LOG_INFO, "Distro %s. Skipping kernel version check.", distro);
  }

  LOG("mgmt", GB_LOG_INFO, "Distro %s. Current kernel version: '%s'.",
      distro, verStr.release);

  pclose(fp);
  return;

 out:
  LOG("mgmt", GB_LOG_ERROR,
      "Distro %s. Minimum recommended kernel version: '%s' and "
      "current kernel version: '%s'. Hint: Upgrade your kernel and try again.",
      distro, tptr, verStr.release);

 fail:
  pclose(fp);

  exit(EXIT_FAILURE);
}


static int
blockNodeSanityCheck(void)
{
  int ret;
  char *global_opts;


  /* Check minimum recommended kernel version */
  gbMinKernelVersionCheck();

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


  if (pthread_mutex_init(&gbConf.lock, NULL) < 0) {
    exit(EXIT_FAILURE);
  }

  if(initLogging()) {
    exit(EXIT_FAILURE);
  }

  fetchGlfsVolServerFromEnv();

  gbCfg = glusterBlockSetupConfig(NULL);
  if (!gbCfg) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glusterBlockSetupConfig() failed");
    return -1;
  }

  if (glusterBlockDParseArgs(argc, argv)) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glusterBlockDParseArgs() failed");
    goto out;
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

  if (!gbConf.noRemoteRpc) {
    errnosv = blockNodeSanityCheck();
    if (errnosv) {
      exit(errnosv);
    }

    if (initDaemonCapabilities()) {
      exit(EXIT_FAILURE);
    }
    LOG("mgmt", GB_LOG_INFO, "%s", "capabilities fetched successfully");
  } else {
    LOG("mgmt", GB_LOG_DEBUG, "%s", "gluster-blockd running in noRemoteRpc mode");
  }

  initCache();

  /* set signal */
  signal(SIGPIPE, SIG_IGN);

  pmap_unset(GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS);
  if (!gbConf.noRemoteRpc) {
    pmap_unset(GLUSTER_BLOCK, GLUSTER_BLOCK_VERS);
  }

  pthread_create(&cli_thread, NULL, glusterBlockCliThreadProc, NULL);
  if (!gbConf.noRemoteRpc) {
    pthread_create(&server_thread, NULL, glusterBlockServerThreadProc, NULL);
    pthread_join(server_thread, NULL);
  }
  pthread_join(cli_thread, NULL);


  LOG("mgmt", GB_LOG_ERROR, "svc_run returned (%s)", strerror (errno));

  lock.l_type = F_UNLCK;
  if (fcntl(fd, F_SETLK, &lock) == -1) {
    LOG("mgmt", GB_LOG_ERROR, "fcntl(UNLCK) on pidfile %s failed[%s]",
        GB_LOCK_FILE, strerror(errno));
  }

  close(fd);

 out:
  glusterBlockDestroyConfig(gbCfg);
  exit (1);
  /* NOTREACHED */
}
