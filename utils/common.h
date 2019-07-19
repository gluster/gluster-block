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

# define   GB_DELIMITER         ','
# define   CLI_TIMEOUT_DEF      300


typedef struct blockServerDef {
  size_t nhosts;
  char   **hosts;
} blockServerDef;
typedef blockServerDef *blockServerDefPtr;


typedef struct strToCharArrayDef {
  size_t len;
  char   **data;
} strToCharArrayDef;
typedef strToCharArrayDef *strToCharArrayDefPtr;


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


strToCharArrayDefPtr getCharArrayFromDelimitedStr(char *str, char delim);

void strToCharArrayDefFree(strToCharArrayDefPtr arr);

enum JsonResponseFormat jsonResponseFormatParse(const char *opt);

ssize_t glusterBlockParseSize(const char *dom, char *value, int blksize);

char* glusterBlockFormatSize(const char *dom, size_t bytes);

int convertStringToTrillianParse(const char *opt);

bool isNumber(char number[]);

void blockServerDefFree(blockServerDefPtr blkServers);

bool blockhostIsValid(char *status);

int glusterBlockLoadConfig(gbConfig *cfg, bool getLogDir);

# endif /* _COMMON_H */
