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
# include  <rpc/rpc.h>
# include  <rpc/pmap_clnt.h>
# include  <signal.h>
# include  <sys/utsname.h>
# include  <linux/version.h>
# include  <sys/wait.h>

# include  "common.h"
# include  "lru.h"
# include  "block.h"
# include  "block_svc.h"
# include  "capabilities.h"
# include  "version.h"

# define   GB_TGCLI_GLOBALS     "targetcli set "                               \
                                "global auto_add_default_portal=false "        \
                                "auto_enable_tpgt=false loglevel_file=info "   \
                                "logfile=%s auto_save_on_exit=false "          \
                                "max_backup_files=100"

# define   GB_DISTRO_CHECK      "grep -P '(^ID=)' /etc/os-release"


gbProcessCtx gbCtx = GB_DAEMON_MODE; /* set process mode */

extern const char *argp_program_version;
static gbConfig *gbCfg;

struct timeval TIMEOUT = {CLI_TIMEOUT_DEF, 0};  /* remote rpc call timeout, 5 mins */

typedef struct globalData {
  int    pfd;  /* pidfile file descriptor */
  pid_t  chpid;
} globalData;

globalData ctx = {0,};


void
glusterBlockCleanGlobals(void)
{
  struct flock lock = {0, };


  lock.l_type = F_UNLCK;
  if (fcntl(ctx.pfd, F_SETLK, &lock) == -1) {
    LOG("mgmt", GB_LOG_ERROR, "fcntl(UNLCK) on pidfile %s failed[%s]",
        GB_LOCK_FILE, strerror(errno));
  }

  if (ctx.pfd) {
    close(ctx.pfd);
  }
  glusterBlockDestroyConfig(gbCfg);
}


void
onSigServerHandler(int signum)
{
  LOG("mgmt", GB_LOG_DEBUG,
      "server process with (pid: %u) received (signal: %s)",
      getpid(), strsignal(signum));

  svc_exit();

  return;
}


void
onSigCliHandler(int signum)
{
  LOG("mgmt", GB_LOG_DEBUG,
      "cli process with (pid: %u) received (signal: %s)",
      getpid(), strsignal(signum));

  kill(ctx.chpid, signum);  /* Pass the signal to server process */

  svc_exit();

  return;
}


static void
glusterBlockDHelp(void)
{
  MSG(stdout,
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
     );
}


void
glusterBlockCliProcess(void)
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

  saun.sun_family = AF_UNIX;
  GB_STRCPYSTATIC(saun.sun_path, GB_UNIX_ADDRESS);

  if (unlink(GB_UNIX_ADDRESS) && errno != ENOENT) {
    LOG("mgmt", GB_LOG_ERROR, "unlink(%s) failed (%s)",
        GB_UNIX_ADDRESS, strerror (errno));
    goto out;
  }

#ifndef HAVE_LIBTIRPC
  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, IPPROTO_IP)) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "UNIX socket creation failed (%s)",
        strerror (errno));
    goto out;
  }

  if (bind(sockfd, (struct sockaddr *) &saun,
           sizeof(struct sockaddr_un)) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "bind on '%s' failed (%s)",
        GB_UNIX_ADDRESS, strerror (errno));
    goto out;
  }
#endif  /* HAVE_LIBTIRPC */

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

  return;
}


void
glusterBlockServerProcess(void)
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

#ifdef HAVE_LIBTIRPC
  if (listen(sockfd, 128) < 0) {
    snprintf(errMsg, sizeof (errMsg), "listen on port %d failed (%s)",
             GB_TCP_PORT, strerror (errno));
    goto out;
  }
#endif  /* HAVE_LIBTIRPC */

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
    MSG(stderr, "%s", errMsg);
    exit(EXIT_FAILURE);
  }

  exit(EXIT_SUCCESS);
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
      MSG(stderr, "unknown option: %s", options[optind-1]);
      return -1;
    }

    switch (opt) {
    case GB_DAEMON_HELP:
    case GB_DAEMON_USAGE:
      if (count != 2) {
        MSG(stderr, "undesired options for: '%s'", options[optind-1]);
        ret = -1;
      }
      glusterBlockDHelp();
      exit(ret);

    case GB_DAEMON_VERSION:
      if (count != 2) {
        MSG(stderr, "undesired options for: '%s'", options[optind-1]);
        ret = -1;
      }
      MSG(stdout, "%s", argp_program_version);
      exit(ret);

    case GB_DAEMON_GLFS_LRU_COUNT:
      if (count - optind  < 1) {
        MSG(stderr, "option '%s' needs argument <COUNT>", options[optind-1]);
        return -1;
      }
      if (sscanf(options[optind], "%zu", &lruCount) != 1) {
        MSG(stderr, "option '%s' expect argument type integer <COUNT>",
            options[optind-1]);
        return -1;
      }

      if (glusterBlockSetLruCount(lruCount)) {
        return -1;
      }
      break;

    case GB_DAEMON_LOG_LEVEL:
      if (count - optind  < 1) {
        MSG(stderr, "option '%s' needs argument <LOG-LEVEL>", options[optind-1]);
        return -1;
      }
      logLevel = blockLogLevelEnumParse(options[optind]);
      ret = glusterBlockSetLogLevel(logLevel);
      if (ret) {
        return ret;
      }
      break;

    case GB_DAEMON_NO_REMOTE_RPC:
      gbConf->noRemoteRpc = true;
      break;

    }

    optind++;
  }

  return 0;
}


static void
gbMinKernelVersionCheck(void)
{
  struct utsname verStr = {{0},};
  char *distro = NULL;
  size_t vNum[VERNUM_BUFLEN] = {0,};
  int i = 0;
  char *tptr;


  distro = gbRunnerGetOutput(GB_DISTRO_CHECK);
  if (!distro) {
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

  GB_FREE(distro);
  return;

 out:
  LOG("mgmt", GB_LOG_ERROR,
      "Distro %s. Minimum recommended kernel version: '%s' and "
      "current kernel version: '%s'. Hint: Upgrade your kernel and try again.",
      distro, tptr, verStr.release);

 fail:
  GB_FREE(distro);

  exit(EXIT_FAILURE);
}


static void
gbDependenciesVersionCheck(void)
{
  char *out = NULL;


  out = gbRunnerGetPkgVersion(TCMU_STR);
  if (!gbDependencyVersionCompare(TCMURUNNER, out)) {
    LOG ("mgmt", GB_LOG_ERROR,
         "current tcmu-runner version is %s, gluster-block need atleast - %s",
         out, GB_MIN_TCMURUNNER_VERSION);
    goto out;
  }
  LOG("mgmt", GB_LOG_INFO, "starting with tcmu-runner version - %s", out);
  GB_FREE(out);

  out = gbRunnerGetPkgVersion(TARGETCLI_STR);
  if (!gbDependencyVersionCompare(TARGETCLI, out)) {
    LOG ("mgmt", GB_LOG_ERROR,
         "current targetcli version is %s, gluster-block need atleast - %s",
         out, GB_MIN_TARGETCLI_VERSION);
    goto out;
  }
  LOG("mgmt", GB_LOG_INFO, "starting with targetcli version - %s", out);
  GB_FREE(out);

  out = gbRunnerGetPkgVersion(RTSLIB_STR);
  if (out[0]) {
    LOG("mgmt", GB_LOG_INFO, "starting with rtslib version - %s", out);
  } else {
    LOG("mgmt", GB_LOG_INFO, "starting with rtslib version <= 2.1.69");
  }
  GB_FREE(out);

  out = gbRunnerGetPkgVersion(CONFIGSHELL_STR);
  if (out[0]) {
    LOG("mgmt", GB_LOG_INFO, "starting with configshell version - %s", out);
  } else {
    LOG("mgmt", GB_LOG_INFO, "starting with configshell version < 1.1.25");
  }
  GB_FREE(out);

  return;

 out:
  GB_FREE(out);
  LOG("mgmt", GB_LOG_ERROR, "Please install the recommended dependency version and try again.");
  exit(EXIT_FAILURE);
}


static int
blockNodeSanityCheck(void)
{
  int ret;
  bool use_targetclid = true;
  char *global_opts;
  char *tmp;


  /*
   * Systemd service will help to guarantee that the tcmu-runner
   * is already up when reaching here
   */
#ifndef USE_SYSTEMD
  /* Check if tcmu-runner is running */
  ret = gbRunner("ps aux ww | grep -w '[t]cmu-runner' > /dev/null");
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "tcmu-runner not running");
    return ESRCH;
  }
#endif

  /* Check targetcli has user:glfs handler listed */
  ret = gbRunner("targetcli /backstores/user:glfs ls > /dev/null");
  if (ret == EKEYEXPIRED) {
    LOG("mgmt", GB_LOG_ERROR,
        "targetcli not found, please install targetcli and try again.");
    return EKEYEXPIRED;
  } else if (ret) {
    LOG("mgmt", GB_LOG_ERROR,
        "tcmu-runner running, but targetcli doesn't list user:glfs handler");
    return  ENODEV;
  }

  /* Check minimum recommended kernel version */
  gbMinKernelVersionCheck();

  /* Check if dependencies meet minimum recommended versions */
  gbDependenciesVersionCheck();

  if (GB_ASPRINTF(&global_opts, GB_TGCLI_GLOBALS, gbConf->configShellLogFile) == -1) {
    return ENOMEM;
  }

  tmp = gbRunnerGetPkgVersion(TARGETCLI_STR);
  if (!gbDependencyVersionCompare(TARGETCLI_DAEMON, tmp)) {
    use_targetclid = false;
    LOG("mgmt", GB_LOG_WARNING,
        "targetclid required: %s, current version: %s, using cli mode",
        GB_MIN_TARGETCLI_DAEMON_VERSION, tmp);
  }
  GB_FREE(tmp);

  if (use_targetclid) {
    tmp = global_opts;
    /* Check if targetclid is running */
    ret = gbRunner("ps aux ww | grep -w '[t]argetclid' > /dev/null");
    if (ret) {
      LOG("mgmt", GB_LOG_WARNING, "targetclid not running, using targetcli");
      if (GB_ASPRINTF(&global_opts, "targetcli --disable-daemon; %s", tmp) == -1) {
        GB_FREE(tmp);
        return ENOMEM;
      }
    } else {
      ret = gbRunner("targetcli set global | grep daemon_use_batch_mode");
      if (!ret) {
        if (GB_ASPRINTF(&global_opts, "%s auto_use_daemon=true daemon_use_batch_mode=true", tmp) == -1) {
          GB_FREE(tmp);
          return ENOMEM;
        }
      } else {
        if (GB_ASPRINTF(&global_opts, "%s auto_use_daemon=true", tmp) == -1) {
          GB_FREE(tmp);
          return ENOMEM;
        }
      }
    }
    GB_FREE(tmp);
  }

  /* Set targetcli globals */
  ret = gbRunner(global_opts);
  GB_FREE(global_opts);
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "targetcli set global attr failed");
    return  -1;
  }

  return 0;
}


static int
initDaemonCapabilities(void)
{

  gbSetCapabilties();
  if (!globalCapabilities) {
    LOG("mgmt", GB_LOG_ERROR, "capabilities fetching failed");
    return -1;
  }

  return 0;
}


int
main (int argc, char **argv)
{
  struct flock lock = {0, };
  int wstatus;


  if (initGbConfig()) {
    exit(EXIT_FAILURE);
  }

  if(initLogging()) {
    goto out;
  }

  fetchGlfsVolServerFromEnv();

  gbCfg = glusterBlockSetupConfig();
  if (!gbCfg) {
    LOG("mgmt", GB_LOG_ERROR, "glusterBlockSetupConfig() failed");
    goto out;
  }

  if (glusterBlockDParseArgs(argc, argv)) {
    LOG("mgmt", GB_LOG_ERROR, "glusterBlockDParseArgs() failed");
    goto out;
  }

  /* is gluster-blockd running ? */
  ctx.pfd = creat(GB_LOCK_FILE, S_IRUSR | S_IWUSR);
  if (ctx.pfd == -1) {
    LOG("mgmt", GB_LOG_ERROR, "creat(%s) failed[%s]",
        GB_LOCK_FILE, strerror(errno));
    goto out;
  }

  lock.l_type = F_WRLCK;
  if (fcntl(ctx.pfd, F_SETLK, &lock) == -1) {
    LOG("mgmt", GB_LOG_ERROR, "gluster-blockd is already running...");
    goto out;
  }

  if (!gbConf->noRemoteRpc) {
    if (blockNodeSanityCheck()) {
      goto out;
    }

    if (initDaemonCapabilities()) {
      goto out;
    }
    LOG("mgmt", GB_LOG_INFO, "capabilities fetched successfully");
  } else {
    LOG("mgmt", GB_LOG_DEBUG, "gluster-blockd running in noRemoteRpc mode");
  }

  initCache();

  /* set signal */
  signal(SIGPIPE, SIG_IGN);

  pmap_unset(GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS);
  if (!gbConf->noRemoteRpc) {
    pmap_unset(GLUSTER_BLOCK, GLUSTER_BLOCK_VERS);
  }

  ctx.chpid = fork();
  if (ctx.chpid == -1) {
    LOG("mgmt", GB_LOG_ERROR, "failed forking: (%s)", strerror(errno));
    goto out;
  } else if (ctx.chpid == 0) {
    LOG("mgmt", GB_LOG_INFO, "server process pid: (%u)", getpid());

    /* Handle signals */
    signal(SIGINT,  onSigServerHandler);
    signal(SIGTERM, onSigServerHandler);

    glusterBlockServerProcess();

    exit (EXIT_FAILURE); /* server process svc_run exits */
  } else {
    LOG("mgmt", GB_LOG_INFO, "cli process pid: (%u)", getpid());

    /* Handle signals */
    signal(SIGINT,  onSigCliHandler);
    signal(SIGTERM, onSigCliHandler);
    signal(SIGALRM, onSigCliHandler);

    glusterBlockCliProcess();

    /* wait for server process to exit */
    waitpid(ctx.chpid, &wstatus, 0);

    glusterBlockCleanGlobals();

    if (WIFEXITED(wstatus)) {  /* did server process terminated normally ? */
      LOG("mgmt", GB_LOG_DEBUG,
          "exit status of server process was (%d)", WEXITSTATUS(wstatus));

      LOG("mgmt", GB_LOG_CRIT, "Exiting ...");

      if (WEXITSTATUS(wstatus) == EXIT_SUCCESS) {
        exit(EXIT_SUCCESS);
      }
    }

    exit(EXIT_FAILURE);
  }

 out:
  finiGbConfig();

  glusterBlockCleanGlobals();
  exit (EXIT_FAILURE);
  /* NOTREACHED */
}
