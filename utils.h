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
            fclose(fd);                                              \
          } while (0)

# define  METALOCK(lkfd)                                             \
          do {                                                       \
            struct flock lock = {0, };                               \
            memset (&lock, 0, sizeof(lock));                         \
            lock.l_type = F_WRLCK;                                   \
            if (glfs_posix_lock (lkfd, F_SETLKW, &lock)) {           \
              LOG("mgmt", ERROR, "%s", "glfs_posix_lock: failed");   \
              ret = -1;                                              \
              goto out;                                              \
            }                                                        \
          } while (0)

# define  METAUPDATE(a, ...)                                         \
          do {                                                       \
            char *write;                                             \
            asprintf(&write, __VA_ARGS__);                           \
            if(glfs_write (a, write, strlen(write), 0) < 0) {        \
              LOG("mgmt", ERROR, "%s", "glfs_write: failed");        \
              ret = -1;                                              \
              goto out;                                              \
            }                                                        \
            GB_FREE(write);                                          \
          } while (0)

# define  METAUNLOCK(lkfd)                                           \
          do {                                                       \
            struct flock lock = {0, };                               \
            lock.l_type = F_UNLCK;                                   \
            if (glfs_posix_lock(lkfd, F_SETLKW, &lock)) {            \
              LOG("mgmt", ERROR, "%s", "glfs_posix_lock: failed");   \
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
    NONE       = 0,
    EMERGENCY  = 1,
    ALERT      = 2,
    CRITICAL   = 3,
    ERROR      = 4,
    WARNING    = 5,
    NOTICE     = 6,
    INFO       = 7,
    DEBUG      = 8,
    TRACE      = 9,

    LOGLEVEL__MAX = 10      /* Updata this when add new level */
} LogLevel;

static const char *const LogLevelLookup[] = {
    [NONE]       = "NONE",
    [EMERGENCY]  = "EMERGENCY",
    [ALERT]      = "ALERT",
    [CRITICAL]   = "CRITICAL",
    [ERROR]      = "ERROR",
    [WARNING]    = "WARNING",
    [NOTICE]     = "NOTICE",
    [INFO]       = "INFO",
    [DEBUG]      = "DEBUG",
    [TRACE]      = "TRACE",

    [LOGLEVEL__MAX] = NULL,
};

typedef enum Metakey {
  VOLUME = 0,
  GBID   = 1,
  SIZE   = 2,
  HA     = 3,
  ENTRYCREATE = 4,

  METAKEY__MAX = 5      /* Updata this when add new Key */
} Metakey;

static const char *const MetakeyLookup[] = {
    [VOLUME] = "VOLUME",
    [GBID]   = "GBID",
    [SIZE]   = "SIZE",
    [HA]     = "HA",
    [ENTRYCREATE] = "ENTRYCREATE",
    [METAKEY__MAX] = NULL,
};

typedef enum MetaStatus {
  CONFIGSUCCESS = 0,
  CONFIGFAIL   = 1,
  CONFIGINPROGRESS = 2,
  CLEANUPSUCCESS = 3,
  CLEANUPFAIL = 4,
  CLEANUPINPROGRES = 5,

  METASTATUS__MAX = 6      /* Updata this when add new Status type */
} MetaStatus;

static const char *const MetaStatusLookup[] = {
    [CONFIGINPROGRESS] = "CONFIGINPROGRESS",
    [CONFIGSUCCESS] = "CONFIGSUCCESS",
    [CONFIGFAIL] = "CONFIGFAIL",
    [CLEANUPINPROGRES] = "CLEANUPINPROGRESS",
    [CLEANUPSUCCESS] = "CLEANUPSUCCESS",
    [CLEANUPFAIL] = "CLEANUPFAIL",

    [METASTATUS__MAX] = NULL,
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
