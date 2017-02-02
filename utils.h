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

# include  <stdio.h>
# include  <stdlib.h>
# include  <stdbool.h>
# include  <string.h>
# include  <errno.h>
# include  <time.h>


/* Target Create */
# define  FAILED_CREATE             "failed in create"
# define  FAILED_CREATING_FILE      "failed while creating block file in gluster volume"
# define  FAILED_CREATING_BACKEND   "failed while creating glfs backend"
# define  FAILED_CREATING_IQN       "failed while creating IQN"
# define  FAILED_CREATING_LUN       "failed while creating LUN"
# define  FAILED_SETTING_ATTRIBUTES "failed while setting attributes"
# define  FAILED_SAVEING_CONFIG     "failed while saving configuration"

/* Target List */
# define  FAILED_LIST               "failed in list"
# define  FAILED_LIST_BACKEND       "failed while listing glfs backends"

/* Target Info */
# define  FAILED_INFO               "failed in info"
# define  FAILED_GATHERING_INFO     "failed while gathering target info"

/* Target get cfgstring */
# define  FAILED_GATHERING_CFGSTR   "failed while gathering backend cfgstring"

/* Target Delete */
# define  FAILED_DELETE             "failed in delete"
# define  FAILED_DELETING_BACKEND   "failed while deleting glfs backend"
# define  FAILED_DELETING_IQN       "failed while deleting IQN"
# define  FAILED_DELETING_FILE      "failed while deleting block file from gluster volume"


# define ERROR(fmt, ...)                                             \
         do {                                                        \
           fprintf(stderr, "Error: " fmt " [at %s+%d :<%s>]\n",      \
                   __VA_ARGS__, __FILE__, __LINE__, __FUNCTION__);   \
          } while (0)

# define  MSG(fmt, ...)                                              \
          do {                                                       \
            fprintf(stdout, fmt, __VA_ARGS__);                       \
          } while (0)

# define  LOG(str, level, fmt, ...)                                  \
          do {                                                       \
            FILE *fd;                                                \
            if (!strcmp(str, "mgmt"))                                \
              fd = fopen (DAEMON_LOG_FILE, "a");                     \
            else if (strcmp(str, "cli"))                             \
              fd = fopen (CLI_LOG_FILE, "a");                        \
            else if (strcmp(str, "gfapi"))                           \
              fd = fopen (GFAPI_LOG_FILE, "a");                      \
            else                                                     \
              fd = stderr;                                           \
            fprintf(fd, "[%lu] %s: " fmt " [at %s+%d :<%s>]\n",      \
                    (unsigned long)time(NULL), LogLevelLookup[level],\
                    __VA_ARGS__, __FILE__, __LINE__, __FUNCTION__);  \
            if (fd != stderr)                                        \
              fclose(fd);                                            \
          } while (0)

# define  GB_METALOCK_OR_GOTO(lkfd, volume, ret, label)              \
          do {                                                       \
            struct flock lock = {0, };                               \
            lock.l_type = F_WRLCK;                                   \
            if (glfs_posix_lock (lkfd, F_SETLKW, &lock)) {           \
              LOG("mgmt", GB_LOG_ERROR, "glfs_posix_lock() on "      \
                  "volume %s failed[%s]", volume, strerror(errno));  \
              ret = -1;                                              \
              goto label;                                            \
            }                                                        \
          } while (0)

# define  GB_METAUPDATE_OR_GOTO(tgmfd, fname, volume, ret, label,...)\
          do {                                                       \
            char *write;                                             \
            if (asprintf(&write, __VA_ARGS__) < 0) {                 \
              ret = -1;                                              \
              goto label;                                            \
            }                                                        \
            if(glfs_write (tgmfd, write, strlen(write), 0) < 0) {    \
              LOG("mgmt", GB_LOG_ERROR, "glfs_write(%s): on "        \
                  "volume %s failed[%s]", fname, volume,             \
                  strerror(errno));                                  \
              ret = -1;                                              \
              goto label;                                            \
            }                                                        \
            GB_FREE(write);                                          \
          } while (0)

# define  GB_METAUNLOCK(lkfd, volume, ret)                           \
          do {                                                       \
            struct flock lock = {0, };                               \
            lock.l_type = F_UNLCK;                                   \
            if (glfs_posix_lock(lkfd, F_SETLK, &lock)) {             \
              LOG("mgmt", GB_LOG_ERROR, "glfs_posix_lock() on "      \
                  "volume %s failed[%s]", volume, strerror(errno));  \
              ret = -1;                                              \
            }                                                        \
          } while (0)


# define  CALLOC(x)                                                  \
            calloc(1, x)

# define  GB_ALLOC_N(ptr, count)                                     \
            gbAllocN(&(ptr), sizeof(*(ptr)), (count),                \
                     __FILE__, __FUNCTION__, __LINE__)               \

# define  GB_ALLOC(ptr)                                              \
            gbAlloc(&(ptr), sizeof(*(ptr)),                          \
                    __FILE__, __FUNCTION__, __LINE__)

# define  GB_STRDUP(dst, src)                                        \
            gbStrdup(&(dst), src,                                    \
                     __FILE__, __FUNCTION__, __LINE__)

# define  GB_FREE(ptr)                                               \
            gbFree(1 ? (void *) &(ptr) : (ptr))


typedef enum LogLevel {
  GB_LOG_NONE       = 0,
  GB_LOG_EMERGENCY  = 1,
  GB_LOG_ALERT      = 2,
  GB_LOG_CRITICAL   = 3,
  GB_LOG_ERROR      = 4,
  GB_LOG_WARNING    = 5,
  GB_LOG_NOTICE     = 6,
  GB_LOG_INFO       = 7,
  GB_LOG_DEBUG      = 8,
  GB_LOG_TRACE      = 9,

  GB_LOG_MAX
} LogLevel;

static const char *const LogLevelLookup[] = {
  [GB_LOG_NONE]       = "NONE",
  [GB_LOG_EMERGENCY]  = "EMERGENCY",
  [GB_LOG_ALERT]      = "ALERT",
  [GB_LOG_CRITICAL]   = "CRITICAL",
  [GB_LOG_ERROR]      = "ERROR",
  [GB_LOG_WARNING]    = "WARNING",
  [GB_LOG_NOTICE]     = "NOTICE",
  [GB_LOG_INFO]       = "INFO",
  [GB_LOG_DEBUG]      = "DEBUG",
  [GB_LOG_TRACE]      = "TRACE",

  [GB_LOG_MAX]        = NULL,
};

typedef enum Metakey {
  GB_META_INVALID     = 0,
  GB_META_VOLUME      = 1,
  GB_META_GBID        = 2,
  GB_META_SIZE        = 3,
  GB_META_HA          = 4,
  GB_META_ENTRYCREATE = 5,

  GB_METAKEY_MAX
} Metakey;

static const char *const MetakeyLookup[] = {
  [GB_META_INVALID]     = NULL,
  [GB_META_VOLUME]      = "VOLUME",
  [GB_META_GBID]        = "GBID",
  [GB_META_SIZE]        = "SIZE",
  [GB_META_HA]          = "HA",
  [GB_META_ENTRYCREATE] = "ENTRYCREATE",

  [GB_METAKEY_MAX]      = NULL
};

typedef enum MetaStatus {
  GB_CONFIG_SUCCESS    = 0,
  GB_CONFIG_FAIL       = 1,
  GB_CONFIG_INPROGRESS = 2,
  GB_CLEANUP_SUCCESS   = 3,
  GB_CLEANUP_FAIL      = 4,
  GB_CLEANUP_INPROGRES = 5,

  GB_METASTATUS_MAX
} MetaStatus;

static const char *const MetaStatusLookup[] = {
  [GB_CONFIG_SUCCESS]     = "CONFIGSUCCESS",
  [GB_CONFIG_FAIL]        = "CONFIGFAIL",
  [GB_CONFIG_INPROGRESS]  = "CONFIGINPROGRESS",
  [GB_CLEANUP_INPROGRES]  = "CLEANUPINPROGRESS",
  [GB_CLEANUP_SUCCESS]    = "CLEANUPSUCCESS",
  [GB_CLEANUP_FAIL]       = "CLEANUPFAIL",

  [GB_METASTATUS_MAX]     = NULL,
};


int blockMetaKeyEnumParse(const char *opt);

int blockMetaStatusEnumParse(const char *opt);

int gbAlloc(void *ptrptr, size_t size,
            const char *filename, const char *funcname, size_t linenr);

int gbAllocN(void *ptrptr, size_t size, size_t count,
             const char *filename, const char *funcname, size_t linenr);

int gbStrdup(char **dest, const char *src,
             const char *filename, const char *funcname, size_t linenr);

void gbFree(void *ptrptr);

#endif  /* _UTILS_H */
