/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include "common.h"



ssize_t
glusterBlockCreateParseSize(char *value)
{
  char *postfix;
  char *tmp;
  ssize_t sizef;

  if (!value)
    return -1;

  sizef = strtod(value, &postfix);
  if (sizef < 0) {
    ERROR("%s", "size cannot be negative number\n");
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
  case 'b':
  case '\0':
    return sizef;
    break;
  default:
  /*TODO: Log this instead of printing
     ERROR("%s", "You may use k/K, M, G or T suffixes for "
           "kilobytes, megabytes, gigabytes and terabytes."); */
    return -1;
  }
}
