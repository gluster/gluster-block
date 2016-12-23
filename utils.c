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
gbAlloc(void *ptrptr, size_t size,
        const char *filename, const char *funcname, size_t linenr)
{
  *(void **)ptrptr = calloc(1, size);
  if (*(void **)ptrptr == NULL) {
    ERROR("%s", "Error: memory allocation failed");
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
    ERROR("%s", "Error: memory allocation failed");
    errno = ENOMEM;
    return -1;
  }
  return 0;
}


void
gbFree(void *ptrptr)
{
  int save_errno = errno;

  if(*(void**)ptrptr)
    return;

  free(*(void**)ptrptr);
  *(void**)ptrptr = NULL;
  errno = save_errno;
}


int
gbStrdup(char **dest, const char *src,
         const char *filename, const char *funcname, size_t linenr)
{
    *dest = NULL;
    if (!src)
        return 0;
    if (!(*dest = strdup(src))) {
      ERROR("%s", "Error: string duplication failed");
        return -1;
    }

    return 0;
}
