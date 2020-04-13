/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# ifndef   _UTILS_H
# define   _UTILS_H   1

# define   _GNU_SOURCE         /* See feature_test_macros(7) */
# include  <stdio.h>

# include  <stdlib.h>
# include  <stddef.h>
# include  <stdbool.h>
# include  <string.h>
# include  <unistd.h>
# include  <errno.h>
# include  <time.h>
# include  <limits.h>
# include  <sys/time.h>
# include  <ctype.h>
# include  <pthread.h>
# include  <inttypes.h>

# include  "list.h"

# define  GB_LOGROTATE_PATH      "/etc/logrotate.d/gluster-block"
# define  GB_LOGDIR_DEF          DATADIR "/log/gluster-block"
# define  GB_INFODIR             DATADIR "/run"

# define  GB_LOCK_FILE           GB_INFODIR "/gluster-blockd.lock"
# define  GB_UNIX_ADDRESS        GB_INFODIR "/gluster-blockd.socket"

# define  GB_CAPS_FILE           CONFDIR "/gluster-block-caps.info"

# define  GB_TCP_PORT            24010
# define  GB_TCP_PORT_STR        "24010"

# define  GB_IO_TIMEOUT_DEF      43

# define  GFAPI_LOG_LEVEL        7

# define   DEVNULLPATH           "/dev/null"
# define   GB_SAVECONFIG         "/etc/target/saveconfig.json"
# define   GB_SAVECONFIG_TEMP    "/etc/target/saveconfig.json.temp"

# define  GB_METADIR             "/block-meta"
# define  GB_STOREDIR            "/block-store"
# define  GB_TXLOCKFILE          "meta.lock"
# define  GB_PRIO_FILENAME       "prio.info"
# define  GB_PRIO_FILE           GB_METADIR "/" GB_PRIO_FILENAME

# define  GB_MAX_LOGFILENAME     64  /* max strlen of file name */

# define  SUN_PATH_MAX           (sizeof(struct sockaddr_un) - sizeof(unsigned short int)) /*sun_family*/

# define  GB_METASTORE_RESERVE   10485760   /* 10 MiB reserve for block-meta */

# define  GB_DEF_CONFIGDIR       "/etc/sysconfig" /* the default config file directory */
# define  GB_DEF_CONFIGPATH      GB_DEF_CONFIGDIR"/gluster-blockd" /* the default config file */

# define  GB_RPM_PKG_VERSION     "rpm -qa | grep %s | awk -F- '{print $(NF-1)}'"

# define  GB_TIME_STRING_BUFLEN  \
          (4 + 1 + 2 + 1 + 2 + 1 + 2 + 1 + 2 + 1 + 2 + 1 + 6 + 1 +   5)
     /*   Yr      Mon     Day     Hour    Min     Sec     Ms  NULL  Round-off(32)
         2017 -   06  -   01  ' ' 18  :   58  :   29  . 695147 '\0' Power^2
         2017-06-01 18:58:29.695147                                             */

/* Target Create */
# define  FAILED_CREATE             "failed in create"
# define  FAILED_REMOTE_CREATE      "failed in remote create"
# define  FAILED_REMOTE_AYNC_CREATE "failed in remote async create"
# define  FAILED_CREATING_FILE      "failed while creating block file in gluster volume"
# define  FAILED_CREATING_META      "failed while creating block meta file from volume"

/* Target Capabilities */
# define  FAILED_CAPS               "failed in capabilities check"
# define  FAILED_REMOTE_CAPS        "failed in remote capabilities check"
# define  FAILED_REMOTE_AYNC_CAPS   "failed in remote async capabilities check"

/* Target List */
# define  FAILED_LIST               "failed in list"

/* Target Info */
# define  FAILED_INFO               "failed in info"

/* Target Modify */
# define  FAILED_MODIFY             "failed in modify"
# define  FAILED_MODIFY_SIZE        "failed while modifying block file size in gluster volume"
# define  FAILED_REMOTE_MODIFY      "failed in remote modify"
# define  FAILED_REMOTE_AYNC_MODIFY "failed in remote async modify"
# define  FAILED_REMOTE_MODIFY_SIZE "failed in remote modify block size"

/* Target Delete */
# define  FAILED_DELETE             "failed in delete"
# define  FAILED_REMOTE_DELETE      "failed in remote delete"
# define  FAILED_REMOTE_AYNC_DELETE "failed in remote async delete"
# define  FAILED_DELETING_FILE      "failed while deleting block file from gluster volume"
# define  FAILED_DELETING_META      "failed while deleting block meta file from volume"

/* Target Reload */
# define  FAILED_RELOAD             "failed in reload"
# define  FAILED_REMOTE_RELOAD      "failed in remote reload"

/* Target Replace */
# define  FAILED_REPLACE            "failed in replace"
# define  FAILED_REMOTE_REPLACE     "failed in remote replace portal"

/* Config generate */
# define  FAILED_GENCONFIG          "failed in generation of config"

# define  FAILED_DEPENDENCY         "failed dependency, check if you have targetcli and tcmu-runner installed"

# define FMT_WARN(fmt...) do { if (0) printf (fmt); } while (0)

# define GB_ASPRINTF(ptr, fmt...) ({FMT_WARN (fmt);                \
                int __ret=asprintf(ptr, ##fmt);__ret;})

# define LOCK(x)                                                     \
         do {                                                        \
            pthread_mutex_lock(&x);                                  \
          } while (0)

# define UNLOCK(x)                                                   \
         do {                                                        \
            pthread_mutex_unlock(&x);                                \
          } while (0)

# define  MSG(fd, fmt, ...)                                          \
          do {                                                       \
            int _len_;                                               \
            char *_buf_;                                             \
            if (fd <= 0)       /* including STDIN_FILENO 0 */        \
              fd = stderr;                                           \
            _len_ = GB_ASPRINTF(&_buf_, fmt, ##__VA_ARGS__);         \
            if (_len_ != -1) {                                       \
              if (_buf_[_len_ - 1] != '\n')                          \
                fprintf(fd, "%s\n", _buf_);                          \
              else                                                   \
                fprintf(fd, "%s", _buf_);                            \
              free(_buf_);                                           \
            } else {                                                 \
              fprintf(fd, fmt "\n", ##__VA_ARGS__);                  \
            }                                                        \
          } while (0)


struct gbConf {
  size_t glfsLruCount;
  unsigned int logLevel;
  size_t cliTimeout;
  char logDir[PATH_MAX];
  char daemonLogFile[PATH_MAX];
  char cliLogFile[PATH_MAX];
  char gfapiLogFile[PATH_MAX];
  char configShellLogFile[PATH_MAX];
  pthread_mutex_t lock;
  char cmdhistoryLogFile[PATH_MAX];
  bool noRemoteRpc;
  char volServer[HOST_NAME_MAX];
};

# define GB_XDATA_MAGIC_NUM 0xABCD2019DCBA
# define GB_XDATA_GET_MAGIC_NUM(magic) ((magic) >> 16)
# define GB_XDATA_GET_MAGIC_VER(magic) ((magic) & 0xFFFF)
# define GB_XDATA_GEN_MAGIC(ver) ((GB_XDATA_MAGIC_NUM << 16) | ((ver) & 0xFFFF))
# define GB_XDATA_IS_MAGIC(magic) (GB_XDATA_GET_MAGIC_NUM(magic) == GB_XDATA_MAGIC_NUM)

struct gbXdata {
  uint64_t magic;
  char data[];
};

struct gbCreate {
  char volServer[HOST_NAME_MAX];
  size_t blk_size;
  size_t io_timeout;
};

extern struct gbConf *gbConf;

# define  LOG(str, level, fmt, ...)                                    \
          do {                                                         \
            FILE *_fd_ = NULL;                                         \
            char _timestamp_[GB_TIME_STRING_BUFLEN] = {0};             \
            char *_tmp_;                                               \
            bool _logFileExist_ = false;                               \
            if (GB_STRDUP(_tmp_, str) < 0)                             \
              fprintf(stderr, "No memory: %s\n", strerror(errno));     \
            LOCK(gbConf->lock);                                        \
            if (level <= gbConf->logLevel) {                           \
              if (!strcmp(_tmp_, "mgmt")) {                            \
                if (gbConf->daemonLogFile[0]) {                        \
                    _fd_ = fopen (gbConf->daemonLogFile, "a");         \
                    _logFileExist_ = true;                             \
                }                                                      \
              } else if (!strcmp(_tmp_, "cli")) {                      \
                if (gbConf->cliLogFile[0]) {                           \
                    _fd_ = fopen (gbConf->cliLogFile, "a");            \
                    _logFileExist_ = true;                             \
                }                                                      \
              } else if (!strcmp(_tmp_, "gfapi")) {                    \
                if (gbConf->gfapiLogFile[0]) {                         \
                    _fd_ = fopen (gbConf->gfapiLogFile, "a");          \
                    _logFileExist_ = true;                             \
                }                                                      \
              } else if (!strcmp(_tmp_, "cmdlog")) {                   \
                if (gbConf->cmdhistoryLogFile[0]) {                    \
                    _fd_ = fopen (gbConf->cmdhistoryLogFile, "a");     \
                    _logFileExist_ = true;                             \
                }                                                      \
              } else {                                                 \
                _fd_ = stderr;                                         \
              }                                                        \
              if (_fd_ == NULL) {                                      \
                if (_logFileExist_)                                    \
                    fprintf(stderr, "Error opening log file: %s\n"     \
                            "Logging to stderr.\n",                    \
                            strerror(errno));                          \
                _fd_ = stderr;                                         \
              }                                                        \
              logTimeNow(_timestamp_, GB_TIME_STRING_BUFLEN);          \
              fprintf(_fd_, "[%s] %s: " fmt " [at %s+%d :<%s>]\n",     \
                      _timestamp_, LogLevelLookup[level],              \
                      ##__VA_ARGS__, __FILE__, __LINE__, __FUNCTION__);\
              if (_fd_ != stderr)                                      \
                fclose(_fd_);                                          \
            }                                                          \
            UNLOCK(gbConf->lock);                                      \
            GB_FREE(_tmp_);                                            \
          } while (0)

# define  GB_METALOCK_OR_GOTO(lkfd, volume, errCode, errMsg, label)  \
          do {                                                       \
            struct flock _lock_ = {0, };                             \
            _lock_.l_type = F_WRLCK;                                 \
            if (glfs_posix_lock (lkfd, F_SETLKW, &_lock_)) {         \
              LOG("mgmt", GB_LOG_ERROR, "glfs_posix_lock() on "      \
                  "volume %s failed[%s]", volume, strerror(errno));  \
              errCode = errno;                                       \
              if (!errMsg) {                                         \
                    GB_ASPRINTF (&errMsg, "Not able to acquire "     \
                        "lock on %s[%s]", volume, strerror(errCode));\
              }                                                      \
              goto label;                                            \
            }                                                        \
          } while (0)

# define  GB_METAUPDATE_OR_GOTO(lock, glfs, fname,                      \
                                volume, ret, errMsg, label,...)         \
          do {                                                          \
            char *_write_;                                              \
            struct glfs_fd *_tgmfd_;                                    \
            LOCK(lock);                                                 \
            ret = glfs_chdir (glfs, GB_METADIR);                        \
            if (ret) {                                                  \
              GB_ASPRINTF(&errMsg, "Failed to update transaction log "  \
                "for %s/%s[%s]", volume, fname, strerror(errno));       \
              LOG("gfapi", GB_LOG_ERROR, "glfs_chdir(%s) on "           \
                  "volume %s failed[%s]", GB_METADIR, volume,           \
                  strerror(errno));                                     \
              UNLOCK(lock);                                             \
              ret = -1;                                                 \
              goto label;                                               \
            }                                                           \
            _tgmfd_ = glfs_creat(glfs, fname,                           \
                               O_WRONLY | O_APPEND | O_SYNC,            \
                               S_IRUSR | S_IWUSR);                      \
            if (!_tgmfd_) {                                             \
              GB_ASPRINTF(&errMsg, "Failed to update transaction log "  \
                "for %s/%s[%s]", volume, fname, strerror(errno));       \
              LOG("mgmt", GB_LOG_ERROR, "glfs_creat(%s): on "           \
                  "volume %s failed[%s]", fname, volume,                \
                  strerror(errno));                                     \
              UNLOCK(lock);                                             \
              ret = -1;                                                 \
              goto label;                                               \
            }                                                           \
            if (GB_ASPRINTF(&_write_, ##__VA_ARGS__) < 0) {             \
              ret = -1;                                                 \
            }                                                           \
            if (!ret) {                                                 \
              if(glfs_write (_tgmfd_, _write_, strlen(_write_), 0) < 0){\
                GB_ASPRINTF(&errMsg, "Failed to update transaction log "\
                  "for %s/%s[%s]", volume, fname, strerror(errno));     \
                LOG("mgmt", GB_LOG_ERROR, "glfs_write(%s): on "         \
                    "volume %s failed[%s]", fname, volume,              \
                    strerror(errno));                                   \
                ret = -1;                                               \
              }                                                         \
              GB_FREE(_write_);                                         \
            }                                                           \
            if (_tgmfd_ && glfs_close(_tgmfd_) != 0) {                  \
              GB_ASPRINTF(&errMsg, "Failed to update transaction log "  \
                "for %s/%s[%s]", volume, fname, strerror(errno));       \
              LOG("mgmt", GB_LOG_ERROR, "glfs_close(%s): on "           \
                  "volume %s failed[%s]", fname, volume,                \
                  strerror(errno));                                     \
              UNLOCK(lock);                                             \
              ret = -1;                                                 \
              goto label;                                               \
            }                                                           \
            UNLOCK(lock);                                               \
            if (ret) {                                                  \
              goto label;                                               \
            }                                                           \
          } while (0)

# define  GB_METAUNLOCK(lkfd, volume, ret, errMsg)                   \
          do {                                                       \
            struct flock _lock_ = {0, };                             \
            _lock_.l_type = F_UNLCK;                                 \
            if (glfs_posix_lock(lkfd, F_SETLK, &_lock_)) {           \
              if (!errMsg) {                                         \
                    GB_ASPRINTF (&errMsg, "Not able to acquire "     \
                        "lock on %s[%s]", volume, strerror(errno));  \
              }                                                      \
              LOG("mgmt", GB_LOG_ERROR, "glfs_posix_lock() on "      \
                  "volume %s failed[%s]", volume, strerror(errno));  \
              ret = -1;                                              \
            }                                                        \
          } while (0)

# define  GB_CMD_EXEC_AND_VALIDATE(cmd, sr, blk, vol, opt)             \
          do {                                                         \
            FILE *_fp_;                                                  \
            char _tmp_[1024];                                            \
            LOG("mgmt", GB_LOG_DEBUG, "command, %s", cmd);             \
            _fp_ = popen(cmd, "r");                                      \
            snprintf(_tmp_, 1024, "%s/%s", vol?vol:"", blk->block_name); \
            if (_fp_) {                                                  \
              size_t newLen = fread(sr->out, sizeof(char), 8192, _fp_);  \
              if (ferror( _fp_ ) != 0) {                                 \
                LOG("mgmt", GB_LOG_ERROR,                              \
                    "reading command %s output for %s failed(%s)", _tmp_,\
                    cmd, strerror(errno));                             \
                sr->out[0] = '\0';                                     \
                sr->exit = -1;                                         \
                pclose(_fp_);                                            \
                break;                                                 \
              } else {                                                 \
                sr->out[newLen++] = '\0';                              \
              }                                                        \
              sr->exit = blockValidateCommandOutput(sr->out, opt,      \
                                                    (void*)blk);       \
              pclose(_fp_);                                              \
            } else {                                                   \
              LOG("mgmt", GB_LOG_ERROR,                                \
                  "popen(): for %s executing command %s failed(%s)",   \
                  _tmp_, cmd, strerror(errno));                          \
            }                                                          \
            LOG("mgmt", GB_LOG_DEBUG, "raw output, %s", sr->out);      \
            LOG("mgmt", GB_LOG_INFO, "command exit code, %d",          \
                 sr->exit);                                            \
          } while (0)

# define GB_OUT_VALIDATE_OR_GOTO(out, label, errStr, blk, vol, ...)    \
         do {                                                          \
           char *_tmp_;                                                \
           char _vol_blk_[1024];                                       \
           snprintf(_vol_blk_, 1024, "%s/%s", vol?vol:"",              \
                    blk->block_name);                                  \
           if (GB_ASPRINTF(&_tmp_, ##__VA_ARGS__) == -1)               \
             goto label;                                               \
           if (!strstr(out, _tmp_)) {                                  \
             GB_FREE(_tmp_);                                           \
             LOG("mgmt", GB_LOG_ERROR, errStr, _vol_blk_);             \
             LOG("mgmt", GB_LOG_ERROR, "Error from targetcli:\n%s\n",  \
                  out);                                                \
             goto label;                                               \
           }                                                           \
           GB_FREE(_tmp_);                                             \
         } while (0)

# define GB_RPC_CALL(op, blk, reply, rqstp, ret)                    \
        do {                                                        \
          blockResponse *resp = block_##op##_1_svc_st(blk, rqstp);  \
          if (resp) {                                               \
            memcpy(reply, resp, sizeof(*reply));                    \
            GB_FREE(resp);                                          \
            ret = true;                                             \
          } else {                                                  \
            ret = false;                                            \
          }                                                         \
        } while (0)


# define  CALLOC(x)                                                  \
            calloc(1, x)

# define  GB_ALLOC(ptr)                                              \
            gbAlloc(&(ptr), sizeof(*(ptr)),                          \
                    __FILE__, __FUNCTION__, __LINE__)

# define  GB_ALLOC_N(ptr, count)                                     \
            gbAllocN(&(ptr), sizeof(*(ptr)), (count),                \
                     __FILE__, __FUNCTION__, __LINE__)               \

# define  xalloc_oversized(n, s)                                     \
            ((size_t) (sizeof(ptrdiff_t) <= sizeof(size_t) ? -1 : -2) / (s) < (n))

# define  GB_REALLOC_N(ptr, count)                                    \
            gbReallocN(&(ptr), sizeof(*(ptr)), (count),               \
                       __FILE__, __FUNCTION__, __LINE__)

# define  GB_STRDUP(dst, src)                                        \
            gbStrdup(&(dst), src,                                    \
                     __FILE__, __FUNCTION__, __LINE__)

# define  GB_STRCPY(dst, src, destbytes)                             \
            gbStrcpy((dst), (src), (destbytes),                      \
                     __FILE__, __FUNCTION__, __LINE__)

# define  GB_STRCAT(dst, src, destbytes)                             \
            gbStrcat((dst), (src), (destbytes),                      \
                     __FILE__, __FUNCTION__, __LINE__)

# define  GB_STRCPYSTATIC(dst, src)                                  \
            GB_STRCPY((dst), (src), (sizeof(dst)))

# define  GB_FREE(ptr)                                               \
            gbFree(1 ? (void *) &(ptr) : (ptr))


typedef enum gbCliCmdlineOption {
  GB_CLI_UNKNOWN = 0,
  GB_CLI_TIMEOUT,
  GB_CLI_CREATE,
  GB_CLI_LIST,
  GB_CLI_INFO,
  GB_CLI_DELETE,
  GB_CLI_MODIFY,
  GB_CLI_REPLACE,
  GB_CLI_RELOAD,
  GB_CLI_GENCONFIG,
  GB_CLI_HELP,
  GB_CLI_HYPHEN_HELP,
  GB_CLI_VERSION,
  GB_CLI_HYPHEN_VERSION,
  GB_CLI_USAGE,
  GB_CLI_HYPHEN_USAGE,

  GB_CLI_OPT_MAX
} gbCliCmdlineOption;

static const char *const gbCliCmdlineOptLookup[] = {
  [GB_CLI_UNKNOWN]        = "NONE",
  [GB_CLI_TIMEOUT]        = "timeout",
  [GB_CLI_CREATE]         = "create",
  [GB_CLI_LIST]           = "list",
  [GB_CLI_INFO]           = "info",
  [GB_CLI_DELETE]         = "delete",
  [GB_CLI_MODIFY]         = "modify",
  [GB_CLI_REPLACE]        = "replace",
  [GB_CLI_RELOAD]         = "reload",
  [GB_CLI_GENCONFIG]      = "genconfig",
  [GB_CLI_HELP]           = "help",
  [GB_CLI_HYPHEN_HELP]    = "--help",
  [GB_CLI_VERSION]        = "version",
  [GB_CLI_HYPHEN_VERSION] = "--version",
  [GB_CLI_USAGE]          = "usage",
  [GB_CLI_HYPHEN_USAGE]   = "--usage",

  [GB_CLI_OPT_MAX]        = NULL,
};

typedef enum gbCliCreateOptions {
  GB_CLI_CREATE_UNKNOWN    = 0,
  GB_CLI_CREATE_HA         = 1,
  GB_CLI_CREATE_AUTH       = 2,
  GB_CLI_CREATE_PREALLOC   = 3,
  GB_CLI_CREATE_STORAGE    = 4,
  GB_CLI_CREATE_RBSIZE     = 5,
  GB_CLI_CREATE_BLKSIZE    = 6,
  GB_CLI_CREATE_IO_TIMEOUT = 7,

  GB_CLI_CREATE_OPT_MAX
} gbCliCreateOptions;

static const char *const gbCliCreateOptLookup[] = {
  [GB_CLI_CREATE_UNKNOWN]    = "NONE",
  [GB_CLI_CREATE_HA]         = "ha",
  [GB_CLI_CREATE_AUTH]       = "auth",
  [GB_CLI_CREATE_PREALLOC]   = "prealloc",
  [GB_CLI_CREATE_STORAGE]    = "storage",
  [GB_CLI_CREATE_RBSIZE]     = "ring-buffer",
  [GB_CLI_CREATE_BLKSIZE]    = "block-size",
  [GB_CLI_CREATE_IO_TIMEOUT] = "io-timeout",

  [GB_CLI_CREATE_OPT_MAX]  = NULL,
};

typedef enum gbDaemonCmdlineOption {
  GB_DAEMON_UNKNOWN        = 0,
  GB_DAEMON_HELP           = 1,
  GB_DAEMON_VERSION        = 2,
  GB_DAEMON_USAGE          = 3,
  GB_DAEMON_GLFS_LRU_COUNT = 4,
  GB_DAEMON_LOG_LEVEL      = 5,
  GB_DAEMON_NO_REMOTE_RPC  = 6,

  GB_DAEMON_OPT_MAX
} gbDaemonCmdlineOption;

static const char *const gbDaemonCmdlineOptLookup[] = {
  [GB_DAEMON_UNKNOWN]        = "NONE",
  [GB_DAEMON_HELP]           = "help",
  [GB_DAEMON_VERSION]        = "version",
  [GB_DAEMON_USAGE]          = "usage",
  [GB_DAEMON_GLFS_LRU_COUNT] = "glfs-lru-count",
  [GB_DAEMON_LOG_LEVEL]      = "log-level",
  [GB_DAEMON_NO_REMOTE_RPC]  = "no-remote-rpc",

  [GB_DAEMON_OPT_MAX]        = NULL,
};

typedef enum gbProcessCtx {
  GB_UNKNOWN_MODE         = 0,
  GB_CLI_MODE             = 1,
  GB_DAEMON_MODE          = 2,

  GB_CTX_MODE_MAX
} gbProcessCtx;

extern gbProcessCtx gbCtx;

typedef enum LogLevel {
  GB_LOG_NONE       = 0,
  GB_LOG_CRIT       = 1,
  GB_LOG_ERROR      = 2,
  GB_LOG_WARNING    = 3,
  GB_LOG_INFO       = 4,
  GB_LOG_DEBUG      = 5,
  GB_LOG_TRACE      = 6,

  GB_LOG_MAX
} LogLevel;

static const char *const LogLevelLookup[] = {
  [GB_LOG_NONE]       = "NONE",
  [GB_LOG_CRIT]       = "CRIT",
  [GB_LOG_ERROR]      = "ERROR",
  [GB_LOG_WARNING]    = "WARNING",
  [GB_LOG_INFO]       = "INFO",
  [GB_LOG_DEBUG]      = "DEBUG",
  [GB_LOG_TRACE]      = "TRACE",

  [GB_LOG_MAX]        = NULL,
};

typedef enum Metakey {
  GB_META_VOLUME      = 0,
  GB_META_GBID        = 1,
  GB_META_SIZE        = 2,
  GB_META_HA          = 3,
  GB_META_ENTRYCREATE = 4,
  GB_META_ENTRYDELETE = 5,
  GB_META_PASSWD      = 6,
  GB_META_RINGBUFFER  = 7,
  GB_META_PRIOPATH    = 8,
  GB_META_BLKSIZE     = 9,
  GB_META_IO_TIMEOUT  = 10,

  GB_METAKEY_MAX
} Metakey;

static const char *const MetakeyLookup[] = {
  [GB_META_VOLUME]      = "VOLUME",
  [GB_META_GBID]        = "GBID",
  [GB_META_SIZE]        = "SIZE",
  [GB_META_HA]          = "HA",
  [GB_META_ENTRYCREATE] = "ENTRYCREATE",
  [GB_META_ENTRYDELETE] = "ENTRYDELETE",
  [GB_META_PASSWD]      = "PASSWORD",
  [GB_META_RINGBUFFER]  = "RINGBUFFER",
  [GB_META_PRIOPATH]    = "PRIOPATH",
  [GB_META_BLKSIZE]     = "BLKSIZE",
  [GB_META_IO_TIMEOUT]  = "IOTIMEOUT",

  [GB_METAKEY_MAX]      = NULL
};

typedef enum MetaStatus {
  GB_CONFIG_SUCCESS           = 0,
  GB_CONFIG_FAIL              = 1,
  GB_CONFIG_INPROGRESS        = 2,
  GB_AUTH_ENFORCEING          = 3,
  GB_AUTH_ENFORCED            = 4,
  GB_AUTH_ENFORCE_FAIL        = 5,
  GB_AUTH_CLEAR_ENFORCEING    = 6,
  GB_AUTH_CLEAR_ENFORCED      = 7,
  GB_AUTH_CLEAR_ENFORCE_FAIL  = 8,
  GB_CLEANUP_SUCCESS          = 9,
  GB_CLEANUP_FAIL             = 10,
  GB_CLEANUP_INPROGRESS       = 11,
  GB_RP_SUCCESS               = 12,
  GB_RP_INPROGRESS            = 13,
  GB_RP_FAIL                  = 14,
  GB_RS_SUCCESS               = 15,
  GB_RS_INPROGRESS            = 16,
  GB_RS_FAIL                  = 17,

  GB_METASTATUS_MAX
} MetaStatus;

static const char *const MetaStatusLookup[] = {
  [GB_CONFIG_SUCCESS]           = "CONFIGSUCCESS",
  [GB_CONFIG_FAIL]              = "CONFIGFAIL",
  [GB_CONFIG_INPROGRESS]        = "CONFIGINPROGRESS",
  [GB_AUTH_ENFORCEING]          = "AUTHENFORCEING",
  [GB_AUTH_ENFORCED]            = "AUTHENFORCED",
  [GB_AUTH_ENFORCE_FAIL]        = "AUTHENFORCEFAIL",
  [GB_AUTH_CLEAR_ENFORCEING]    = "AUTHCLEARENFORCEING",
  [GB_AUTH_CLEAR_ENFORCED]      = "AUTHCLEARENFORCED",
  [GB_AUTH_CLEAR_ENFORCE_FAIL]  = "AUTHCLEARENFORCEFAIL",
  [GB_CLEANUP_INPROGRESS]       = "CLEANUPINPROGRESS",
  [GB_CLEANUP_SUCCESS]          = "CLEANUPSUCCESS",
  [GB_CLEANUP_FAIL]             = "CLEANUPFAIL",
  [GB_RP_SUCCESS]               = "RPSUCCESS",
  [GB_RP_INPROGRESS]            = "RPINPROGRESS",
  [GB_RP_FAIL]                  = "RPFAIL",
  [GB_RS_SUCCESS]               = "RSSUCCESS",
  [GB_RS_INPROGRESS]            = "RSINPROGRESS",
  [GB_RS_FAIL]                  = "RSFAIL",

  [GB_METASTATUS_MAX]           = NULL,
};

typedef enum RemoteCreateResp {
  GB_BACKEND_RESP   = 0,
  GB_IQN_RESP       = 1,
  GB_TPG_NO_RESP    = 2,
  GB_LUN_NO_RESP    = 3,
  GB_IP_PORT_RESP   = 4,
  GB_PORTAL_RESP    = 5,
  GB_FAILED_RESP    = 6,

  GB_REMOTE_CREATE_RESP_MAX
} RemoteCreateResp;

static const char *const RemoteCreateRespLookup[] = {
  [GB_BACKEND_RESP]  = "Created user-backed storage object ",
  [GB_IQN_RESP]      = "Created target ",
  [GB_TPG_NO_RESP]   = "Created TPG ",
  [GB_LUN_NO_RESP]   = "Created LUN ",
  [GB_IP_PORT_RESP]  = "Using default IP port ",
  [GB_PORTAL_RESP]   = "Created network portal ",
  [GB_FAILED_RESP]   = "failed to configure on ",

  [GB_REMOTE_CREATE_RESP_MAX] = NULL,
};

typedef struct gbConfig {
  pthread_t threadId;
  char *configPath;

  bool isDynamic;
  char *GB_LOG_LEVEL;
  char *GB_LOG_DIR;
  ssize_t GB_GLFS_LRU_COUNT;
  ssize_t GB_CLI_TIMEOUT;  /* seconds */
} gbConfig;

typedef enum gbDependencies {
  TCMURUNNER              = 1,
  TARGETCLI               = 2,
  RTSLIB_BLKSIZE          = 3,
  TARGETCLI_RELOAD        = 4,
  RTSLIB_RELOAD           = 5,
  CONFIGSHELL_SEMICOLON   = 6,
  TCMURUNNER_IO_TIMEOUT   = 7,
  TARGETCLI_DAEMON        = 8,
} gbDependencies;

int initGbConfig(void);
void finiGbConfig(void);

int glusterBlockSetLogLevel(unsigned int logLevel);

//int glusterBlockSetCliTimeout(size_t timeout);

int glusterBlockCLIOptEnumParse(const char *opt);

int glusterBlockCLICreateOptEnumParse(const char *opt);

int glusterBlockDaemonOptEnumParse(const char *opt);

int blockLogLevelEnumParse(const char *opt);

int blockMetaKeyEnumParse(const char *opt);

int blockMetaStatusEnumParse(const char *opt);

int blockRemoteCreateRespEnumParse(const char *opt);

void logTimeNow(char* buf, size_t bufSize);

void fetchGlfsVolServerFromEnv(void);

bool gbDependencyVersionCompare(int dependencyName, char *version);

bool glusterBlockSetLogDir(char *logDir);

int initLogging(void);

int gbRunnerExitStatus(int exitStatus);

int gbRunner(char *cmd);

char* gbRunnerGetOutput(char *cmd);

char* gbGetRpmPkgVersion(const char* pkgName);

char* gbRunnerGetPkgVersion(const char * pkgName);

int gbAlloc(void *ptrptr, size_t size,
            const char *filename, const char *funcname, size_t linenr);

int gbAllocN(void *ptrptr, size_t size, size_t count,
             const char *filename, const char *funcname, size_t linenr);

int gbReallocN(void *ptrptr, size_t size, size_t count,
               const char *filename, const char *funcname, size_t linenr);

int gbStrdup(char **dest, const char *src,
             const char *filename, const char *funcname, size_t linenr);

char* gbStrcpy(char *dest, const char *src, size_t destbytes,
               const char *filename, const char *funcname, size_t linenr);

char *gbStrcat(char *dest, const char *src, size_t destbytes,
               const char *filename, const char *funcname, size_t linenr);

void gbFree(void *ptrptr);

char *glusterBlockDynConfigGetLogDir(void);

void glusterBlockDestroyConfig(struct gbConfig *cfg);

gbConfig *glusterBlockSetupConfig(void);

#endif  /* _UTILS_H */
