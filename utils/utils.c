/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include "utils.h"



int
glusterBlockCLIOptEnumParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_CLI_OPT_MAX;
  }

  for (i = 0; i < GB_CLI_OPT_MAX; i++) {
    if (!strcmp(opt, gbCmdlineOptLookup[i])) {
      return i;
    }
  }

  return i;
}


int
glusterBlockCLICreateOptEnumParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_CLI_CREATE_OPT_MAX;
  }

  /* i = 11, enum start look gbCmdlineCreateOption */
  for (i = 11; i < GB_CLI_CREATE_OPT_MAX; i++) {
    if (!strcmp(opt, gbCmdlineCreateOptLookup[i])) {
      return i;
    }
  }

  return i;
}


int
glusterBlockCLICommonOptEnumParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_CLI_COMMON_OPT_MAX;
  }

  /* i = 21, enum start look gbCmdlineCreateOption */
  for (i = 21; i < GB_CLI_COMMON_OPT_MAX; i++) {
    if (!strcmp(opt, gbCmdlineCommonOptLookup[i])) {
      return i;
    }
  }

  return i;
}


int
blockMetaKeyEnumParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_METAKEY_MAX;
  }

  for (i = 0; i < GB_METAKEY_MAX; i++) {
    if (!strcmp(opt, MetakeyLookup[i])) {
      return i;
    }
  }

  return i;
}


int
blockMetaStatusEnumParse(const char *opt)
{
  int i;


  if (!opt) {
    return GB_METASTATUS_MAX;
  }

  for (i = 0; i < GB_METASTATUS_MAX; i++) {
    if (!strcmp(opt, MetaStatusLookup[i])) {
      return i;
    }
  }

  return i;
}


int
gbAlloc(void *ptrptr, size_t size,
        const char *filename, const char *funcname, size_t linenr)
{
  *(void **)ptrptr = calloc(1, size);

  if (*(void **)ptrptr == NULL) {
    errno = ENOMEM;
    return -1;
  }

  return 0;
}


int
gbAllocN(void *ptrptr, size_t size, size_t count,
         const char *filename, const char *funcname, size_t linenr)
{
  *(void**)ptrptr = calloc(count, size);

  if (*(void**)ptrptr == NULL) {
    errno = ENOMEM;
    return -1;
  }

  return 0;
}


void
gbFree(void *ptrptr)
{
  int save_errno = errno;


  if(*(void**)ptrptr == NULL) {
   return;
  }

  free(*(void**)ptrptr);
  *(void**)ptrptr = NULL;
  errno = save_errno;
}


int
gbStrdup(char **dest, const char *src,
         const char *filename, const char *funcname, size_t linenr)
{
  *dest = NULL;

  if (!src) {
    return 0;
  }

  if (!(*dest = strdup(src))) {
    return -1;
  }

  return 0;
}
