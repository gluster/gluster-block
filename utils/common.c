/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include "common.h"


enum JsonResponseFormat
jsonResponseFormatParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_JSON_MAX;
  }

  if (strlen (opt) < 2 || opt[0] != '-' || opt[1] != '-') {
    /*json option is not given*/
    return GB_JSON_NONE;
  }

  for (i = 0; i < GB_JSON_MAX; i++) {
    if (!strcmp(opt, JsonResponseFormatLookup[i])) {
      return i;
    }
  }

  return i;
}


ssize_t
glusterBlockParseSize(const char *dom, char *value)
{
  char *postfix;
  char *tmp;
  ssize_t sizef;


  if (!value)
    return -1;

  sizef = strtod(value, &postfix);
  if (sizef < 0) {
    LOG(dom, GB_LOG_ERROR, "%s", "size cannot be negative number");
    return -1;
  }

  tmp = postfix;
  if (*postfix == ' ') {
    tmp = tmp + 1;
  }

  switch (*tmp) {
  case 'Y':
    sizef *= 1024;
    /* fall through */
  case 'Z':
    sizef *= 1024;
    /* fall through */
  case 'E':
    sizef *= 1024;
    /* fall through */
  case 'P':
    sizef *= 1024;
    /* fall through */
  case 'T':
    sizef *= 1024;
    /* fall through */
  case 'G':
    sizef *= 1024;
    /* fall through */
  case 'M':
    sizef *= 1024;
    /* fall through */
  case 'K':
  case 'k':
    sizef *= 1024;
    /* fall through */
  case 'B':
  case 'b':
  case '\0':
    return sizef;
    break;
  default:
    LOG(dom, GB_LOG_ERROR, "%s",
         "You may use k/K, M, G or T suffixes for kilobytes, "
         "megabytes, gigabytes and terabytes.");
    return -1;
  }
}


char *
glusterBlockFormatSize(const char *dom, size_t bytes)
{
  char *buf;
  size_t i = 0;
  size_t rem = 0;
  const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"};


  while (bytes >= 1024) {
    rem = (bytes % 1024);
    bytes /= 1024;
    i++;
  }

  if (GB_ASPRINTF(&buf, "%.1f %s", (float)bytes + (float)rem / 1024.0, units[i]) < 0) {
    LOG(dom, GB_LOG_ERROR, "%s", "glusterBlockFormatSize() failed");
    buf = NULL;
  }

  return buf;
}


/* Return value and meaning
 *  1  - true/set
 *  0  - false/unset
 * -1  - unknown string
 */
int
convertStringToTrillianParse(const char *opt)
{
  int i;


  if (!opt) {
    return -1;
  }

  for (i = 1; i < GB_BOOL_MAX; i++) {
    if (!strcmp(opt, ConvertStringToTrillianLookup[i])) {
      return i%2;
    }
  }

  return -1;
}
