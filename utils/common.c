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


# define  GB_DEFAULT_SECTOR_SIZE  512

# define round_down(a, b) ({           \
        __typeof__ (a) _a = (a);       \
        __typeof__ (b) _b = (b);       \
        (_a - (_a % _b)); })

ssize_t
glusterBlockParseSize(const char *dom, char *value, int blksize)
{
  char *postfix;
  char *tmp;
  ssize_t sizef;


  if (!value)
    return -1;

  sizef = strtod(value, &postfix);
  if (sizef <= 0) {
    LOG(dom, GB_LOG_ERROR, "size cannot be negative number or zero");
    return -1;
  }

  tmp = postfix;
  if (*postfix == ' ') {
    tmp = tmp + 1;
  }

  if (!blksize) {
    blksize = GB_DEFAULT_SECTOR_SIZE;
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
    if (sizef < blksize) {
        MSG(stderr, "minimum acceptable block size is %d bytes", blksize);
        LOG(dom, GB_LOG_ERROR, "minimum acceptable block size is %d bytes",
            blksize);
        return -1;
    }

    if (sizef % blksize) {
      MSG(stdout, "The size %ld will align to sector size %d bytes",
          sizef, blksize);
      LOG(dom, GB_LOG_ERROR,
          "The target device size %ld will align to the sector size %d",
          sizef, blksize);
      sizef = round_down(sizef, blksize);
    }
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
  LOG(dom, GB_LOG_ERROR,
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
    LOG(dom, GB_LOG_ERROR, "glusterBlockFormatSize() failed");
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


bool
isNumber(char number[])
{
  int i = 0;


  // handle negative numbers
  if (number[0] == '-')
    i = 1;

  for (; number[i] != 0; i++)
  {
    if (!isdigit(number[i]))
      return false;
  }

  return true;
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


void
strToCharArrayDefFree(strToCharArrayDefPtr arr)
{
  size_t i;


  if (!arr) {
    return;
  }

  for (i = 0; i < arr->len; i++) {
     GB_FREE(arr->data[i]);
  }
  GB_FREE(arr->data);
  GB_FREE(arr);
}


strToCharArrayDefPtr
getCharArrayFromDelimitedStr(char *str, char delim)
{
  strToCharArrayDefPtr arr;
  char *tmp;
  char *tok;
  char *base;
  size_t i = 0;

  if (!str) {
    return NULL;
  }

  if (GB_STRDUP(tmp, str) < 0) {
    return NULL;
  }
  base = tmp;

  if (GB_ALLOC(arr) < 0) {
    goto out;
  }

  /* count number of Vols */
  while (*tmp) {
    if (*tmp == delim) {
      arr->len++;
    }
    tmp++;
  }
  arr->len++;
  tmp = base; /* reset vols */


  if (GB_ALLOC_N(arr->data, arr->len) < 0) {
    goto out;
  }

  tok = strtok(tmp, &delim);
  for (i = 0; tok != NULL; i++) {
    if (GB_STRDUP(arr->data[i], tok) < 0) {
      goto out;
    }
    tok = strtok(NULL, &delim);
  }

  GB_FREE(base);
  return arr;

 out:
  GB_FREE(base);
  strToCharArrayDefFree(arr);
  return NULL;
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
