/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <pthread.h>

#include "utils.h"
#include "lru.h"

typedef enum {
  GB_OPT_NONE = 0,
  GB_OPT_INT, /* type int */
  GB_OPT_STR, /* type string */
  GB_OPT_BOOL, /* type boolean */
  GB_OPT_MAX,
} gbOptionType;

typedef struct gbConfOption {
  struct list_head list;

  char *key;
  gbOptionType type;
  union {
    int optInt;
    bool optBool;
    char *optStr;
  };
} gbConfOption;

/*
 * System config for gluster-block, for now there are only 3 option types supported:
 * 1, The "int type" option, for example:
 *	gb_int = 2
 *
 * 2, The "string type" option, for example:
 *	gb_str = "Tom"  --> Tom
 *    or
 *	gb_str = 'Tom'  --> Tom
 *    or
 *	gb_str = 'Tom is a "boy"' ---> Tom is a "boy"
 *    or
 *	gb_str = "'T' is short for Tom" --> 'T' is short for Tom
 *
 * 3, The "boolean type" option, for example:
 *	gb_bool
 *
 * ========================
 * How to add new options ?
 *
 * Using "GB_LOG_LEVEL" as an example:
 *
 * 1, Add logLevel member in:
 *	struct gbConfig {
 *	  char *GB_LOG_LEVEL;
 *	};
 *    in file utils.h.
 *
 * 2, Add the following option in "gluster-blockd" file as default:
 *	GB_LOG_LEVEL=INFO
 *    or
 *	GB_LOG_LEVEL = INFO
 *    or
 *	GB_LOG_LEVEL = "INFO"
 *    or
 *	GB_LOG_LEVEL = 'INFO'
 *
 *    Note: the option name in config file must be the same as in
 *    gbConfig.
 *
 * 3, You should add your own set method in:
 *	static void glusterBlockConfSetOptions(gbConfig *cfg)
 *	{
 *		GB_PARSE_CFG_STR(cfg, GB_LOG_LEVEL);
 *	}
 * 4, Then add your own free method if it's a STR KEY:
 *	static void glusterBlockConfFreeStrKeys(gbConfig *cfg)
 *	{
 *		GB_FREE_CFG_STR_KEY(cfg, 'STR KEY');
 *	}
 *
 * Note: For now, if the options have been changed in config file, the
 * system config reload thread daemon will try to update them for the
 * gluster-blockd daemon.
 */

static LIST_HEAD(gb_options);

static gbConfOption *
glusterBlockGetOption(const char *key)
{
  struct list_head *pos;
  gbConfOption *option;


  list_for_each(pos, &gb_options) {
    option = list_entry(pos, gbConfOption, list);
      if (!strcmp(option->key, key)) {
        return option;
      }
  }

  return NULL;
}

/* The default value should be specified here,
 * so the next time when users comment out an
 * option in config file, here it will set the
 * default value back.
 */
# define GB_PARSE_CFG_INT(cfg, key, def) \
        do { \
          gbConfOption *option; \
          option = glusterBlockGetOption(#key); \
          if (option) { \
            cfg->key = option->optInt; \
            option->optInt = def; \
          } \
        } while (0)

# define GB_PARSE_CFG_BOOL(cfg, key, def) \
        do { \
          struct gbConfOption *option; \
          option = glusterBlockGetOption(#key); \
          if (option) { \
            cfg->key = option->optBool; \
            option->optBool = def; \
          } \
        } while (0)

# define GB_PARSE_CFG_STR(cfg, key, def) \
        do { \
          struct gbConfOption *option; \
          char buf[1024]; \
          option = glusterBlockGetOption(#key); \
          if (option) { \
            if (cfg->key) \
              GB_FREE(cfg->key); \
            GB_STRDUP(cfg->key, option->optStr); \
            if (option->optStr) \
              GB_FREE(option->optStr); \
            snprintf(buf, 1024, "%s", def); \
            GB_STRDUP(option->optStr, buf); \
          } \
        } while (0);

# define GB_FREE_CFG_STR_KEY(cfg, key) \
        do { \
          GB_FREE(cfg->key); \
        } while (0);

static void
glusterBlockConfSetOptions(gbConfig *cfg, bool getLogDir)
{
  unsigned int logLevel;


  /* set logdir option */
  GB_PARSE_CFG_STR(cfg, GB_LOG_DIR, GB_LOGDIR_DEF);
  if (getLogDir) {
    return;
  }
  if (cfg->GB_LOG_DIR) {
    glusterBlockSetLogDir(cfg->GB_LOG_DIR);
  }

  /* set logLevel option */
  GB_PARSE_CFG_STR(cfg, GB_LOG_LEVEL, "INFO");
  if (cfg->GB_LOG_LEVEL) {
    logLevel = blockLogLevelEnumParse(cfg->GB_LOG_LEVEL);
    glusterBlockSetLogLevel(logLevel);
  }

  /* set lruCount option */
  if (gbCtx != GB_CLI_MODE ) {
    GB_PARSE_CFG_INT(cfg, GB_GLFS_LRU_COUNT, LRU_COUNT_DEF);
    if (cfg->GB_GLFS_LRU_COUNT) {
      glusterBlockSetLruCount(cfg->GB_GLFS_LRU_COUNT);
    }
  }

  GB_PARSE_CFG_INT(cfg, GB_CLI_TIMEOUT, CLI_TIMEOUT_DEF);
  /* NOTE: we don't use CLI_TIMEOUT in daemon at the moment
   * TODO: use gbConf in cli too, for logLevel/LogDir and other future options
   *
   * if (cfg->GB_CLI_TIMEOUT) {
   *  glusterBlockSetCliTimeout(cfg->GB_CLI_TIMEOUT);
   * }
   */

  /* add your new config options */
}

static void
glusterBlockConfFreeStrKeys(gbConfig *cfg)
{
  /* add your str type config options
   *
   * For example:
   * GB_FREE_CFG_STR_KEY(cfg, 'STR KEY');
   */
   GB_FREE_CFG_STR_KEY(cfg, GB_LOG_DIR);
   GB_FREE_CFG_STR_KEY(cfg, GB_LOG_LEVEL);
}

static bool
gluserBlockIsBlankOrCommentLine(char *line, ssize_t len)
{
  char *p = line;


  while (p <= line + len) {
    if (isblank(*p) || *p == '\n' || *p == '\r' || *p == '\0') {
      p++;
    } else if (*p == '#') {
      return true;
    } else {
      return false;
    }
  }

  return true;
}

# define GB_BUF_LEN 1024
static char *
glusterBlockReadConfig(gbConfig *cfg, ssize_t *len)
{
  int save = errno;
  char *p, *buf, *line = NULL;
  ssize_t m, buf_len = GB_BUF_LEN;
  int i, count = 0;
  size_t n;
  FILE *fp;
  bool empty = true;


  if (GB_ALLOC_N(buf, buf_len) < 0) {
    return NULL;
  }

retry:
  count++;
  for (i = 0; i < 5; i++) {
    if ((fp = fopen(cfg->configPath, "r")) == NULL) {
      /* give a moment for editor to restore
       * the conf-file after edit and save */
      sleep(1);
      continue;
    }
    break;
  }
  if (fp == NULL) {
    LOG("mgmt", GB_LOG_ERROR,
        "Failed to open file '%s'", cfg->configPath);
    GB_FREE(buf);
    return NULL;
  }

  *len = 0;
  p = buf;
  while ((m = getline(&line, &n, fp)) != -1) {
    empty = false;
    if (gluserBlockIsBlankOrCommentLine(line, m)) {
      continue;
    }

    if (*len + m >= buf_len) {
      buf_len += GB_BUF_LEN;
      if (GB_REALLOC_N(buf, buf_len) < 0) {
	      GB_FREE(line);
	      fclose(fp);
	      GB_FREE(buf);
	      return NULL;
      }
      p = buf + *len;
    }
    GB_STRCPY(p, line, m + 1);
    p += m;
    *len += m;
  }

  GB_FREE(line);
  fclose(fp);

  /*
   * In-case if the editor (vim) follows write to a new file (.swp, .tmp ..)
   * and move it to actual file name later. There is a window, where we will
   * encounter one case that the file data is not flushed to the disk, so in
   * another process(here) when reading it will be empty.
   *
   * Let just wait and try again.
   */
  if (empty == true ) {
    if (count <= 5) {
      LOG("mgmt", GB_LOG_DEBUG,
          "failed to read the config from file, probably your editors savefile"
          " transaction is conflicting, retrying (%d/5) time(s)", count);
      sleep(1);
      goto retry;
    }

    GB_FREE(buf);
    goto out;
  }

  buf[++(*len)] = '\0';

out:
  errno = save;
  return buf;
}

# define MAX_KEY_LEN 64
# define MAX_VAL_STR_LEN 256

static gbConfOption *
glusterBlockRegisterOption(char *key, gbOptionType type)
{
  struct gbConfOption *option;


  if (GB_ALLOC(option) < 0) {
    return NULL;
  }

  if (GB_STRDUP(option->key, key) < 0) {
    goto freeOption;
  }
  option->type = type;
  INIT_LIST_HEAD(&option->list);

  list_add_tail(&option->list, &gb_options);
  return option;

freeOption:
  GB_FREE(option);
  return NULL;
}

/* end of line */
#define __EOL(c) (((c) == '\n') || ((c) == '\r'))

#define GB_TO_LINE_END(x, y) \
       do { \
        while ((x) < (y) && !__EOL(*(x))) \
          { (x)++; } \
       } while (0);

static void
glusterBlockParseOption(char **cur, const char *end)
{
  struct gbConfOption *option;
  gbOptionType type;
  char *p = *cur, *q = *cur, *r, *s;


  while (isblank(*p)) {
    p++;
  }

  GB_TO_LINE_END(q, end);
  *q = '\0';
  *cur = q + 1;

  /* parse the boolean type option */
  s = r = strchr(p, '=');
  if (!r) {
    /* boolean type option at file end or line end */
    r = p;
    while (!isblank(*r) && r < q) {
      r++;
    }
    *r = '\0';
    option = glusterBlockGetOption(p);
    if (!option) {
      option = glusterBlockRegisterOption(p, GB_OPT_BOOL);
    }

    if (option) {
      option->optBool = true;
    }

    return;
  }
  /* skip character '='  */
  s++;
  r--;
  while (isblank(*r)) {
    r--;
  }
  r++;
  *r = '\0';

  option = glusterBlockGetOption(p);
  if (!option) {
    r = s;
    while (isblank(*r)) {
      r++;
    }

    if (isdigit(*r)) {
      type = GB_OPT_INT;
    } else {
      type = GB_OPT_STR;
    }

    option = glusterBlockRegisterOption(p, type);
    if (!option) {
      return;
    }
  }

  /* parse the int/string type options */
  switch (option->type) {
  case GB_OPT_INT:
    while (!isdigit(*s)) {
      s++;
    }
    r = s;
    while (isdigit(*r)) {
      r++;
    }
    *r= '\0';

    option->optInt = atoi(s);
    break;
  case GB_OPT_STR:
    while (isblank(*s)) {
      s++;
    }
    /* skip first " or ' if exist */
    if (*s == '"' || *s == '\'') {
      s++;
    }
    r = q - 1;
    while (isblank(*r)) {
      r--;
    }
    /* skip last " or ' if exist */
    if (*r == '"' || *r == '\'') {
      *r = '\0';
    }

    /* free if this is reconfig */
    if (option->optStr) {
      GB_FREE(option->optStr);
    }
    GB_STRDUP(option->optStr, s);
    break;
  default:
    LOG("mgmt", GB_LOG_ERROR,
        "option type %d not supported!", option->type);
    break;
  }
}

static void
glusterBlockParseOptions(gbConfig *cfg, char *buf, int len, bool getLogDir)
{
  char *cur = buf, *end = buf + len;


  while (cur < end) {
    /* parse the options from config file to gb_options[] */
    glusterBlockParseOption(&cur, end);
  }

  /* parse the options from gb_options[] to struct gbConfig */
  glusterBlockConfSetOptions(cfg, getLogDir);
}

int
glusterBlockLoadConfig(gbConfig *cfg, bool getLogDir)
{
  ssize_t len = 0;
  char *buf;


  buf = glusterBlockReadConfig(cfg, &len);
  if (buf == NULL) {
    LOG("mgmt", GB_LOG_ERROR,
        "Failed to read file '%s'", cfg->configPath);
    return -1;
  }

  glusterBlockParseOptions(cfg, buf, len, getLogDir);

  GB_FREE(buf);
  return 0;
}


char *
glusterBlockDynConfigGetLogDir(void)
{
  gbConfig *cfg = NULL;
  char *logDir = NULL;
  char *configPath = GB_DEF_CONFIGPATH;


  if (GB_ALLOC(cfg) < 0) {
    MSG(stderr, "Alloc GB config failed for configPath: %s!", configPath);
    return NULL;
  }

  if (GB_STRDUP(cfg->configPath, configPath) < 0) {
    MSG(stderr, "failed to copy configPath: %s", configPath);
    goto freeConfig;
  }

  if (glusterBlockLoadConfig(cfg, true)) {
    MSG(stderr, "Loading GB config failed for configPath: %s!", configPath);
    goto freeConfigPath;
  }

  if (cfg->GB_LOG_DIR) {
    if (GB_STRDUP(logDir, cfg->GB_LOG_DIR) < 0) {
      MSG(stderr, "failed to copy logDir: %s", cfg->GB_LOG_DIR);
      logDir = NULL;
    }
  }

freeConfigPath:
  GB_FREE(cfg->configPath);
freeConfig:
  GB_FREE(cfg);

  return logDir;
}


static void *
glusterBlockDynConfigStart(void *arg)
{
  gbConfig *cfg = arg;
  int monitor, wd, len;
  char buf[GB_BUF_LEN];
  struct inotify_event *event;
  struct timespec mtim = {0, }; /* Time of last modification.  */
  struct stat statbuf;
  char *p;


  monitor = inotify_init();
  if (monitor == -1) {
    LOG("mgmt", GB_LOG_ERROR,
        "Failed to init inotify %d", monitor);
    return NULL;
  }

  /* Editors (vim, nano ..) follow different approaches to save conf file.
   * The two commonly followed techniques are to overwrite the existing
   * file, or to write to a new file (.swp, .tmp ..) and move it to actual
   * file name later. In the later case, the inotify fails, because the
   * file it's been intended to watch no longer exists, as the new file
   * is a different file with just a same name.
   * To handle both the file save approaches mentioned above, it is better
   * we watch the directory and filter for MODIFY events.
   */
  wd = inotify_add_watch(monitor, GB_DEF_CONFIGDIR, IN_MODIFY);
  if (wd == -1) {
    LOG("mgmt", GB_LOG_ERROR,
        "Failed to add \"%s\" to inotify (%d)", GB_DEF_CONFIGDIR, monitor);
    return NULL;
  }

  LOG("mgmt", GB_LOG_INFO,
      "Inotify is watching \"%s\", wd: %d, mask: IN_MODIFY", GB_DEF_CONFIGDIR, wd);

  while (1) {
    len = read(monitor, buf, GB_BUF_LEN);
    if (len == -1) {
      LOG("mgmt", GB_LOG_WARNING, "Failed to read inotify: %d", len);
      continue;
    }

    for (p = buf; p < buf + len; p += sizeof(*event) + event->len) {
      event = (struct inotify_event *)p;

      LOG("mgmt", GB_LOG_DEBUG, "event->mask: 0x%x", event->mask);

      if (event->wd != wd) {
        continue;
      }

      /* If stat fails we will skip the modify time check */
      if (!stat(cfg->configPath, &statbuf)) {
          if (statbuf.st_mtim.tv_sec == mtim.tv_sec &&
              statbuf.st_mtim.tv_nsec == mtim.tv_nsec) {
              continue;
          }
      }

      mtim.tv_sec = statbuf.st_mtim.tv_sec;
      mtim.tv_nsec = statbuf.st_mtim.tv_nsec;

      /* Try to reload the config file */
      if (event->mask & IN_MODIFY) {
        glusterBlockLoadConfig(cfg, false);
      }
    }
  }

  inotify_rm_watch(monitor, wd);
  return NULL;
}

gbConfig *
glusterBlockSetupConfig(void)
{
  gbConfig *cfg = NULL;
  char *configPath = GB_DEF_CONFIGPATH;
  int ret;


  if (GB_ALLOC(cfg) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "Alloc GB config failed for configPath: %s!", configPath);
    return NULL;
  }

  if (GB_STRDUP(cfg->configPath, configPath) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "failed to copy configPath: %s", configPath);
    goto freeConfig;
  }

  if (glusterBlockLoadConfig(cfg, false)) {
    LOG("mgmt", GB_LOG_ERROR, "Loading GB config failed for configPath: %s!", configPath);
    goto freeConfigPath;
  }

  /*
   * If the dynamic reloading thread fails to start, it will fall
   * back to static config
   */
  ret = pthread_create(&cfg->threadId, NULL, glusterBlockDynConfigStart, cfg);
  if (ret) {
    LOG("mgmt", GB_LOG_WARNING,
        "Dynamic config started failed, fallling back to static %d!", ret);
  } else {
    cfg->isDynamic = true;
  }

  return cfg;

freeConfigPath:
  GB_FREE(cfg->configPath);
freeConfig:
  GB_FREE(cfg);
  return NULL;
}

static void
glusterBlockCancelConfigThread(gbConfig *cfg)
{
  pthread_t threadId = cfg->threadId;
  void *join_retval;
  int ret;


  ret = pthread_cancel(threadId);
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "pthread_cancel failed with value %d", ret);
    return;
  }

  ret = pthread_join(threadId, &join_retval);
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "pthread_join failed with value %d", ret);
    return;
  }

  if (join_retval != PTHREAD_CANCELED) {
    LOG("mgmt", GB_LOG_ERROR, "unexpected join retval: %p", join_retval);
  }
}

void
glusterBlockDestroyConfig(gbConfig *cfg)
{
  struct list_head *pos, *q;
  gbConfOption *option;


  if (!cfg) {
    return;
  }

  if (cfg->isDynamic) {
    glusterBlockCancelConfigThread(cfg);
  }

  list_for_each_safe(pos, q, &gb_options) {
    option = list_entry(pos, gbConfOption, list);
    list_del(&option->list);

    if (option->type == GB_OPT_STR) {
      GB_FREE(option->optStr);
    }
    GB_FREE(option->key);
    GB_FREE(option);
  }

  glusterBlockConfFreeStrKeys(cfg);
  GB_FREE(cfg->configPath);
  GB_FREE(cfg);
}
