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

  switch (tolower(*tmp)) {
  case 'y':
    sizef *= 1024;
    /* fall through */
  case 'z':
    sizef *= 1024;
    /* fall through */
  case 'e':
    sizef *= 1024;
    /* fall through */
  case 'p':
    sizef *= 1024;
    /* fall through */
  case 't':
    sizef *= 1024;
    /* fall through */
  case 'g':
    sizef *= 1024;
    /* fall through */
  case 'm':
    sizef *= 1024;
    /* fall through */
  case 'k':
    sizef *= 1024;
    /* fall through */
  case 'b':
  case '\0':
    break;
  default:
    goto fail;
  }

  if ((strlen(tmp) > 1) &&
      ((tmp[0] == 'b') || (tmp[0] == 'B') ||
       (strncasecmp(tmp+1, "ib", strlen(tmp+1)) != 0)))
  {
    goto fail;
  }

  /* success */
  return sizef;

fail:
  LOG(dom, GB_LOG_ERROR, "%s",
      "Unknown size unit. "
      "You may use b/B, k/K(iB), m/M(iB), g/G(iB), and t/T(iB) suffixes for "
      "bytes, kibibytes, mebibytes, gibibytes, and tebibytes.");
  return -1;
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


void
blockServerDefFree(blockServerDefPtr blkServers)
{
  size_t i;


  if (!blkServers) {
    return;
  }

  for (i = 0; i < blkServers->nhosts; i++) {
     GB_FREE(blkServers->hosts[i]);
  }
  GB_FREE(blkServers->hosts);
  GB_FREE(blkServers);
}


bool
blockhostIsValid(char *status)
{
  switch (blockMetaStatusEnumParse(status)) {
  case GB_CONFIG_SUCCESS:
  case GB_CLEANUP_INPROGRESS:
  case GB_AUTH_ENFORCEING:
  case GB_AUTH_ENFORCED:
  case GB_AUTH_ENFORCE_FAIL:
  case GB_AUTH_CLEAR_ENFORCED:
  case GB_AUTH_CLEAR_ENFORCEING:
  case GB_AUTH_CLEAR_ENFORCE_FAIL:
  case GB_RP_SUCCESS:
  case GB_RP_FAIL:
  case GB_RP_INPROGRESS:
  case GB_RS_SUCCESS:
  case GB_RS_FAIL:
  case GB_RS_INPROGRESS:
    return TRUE;
  }

  return FALSE;
}
