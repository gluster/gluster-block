/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# ifndef   _COMMON_H
# define   _COMMON_H   1

# include "utils.h"
# include "block.h"

# define  GB_LOGDIR              DATADIR "/log/gluster-block"
# define  GB_INFODIR             DATADIR "/run"

# define  GB_LOCK_FILE           GB_INFODIR "/gluster-blockd.lock"
# define  GB_UNIX_ADDRESS        GB_INFODIR "/gluster-blockd.socket"
# define  GB_TCP_PORT            24006

# define  DAEMON_LOG_FILE        GB_LOGDIR "/gluster-blockd.log"
# define  CLI_LOG_FILE           GB_LOGDIR "/gluster-block-cli.log"
#define   DEVNULLPATH            "/dev/null"

# define  GFAPI_LOG_FILE         GB_LOGDIR "/gluster-block-gfapi.log"
# define  GFAPI_LOG_LEVEL        7

# define  CONFIGSHELL_LOG_FILE   GB_LOGDIR "/gluster-block-configshell.log"

# define  GB_METADIR             "/block-meta"
# define  GB_STOREDIR            "/block-store"
# define  GB_TXLOCKFILE          "meta.lock"

# define  SUN_PATH_MAX           (sizeof(struct sockaddr_un) - sizeof(unsigned short int)) /*sun_family*/


static const char *const JsonResponseFormatLookup[] = {
  [GB_JSON_NONE]            = "",

  [GB_JSON_PLAIN]    = "--json-plain",
  [GB_JSON_SPACED]   = "--json-spaced",
  [GB_JSON_PRETTY]   = "--json-pretty",
  [GB_JSON_DEFAULT]  = "--json",

  [GB_JSON_MAX]             = NULL,
};


/* Always add new boolean data in a way that, word with jist
 * 'yes/true' first to assign a odd number to it */
typedef enum  ConvertStringToTrillian {
  GB_BOOL_YES             = 1,
  GB_BOOL_NO              = 2,

  GB_BOOL_TRUE            = 3,
  GB_BOOL_FALSE           = 4,

  GB_BOOL_ENABLE          = 5,
  GB_BOOL_DISABLE         = 6,

  GB_BOOL_ONE             = 7,
  GB_BOOL_ZERO            = 8,

  GB_BOOL_SET             = 9,
  GB_BOOL_UNSET           = 10,

  GB_BOOL_FULL            = 11,

  GB_BOOL_MAX
} ConvertStringToBool;


static const char *const ConvertStringToTrillianLookup[] = {
  [GB_BOOL_YES]             = "yes",
  [GB_BOOL_NO]              = "no",

  [GB_BOOL_TRUE]            = "true",
  [GB_BOOL_FALSE]           = "false",

  [GB_BOOL_ENABLE]          = "enable",
  [GB_BOOL_DISABLE]         = "disable",

  [GB_BOOL_ONE]             = "1",   /* true */
  [GB_BOOL_ZERO]            = "0",

  [GB_BOOL_SET]             = "set",
  [GB_BOOL_UNSET]           = "unset",

  [GB_BOOL_FULL]            = "full",

  [GB_BOOL_MAX]             = NULL,
};


enum JsonResponseFormat jsonResponseFormatParse(const char *opt);

ssize_t glusterBlockParseSize(const char *dom, char *value);

char* glusterBlockFormatSize(const char *dom, size_t bytes);

int convertStringToTrillianParse(const char *opt);

# endif /* _COMMON_H */
