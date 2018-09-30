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
glusterBlockConfSetOptions(gbConfig *cfg, bool reloading)
{
  unsigned int logLevel;


  /* set logLevel option */
  GB_PARSE_CFG_STR(cfg, GB_LOG_LEVEL, "INFO");
  if (cfg->GB_LOG_LEVEL) {
    logLevel = blockLogLevelEnumParse(cfg->GB_LOG_LEVEL);
    glusterBlockSetLogLevel(logLevel);
  }

  /* set lruCount option */
  GB_PARSE_CFG_INT(cfg, GB_GLFS_LRU_COUNT, LRU_COUNT_DEF);
  if (cfg->GB_GLFS_LRU_COUNT) {
    glusterBlockSetLruCount(cfg->GB_GLFS_LRU_COUNT);
  }
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
  ssize_t m, n, buf_len = GB_BUF_LEN;
  FILE *fp;


  if (GB_ALLOC_N(buf, buf_len) < 0) {
    return NULL;
  }

  fp = fopen(cfg->configPath, "r");
  if (fp == NULL) {
    LOG("mgmt", GB_LOG_ERROR,
        "Failed to open file '%s', %m\n", cfg->configPath);
    GB_FREE(buf);
    return NULL;
  }

  *len = 0;
  p = buf;
  while ((m = getline(&line, &n, fp)) != -1) {
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

  *len += 1;
  buf[*len] = '\0';

  fclose(fp);
  GB_FREE(line);
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
        "option type %d not supported!\n", option->type);
    break;
  }
}

static void
glusterBlockParseOptions(gbConfig *cfg, char *buf, int len, bool reloading)
{
  char *cur = buf, *end = buf + len;


  while (cur < end) {
    /* parse the options from config file to gb_options[] */
    glusterBlockParseOption(&cur, end);
  }

  /* parse the options from gb_options[] to struct gbConfig */
  glusterBlockConfSetOptions(cfg, reloading);
}

static int
glusterBlockLoadConfig(gbConfig *cfg, bool reloading)
{
  ssize_t len = 0;
  char *buf;


  buf = glusterBlockReadConfig(cfg, &len);
  if (buf == NULL) {
    LOG("mgmt", GB_LOG_ERROR,
        "Failed to read file '%s'\n", cfg->configPath);
    return -1;
  }

  glusterBlockParseOptions(cfg, buf, len, reloading);

  GB_FREE(buf);
  return 0;
}

static void *
glusterBlockDynConfigStart(void *arg)
{
  gbConfig *cfg = arg;
  int monitor, wd, len;
  char buf[GB_BUF_LEN];
  struct inotify_event *event;
  char *p;


  monitor = inotify_init();
  if (monitor == -1) {
    LOG("mgmt", GB_LOG_ERROR,
        "Failed to init inotify %d\n", monitor);
    return NULL;
  }

  wd = inotify_add_watch(monitor, cfg->configPath, IN_ALL_EVENTS);
  if (wd == -1) {
    LOG("mgmt", GB_LOG_ERROR,
        "Failed to add \"%s\" to inotify %m\n", cfg->configPath);
    return NULL;
  }

  LOG("mgmt", GB_LOG_INFO,
      "Inotify is watching \"%s\", wd: %d, mask: IN_ALL_EVENTS\n",
      cfg->configPath, wd);

  while (1) {
    len = read(monitor, buf, GB_BUF_LEN);
    if (len == -1) {
      LOG("mgmt", GB_LOG_WARNING, "Failed to read inotify: %d\n", len);
      continue;
    }

    for (p = buf; p < buf + len;) {
      event = (struct inotify_event *)p;

      LOG("mgmt", GB_LOG_INFO, "event->mask: 0x%x\n", event->mask);

      if (event->wd != wd) {
        continue;
      }

      /*
       * If force to write to the unwritable or crashed
       * config file, the vi/vim will try to move and
       * delete the config file and then recreate it again
       * via the *.swp
       */
      if ((event->mask & IN_IGNORED) && !access(cfg->configPath, F_OK)) {
        wd = inotify_add_watch(monitor, cfg->configPath, IN_ALL_EVENTS);
      }

      /* Try to reload the config file */
      if (event->mask & IN_MODIFY || event->mask & IN_IGNORED) {
        glusterBlockLoadConfig(cfg, true);
      }

      p += sizeof(struct inotify_event) + event->len;
    }
  }

  return NULL;
}

gbConfig *
glusterBlockSetupConfig(const char *configPath)
{
  gbConfig *cfg = NULL;
  int ret;


  if (!configPath) {
    configPath = GB_DEF_CONFIGPATH;
  }

  if (GB_ALLOC(cfg) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "Alloc GB config failed for configPath: %s!\n", configPath);
    return NULL;
  }

  if (GB_STRDUP(cfg->configPath, configPath) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "failed to copy configPath: %s\n", configPath);
    goto freeConfig;
  }

  if (glusterBlockLoadConfig(cfg, false)) {
    LOG("mgmt", GB_LOG_ERROR, "Loading GB config failed for configPath: %s!\n", configPath);
    goto freeConfigPath;
  }

  /*
   * If the dynamic reloading thread fails to start, it will fall
   * back to static config
   */
  ret = pthread_create(&cfg->threadId, NULL, glusterBlockDynConfigStart, cfg);
  if (ret) {
    LOG("mgmt", GB_LOG_WARNING,
        "Dynamic config started failed, fallling back to static %d!\n", ret);
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
    LOG("mgmt", GB_LOG_ERROR, "pthread_cancel failed with value %d\n", ret);
    return;
  }

  ret = pthread_join(threadId, &join_retval);
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "pthread_join failed with value %d\n", ret);
    return;
  }

  if (join_retval != PTHREAD_CANCELED) {
    LOG("mgmt", GB_LOG_ERROR, "unexpected join retval: %p\n", join_retval);
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
