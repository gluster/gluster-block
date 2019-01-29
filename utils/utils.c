/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  <dirent.h>
# include  <sys/stat.h>

# include "utils.h"
# include "lru.h"
# include "config.h"

struct gbConf gbConf = {
  .glfsLruCount = LRU_COUNT_DEF,
  .logLevel = GB_LOG_INFO,
  .logDir = GB_LOGDIR,
  .cliTimeout = CLI_TIMEOUT_DEF
};

const char *argp_program_version = ""                                 \
  PACKAGE_NAME" ("PACKAGE_VERSION")"                                  \
  "\nRepository rev: https://github.com/gluster/gluster-block.git\n"  \
  "Copyright (c) 2016 Red Hat, Inc. <https://redhat.com/>\n"          \
  "gluster-block comes with ABSOLUTELY NO WARRANTY.\n"                \
  "It is licensed to you under your choice of the GNU Lesser\n"       \
  "General Public License, version 3 or any later version (LGPLv3\n"  \
  "or later), or the GNU General Public License, version 2 (GPLv2),\n"\
  "in all cases as published by the Free Software Foundation.";


int
glusterBlockSetLogLevel(unsigned int logLevel)
{
  if (logLevel >= GB_LOG_MAX) {
    MSG(stderr, "unknown LOG-LEVEL: '%d'\n", logLevel);
    return -1;
  }
  LOCK(gbConf.lock);
  gbConf.logLevel = logLevel;
  UNLOCK(gbConf.lock);
  LOG("mgmt", GB_LOG_INFO,
      "logLevel now is %s\n", LogLevelLookup[logLevel]);

  return 0;
}


/* TODO: use gbConf in cli too, for logLevel/LogDir and other future options
int
glusterBlockSetCliTimeout(size_t timeout)
{
  if (timeout < 0) {
    MSG(stderr, "unknown GB_CLI_TIMEOUT: '%zu'\n", timeout);
    return -1;
  }
  LOCK(gbConf.lock);
  gbConf.cliTimeout = timeout;
  UNLOCK(gbConf.lock);

  return 0;
}
*/


int
glusterBlockCLIOptEnumParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_CLI_OPT_MAX;
  }

  for (i = 0; i < GB_CLI_OPT_MAX; i++) {
    if (!strcmp(opt, gbCliCmdlineOptLookup[i])) {
      return i;
    }
  }

  return i;
}


int
glusterBlockCLICreateOptEnumParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_CLI_CREATE_OPT_MAX;
  }

  for (i = 0; i < GB_CLI_CREATE_OPT_MAX; i++) {
    if (!strcmp(opt, gbCliCreateOptLookup[i])) {
      return i;
    }
  }

  return i;
}


int
glusterBlockDaemonOptEnumParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_DAEMON_OPT_MAX;
  }

  for (i = 0; i < GB_DAEMON_OPT_MAX; i++) {
    /* clip '--' from option */
    while (*opt == '-') {
      opt++;
    }
    if (!strcmp(opt, gbDaemonCmdlineOptLookup[i])) {
      return i;
    }
  }

  return i;
}


int
blockLogLevelEnumParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_LOG_MAX;
  }

  for (i = 0; i < GB_LOG_MAX; i++) {
    if (!strcmp(opt, LogLevelLookup[i])) {
      return i;
    }
  }

  return i;
}


int
blockMetaKeyEnumParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_METAKEY_MAX;
  }

  for (i = 0; i < GB_METAKEY_MAX; i++) {
    if (!strcmp(opt, MetakeyLookup[i])) {
      return i;
    }
  }

  return i;
}


int
blockMetaStatusEnumParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_METASTATUS_MAX;
  }

  for (i = 0; i < GB_METASTATUS_MAX; i++) {
    if (!strcmp(opt, MetaStatusLookup[i])) {
      return i;
    }
  }

  return i;
}

int blockRemoteCreateRespEnumParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_REMOTE_CREATE_RESP_MAX;
  }

  for (i = 0; i < GB_REMOTE_CREATE_RESP_MAX; i++) {
    if (strstr(opt, RemoteCreateRespLookup[i])) {
      return i;
    }
  }

  return i;
}


/* On any failure return, epoch atleast */
void
logTimeNow(char *buf, size_t bufSize)
{
  struct tm tm;
  struct timeval tv;


  if (gettimeofday (&tv, NULL) < 0) {
    goto out;
  }

  if (tv.tv_sec && gmtime_r(&tv.tv_sec, &tm) != NULL) {
    strftime (buf, bufSize, "%Y-%m-%d %H:%M:%S", &tm);
    snprintf (buf + strlen(buf), bufSize - strlen(buf), ".%06ld", tv.tv_usec);
    return;
  }

out:
  snprintf(buf, bufSize, "%lu", (unsigned long)time(NULL));
  return;
}


static bool
glusterBlockLogdirCreate(void)
{
  DIR* dir = opendir(gbConf.logDir);


  if (dir) {
    closedir(dir);
  } else if (errno == ENOENT) {
    if (mkdir(gbConf.logDir, 0755) == -1) {
      fprintf(stderr, "mkdir(%s) failed (%s)", gbConf.logDir, strerror (errno));
      return 0;  /* False */
    }
  } else {
    fprintf(stderr, "opendir(%s) failed (%s)", gbConf.logDir, strerror (errno));
    return 0;  /* False */
  }

  return 1;
}


void fetchGlfsVolServerFromEnv()
{
  char *volServer;


  volServer = getenv("GB_BHV_VOLSERVER");
  if (!volServer) {
    volServer = "localhost";
  }
  snprintf(gbConf.volServer, HOST_NAME_MAX, "%s", volServer);

  LOG("mgmt", GB_LOG_INFO, "Block Hosting Volfile Server Set to: %s", gbConf.volServer);
}


int
initLogging(void)
{
  char *logDir = NULL;


  logDir = getenv("GB_LOGDIR");
  if (!logDir) {
    logDir = GB_LOGDIR;
  }

  if (strlen(logDir) > PATH_MAX - GB_MAX_LOGFILENAME) {
    fprintf(stderr, "strlen of logDir Path > PATH_MAX: %s\n", logDir);
    return EXIT_FAILURE;
  }

  /* set logfile paths */
  snprintf(gbConf.logDir, PATH_MAX,
           "%s", logDir);
  snprintf(gbConf.daemonLogFile, PATH_MAX,
           "%s/gluster-blockd.log", logDir);
  snprintf(gbConf.cliLogFile, PATH_MAX,
           "%s/gluster-block-cli.log", logDir);
  snprintf(gbConf.gfapiLogFile, PATH_MAX,
           "%s/gluster-block-gfapi.log", logDir);
  snprintf(gbConf.configShellLogFile, PATH_MAX,
           "%s/gluster-block-configshell.log", logDir);
  snprintf(gbConf.cmdhistoryLogFile, PATH_MAX,
           "%s/cmd_history.log", logDir);

  if(!glusterBlockLogdirCreate()) {
    return EXIT_FAILURE;
  }

  return 0;
}


int
gbRunnerExitStatus(int exitStatus)
{
  if (!WIFEXITED(exitStatus)) {
    return -1;
  }

  return WEXITSTATUS(exitStatus);
}


int
gbRunner(char *cmd)
{
  int childExitStatus;


  childExitStatus = system(cmd);

  return gbRunnerExitStatus(childExitStatus);
}


int
gbAlloc(void *ptrptr, size_t size,
        const char *filename, const char *funcname, size_t linenr)
{
  *(void **)ptrptr = calloc(1, size);

  if (*(void **)ptrptr == NULL) {
    errno = ENOMEM;
    return -1;
  }

  return 0;
}


int
gbAllocN(void *ptrptr, size_t size, size_t count,
         const char *filename, const char *funcname, size_t linenr)
{
  *(void**)ptrptr = calloc(count, size);

  if (*(void**)ptrptr == NULL) {
    errno = ENOMEM;
    return -1;
  }

  return 0;
}


int
gbReallocN(void *ptrptr, size_t size, size_t count,
         const char *filename, const char *funcname, size_t linenr)
{
  void *tmp;


  if (xalloc_oversized(count, size)) {
    errno = ENOMEM;
    return -1;
  }
  tmp = realloc(*(void**)ptrptr, size * count);
  if (!tmp && ((size * count) != 0)) {
    errno = ENOMEM;
    return -1;
  }
  *(void**)ptrptr = tmp;

  return 0;
}


void
gbFree(void *ptrptr)
{
  int save_errno = errno;


  if(*(void**)ptrptr == NULL) {
   return;
  }

  free(*(void**)ptrptr);
  *(void**)ptrptr = NULL;
  errno = save_errno;
}


int
gbStrdup(char **dest, const char *src,
         const char *filename, const char *funcname, size_t linenr)
{
  *dest = NULL;

  if (!src) {
    return 0;
  }

  if (!(*dest = strdup(src))) {
    return -1;
  }

  return 0;
}


char *
gbStrcpy(char *dest, const char *src, size_t destbytes,
         const char *filename, const char *funcname, size_t linenr)
{
    char *ret;
    size_t n = strlen(src);

    if (n > (destbytes - 1))
      return NULL;

    ret = strncpy(dest, src, n);
    /* strncpy NULL terminates if the last character is \0.  Therefore
     * force the last byte to be \0
     */
    dest[n] = '\0';

    return ret;
}
