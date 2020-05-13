/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# define   _GNU_SOURCE         /* See feature_test_macros(7) */
# include  <stdio.h>
# include  <dirent.h>
# include  <sys/stat.h>
# include  <pthread.h>
# include  <sys/mman.h>

# include "utils.h"
# include "lru.h"
# include "version.h"

const char *argp_program_version = ""                                 \
  PACKAGE_NAME" ("PACKAGE_VERSION")"                                  \
  "\nRepository rev: https://github.com/gluster/gluster-block.git\n"  \
  "Copyright (c) 2016 Red Hat, Inc. <https://redhat.com/>\n"          \
  "gluster-block comes with ABSOLUTELY NO WARRANTY.\n"                \
  "It is licensed to you under your choice of the GNU Lesser\n"       \
  "General Public License, version 3 or any later version (LGPLv3\n"  \
  "or later), or the GNU General Public License, version 2 (GPLv2),\n"\
  "in all cases as published by the Free Software Foundation.";

struct gbConf *gbConf = NULL;

int
initGbConfig(void)
{
  pthread_mutexattr_t attr;
  int ret;


  if (gbConf)
    return 0;

  if (gbCtx == GB_CLI_MODE) {
    if (GB_ALLOC_N(gbConf, sizeof(gbConf)) < 0) {
      ret = errno;
      MSG(stderr, "Failed to allocate memory for gbConf %s", strerror (errno));
      return ret;
    }

    pthread_mutex_init(&gbConf->lock, NULL);
  } else {
    gbConf = mmap(NULL, sizeof(*gbConf), PROT_READ|PROT_WRITE,
                  MAP_SHARED|MAP_ANON, -1, 0);
    if (gbConf == MAP_FAILED) {
      ret = errno;
      MSG(stderr, "mmap the gbConf failed %s", strerror (errno));
      return ret;
    }

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&gbConf->lock, &attr);
  }

  gbConf->glfsLruCount = LRU_COUNT_DEF;
  gbConf->logLevel = GB_LOG_INFO;
  gbConf->cliTimeout = CLI_TIMEOUT_DEF;

  return 0;
}


void
finiGbConfig(void)
{
  if (!gbConf)
      return;

  pthread_mutex_destroy(&gbConf->lock);

  if (gbCtx == GB_CLI_MODE) {
    GB_FREE(gbConf);
  } else {
    munmap(gbConf, sizeof(*gbConf));
  }
}


int
glusterBlockSetLogLevel(unsigned int logLevel)
{
  char *dom;
  int level;

  if (gbCtx == GB_CLI_MODE) {
    dom = "cli";
    level = GB_LOG_DEBUG;
  } else {
    dom = "mgmt";
    level = GB_LOG_CRIT;
  }

  if (logLevel >= GB_LOG_MAX) {
    LOG(dom, GB_LOG_ERROR, "unknown LOG-LEVEL: '%d'", logLevel);
    return -EINVAL;
  }

  LOCK(gbConf->lock);
  if (gbConf->logLevel == logLevel) {
    UNLOCK(gbConf->lock);
    LOG(dom, GB_LOG_DEBUG,
        "No changes to current logLevel: %s, skipping it.",
        LogLevelLookup[logLevel]);
    return 0;
  }
  gbConf->logLevel = logLevel;
  UNLOCK(gbConf->lock);

  LOG(dom, level, "logLevel now is %s", LogLevelLookup[logLevel]);

  return 0;
}


/* TODO: use gbConf in cli too, for logLevel/LogDir and other future options
int
glusterBlockSetCliTimeout(size_t timeout)
{
  if (timeout < 0) {
    MSG(stderr, "unknown GB_CLI_TIMEOUT: '%zu'", timeout);
    return -1;
  }
  LOCK(gbConf->lock);
  gbConf->cliTimeout = timeout;
  UNLOCK(gbConf->lock);

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
  DIR* dir = opendir(gbConf->logDir);
  char *buf = NULL;


  if (dir) {
    closedir(dir);
  } else if (errno == ENOENT) {
    GB_ASPRINTF(&buf, "mkdir -p %s -m 0755 > /dev/null", gbConf->logDir);
    if (gbRunner(buf) == -1) {
      MSG(stderr, "mkdir(%s) failed (%s)", gbConf->logDir, strerror (errno));
      return 0;  /* False */
    }
  } else {
    MSG(stderr, "opendir(%s) failed (%s)", gbConf->logDir, strerror (errno));
    return 0;  /* False */
  }

  GB_FREE(buf);
  return 1;
}


void
fetchGlfsVolServerFromEnv()
{
  char *volServer;


  volServer = getenv("GB_BHV_VOLSERVER");
  if (!volServer) {
    volServer = "localhost";
  }
  snprintf(gbConf->volServer, HOST_NAME_MAX, "%s", volServer);

  LOG("mgmt", GB_LOG_INFO, "Block Hosting Volfile Server Set to: %s", gbConf->volServer);
}


bool
gbDependencyVersionCompare(int dependencyName, char *version)
{
  size_t vNum[VERNUM_BUFLEN] = {0, };
  char *verStr;
  int i = 0, j;
  char *token, *tmp;
  bool done = false;
  bool ret = false;


  if (GB_STRDUP(verStr, version) < 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "gbDependencyVersionCompare: failed to strdup (%s)", strerror(errno));
    return ret;
  }

  token = strtok(verStr, ".-");
  while( token != NULL ) {
    done = false;
    for (j = 0; j < strlen(token); j++) { /* say if version is 2.1.fb49, parse fb49 */
      if (isdigit(token[j])) {
        vNum[i] = atoi(token);
      } else {
        tmp = token;
        for(; *tmp; ++tmp) {
          if (isdigit(*tmp)) {
            vNum[i] = atoi(tmp);  /* feed 49 from fb49, so done = true */
            done = true;
            break;
          }
        }
      }
      if (done) {
        break;
      }
    }
    token = strtok(NULL, ".-");
    i++;
  }

  switch (dependencyName) {
  case TCMURUNNER:
    if (DEPENDENCY_VERSION(vNum[0], vNum[1], vNum[2]) >= GB_MIN_TCMURUNNER_VERSION_CODE) {
      ret = true;
    }
    break;
  case TARGETCLI:
    if (DEPENDENCY_VERSION(vNum[0], vNum[1], vNum[2]) >= GB_MIN_TARGETCLI_VERSION_CODE) {
      ret = true;
    }
    break;
  case RTSLIB_BLKSIZE:
    if (DEPENDENCY_VERSION(vNum[0], vNum[1], vNum[2]) >= GB_MIN_RTSLIB_BLKSIZE_VERSION_CODE) {
      ret = true;
    }
    break;
  case TARGETCLI_RELOAD:
    if (DEPENDENCY_VERSION(vNum[0], vNum[1], vNum[2]) >= GB_MIN_TARGETCLI_RELOAD_VERSION_CODE) {
      ret = true;
    }
    break;
  case RTSLIB_RELOAD:
    if (DEPENDENCY_VERSION(vNum[0], vNum[1], vNum[2]) >= GB_MIN_RTSLIB_RELOAD_VERSION_CODE) {
      ret = true;
    }
    break;
  case CONFIGSHELL_SEMICOLON:
    if (DEPENDENCY_VERSION(vNum[0], vNum[1], vNum[2]) >= GB_MIN_CONFIGSHELL_SEM_VERSION_CODE) {
      ret = true;
    }
    break;
  case TCMURUNNER_IO_TIMEOUT:
    if (DEPENDENCY_VERSION(vNum[0], vNum[1], vNum[2]) >= GB_MIN_TCMURUNNER_IO_TIMEOUT_VERSION_CODE) {
      ret = true;
    }
    break;
  case TARGETCLI_DAEMON:
    if (DEPENDENCY_VERSION(vNum[0], vNum[1], vNum[2]) >= GB_MIN_TARGETCLI_DAEMON_VERSION_CODE) {
      ret = true;
    }
    break;
  }

  GB_FREE(verStr);
  return ret;
}


static int
glusterLogrotateConfigSet(char *logDir)
{
  char *buf = NULL, *line = NULL, *p, *dom = NULL;
  int ret, m, len;
  size_t n;
  FILE *fp;


  if (gbCtx == GB_CLI_MODE) {
    dom = "cli";
  } else if (gbCtx == GB_DAEMON_MODE) {
    dom = "mgmt";
  }

  if (!dom) {
    return -EINVAL;
  }

  fp = fopen(GB_LOGROTATE_PATH, "r+");
  if (fp == NULL) {
    ret = -errno;
    LOG(dom, GB_LOG_ERROR, "Failed to open file '%s', %s", GB_LOGROTATE_PATH,
        strerror (errno));
    return ret;
  }

  ret = fseek(fp, 0L, SEEK_END);
  if (ret == -1) {
    ret = -errno;
    LOG(dom, GB_LOG_ERROR, "Failed to seek file '%s', %s", GB_LOGROTATE_PATH,
        strerror (errno));
    goto error;
  }

  len = ftell(fp);
  if (len == -1) {
    ret = -errno;
    LOG(dom, GB_LOG_ERROR, "Failed to get the length of file '%s', %s",
        GB_LOGROTATE_PATH, strerror (errno));
    goto error;
  }

  /* to make sure we have enough size */
  len += strlen(logDir) + 1;
  if (GB_ALLOC_N(buf, len) < 0) {
    ret = -ENOMEM;
    goto error;
  }

  p = buf;
  fseek(fp, 0L, SEEK_SET);
  while ((m = getline(&line, &n, fp)) != -1) {
    if (strstr(line, "*.log") && strchr(line, '{')) {
      m = sprintf(p, "%s/*.log {\n", logDir);
    } else {
      m = sprintf(p, "%s", line);
    }
    if (m < 0) {
      ret = m;
      goto error;
    }

    p += m;
  }
  *p = '\0';
  len = p - buf;

  fseek(fp, 0L, SEEK_SET);
  if (truncate(GB_LOGROTATE_PATH, 0L) == -1) {
    LOG(dom, GB_LOG_ERROR, "Failed to truncate '%s', %s", GB_LOGROTATE_PATH,
        strerror (errno));
    goto error;
  }
  ret = fwrite(buf, 1, len, fp);
  if (ret != len) {
    LOG(dom, GB_LOG_ERROR, "Failed to update '%s', %s", GB_LOGROTATE_PATH,
        strerror (errno));
    goto error;
  }

  ret = 0;
 error:
  if (fp) {
    fclose(fp);
  }
  GB_FREE(buf);
  GB_FREE(line);
  return ret;
}


static bool
isSamePath(const char *path1, const char *path2)
{
    struct stat st1 = {0,};
    struct stat st2 = {0,};

    if (!path1 || !path2) {
        return false;
    }

    if (stat(path1, &st1) == -1 || stat(path2, &st2) == -1) {
        return false;
    }

    return st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino;
}


static int
initLogDirAndFiles(char *newLogDir)
{
  char *logDir = NULL;
  char *tmpLogDir = NULL;
  char *dom;
  int ret = 0;
  bool def = false;
  int logLevel;


  if (gbCtx == GB_CLI_MODE) {
    dom = "cli";
    logLevel = GB_LOG_DEBUG;
  } else {
    dom = "mgmt";
    logLevel = GB_LOG_CRIT;
  }

  /*
   * The priority of the logdir setting is:
   * 1, /etc/sysconfig/gluster-blockd config file
   * 2, "GB_LOGDIR" from the ENV setting
   * 3, default as GB_LOGDIR_DEF
   */
  if (newLogDir) {
    logDir = newLogDir;
    LOCK(gbConf->lock);
    if (isSamePath(logDir, gbConf->logDir)) {
      UNLOCK(gbConf->lock);
      LOG(dom, GB_LOG_DEBUG,
          "No changes to current logDir: %s, skipping it.",
          gbConf->logDir);
      goto out;
    }
    UNLOCK(gbConf->lock);
    LOG(dom, logLevel,
        "trying to change logDir from %s to %s", gbConf->logDir, logDir);
  } else {
    logDir = getenv("GB_LOGDIR");

    tmpLogDir = glusterBlockDynConfigGetLogDir();
    if (tmpLogDir) {
      logDir = tmpLogDir;
    }

    if (!logDir) {
      def = true;
      logDir = GB_LOGDIR_DEF;
    }
  }

  if (strlen(logDir) > PATH_MAX - GB_MAX_LOGFILENAME) {
    LOG(dom, GB_LOG_ERROR, "strlen of logDir Path > PATH_MAX: %s", logDir);
    ret = -1;
    goto out;
  }

  /* set logfile paths */
  LOCK(gbConf->lock);
  snprintf(gbConf->logDir, PATH_MAX,
           "%s", logDir);
  snprintf(gbConf->daemonLogFile, PATH_MAX,
           "%s/gluster-blockd.log", logDir);
  snprintf(gbConf->cliLogFile, PATH_MAX,
           "%s/gluster-block-cli.log", logDir);
  snprintf(gbConf->gfapiLogFile, PATH_MAX,
           "%s/gluster-block-gfapi.log", logDir);
  snprintf(gbConf->configShellLogFile, PATH_MAX,
           "%s/gluster-block-configshell.log", logDir);
  snprintf(gbConf->cmdhistoryLogFile, PATH_MAX,
           "%s/cmd_history.log", logDir);

  if(!glusterBlockLogdirCreate()) {
    ret = -1;
  }
  UNLOCK(gbConf->lock);

  if (ret == -1) {
    goto out;
  }

  LOG(dom, logLevel, "logDir now is %s", gbConf->logDir);

  glusterBlockUpdateLruLogdir(gbConf->gfapiLogFile);

  if (!def) {
    glusterLogrotateConfigSet(gbConf->logDir);
  }

out:
  GB_FREE(tmpLogDir);
  return ret;
}


int
initLogging(void)
{
  return initLogDirAndFiles(NULL);
}


bool
glusterBlockSetLogDir(char *logDir)
{
  return initLogDirAndFiles(logDir);
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


char*
gbRunnerGetOutput(char *cmd)
{
  FILE *fp;
  char *tptr;
  char *buf = NULL;


  LOG("mgmt", GB_LOG_DEBUG, "command, %s", cmd);

  if (GB_ALLOC_N(buf, 1024) < 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "gbRunnerGetOutput: error allocating memory (%s)", strerror(errno));
    return NULL;
  }

  fp = popen(cmd, "r");
  if (fp) {
    size_t newLen = fread(buf, sizeof(char), 1024, fp);
    if (ferror(fp) != 0) {
      LOG("mgmt", GB_LOG_ERROR,
          "reading command output for %s failed (%s)", cmd, strerror(errno));
      buf[0] = '\0';
      goto fail;
    } else {
      buf[newLen++] = '\0';
      tptr = strchr(buf,'\n');
      if (tptr) {
        *tptr = '\0';
      }
      goto out;
    }
  } else {
    LOG("mgmt", GB_LOG_ERROR,
        "popen(%s): failed: %s", cmd, strerror(errno));
    goto fail;
  }

fail:
  GB_FREE(buf);

 out:
  if (fp) {
    pclose(fp);
  }
  return buf;
}


char *
gbGetRpmPkgVersion(const char* pkgName)
{
  char *exec;

  if (GB_ASPRINTF(&exec, GB_RPM_PKG_VERSION, pkgName) == -1) {
    return NULL;
  }

  return gbRunnerGetOutput(exec);
}


char *
gbRunnerGetPkgVersion(const char * pkgName)
{
  char *cmd = NULL;
  char *out;

  if (!strcmp(pkgName, TARGETCLI_STR)) {
    cmd = TARGETCLI_VERSION;
  } else if (!strcmp(pkgName, RTSLIB_STR)) {
    cmd = RTSLIB_VERSION;
  } else if (!strcmp(pkgName, CONFIGSHELL_STR)) {
    cmd = CONFIGSHELL_VERSION;
  } else if (!strcmp(pkgName,TCMU_STR)) {
    cmd = TCMU_VERSION;
  }

  if (!cmd) {
    return NULL;
  }

  out = gbRunnerGetOutput(cmd);
  if (!out[0] || !strcmp(out, "GIT_VERSION")) {
      out = gbGetRpmPkgVersion(pkgName);
  }

  return out;
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

    ret = memcpy(dest, src, n);
    dest[n] = '\0';

    return ret;
}


char *
gbStrcat(char *dest, const char *src, size_t destbytes,
         const char *filename, const char *funcname, size_t linenr)
{
    char *ret;
    size_t n = strlen(src);
    size_t m = strlen(dest);

    if (n > (destbytes - 1))
      return NULL;

    ret = memcpy(dest + m, src, n);
    dest[m + n] = '\0';

    return ret;
}
