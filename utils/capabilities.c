/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include "capabilities.h"


int
gbCapabilitiesEnumParse(const char *cap)
{
  int i;


  if (!cap) {
    return GB_CAP_MAX;
  }

  for (i = 0; i < GB_CAP_MAX; i++) {
    if (!strcmp(cap, gbCapabilitiesLookup[i])) {
      return i;
    }
  }

  return i;
}


int
gbSetCapabilties(blockResponse **c)
{
  FILE * fp;
  char *line = NULL;
  size_t len = 0;
  int count = 0;
  int ret = 0;
  blockResponse *reply = *c;
  gbCapObj *caps = NULL;
  char *p, *sep;


  fp = fopen(GB_CAPS_FILE, "r");
  if (fp == NULL) {
    return -1;
  }

  if (GB_ALLOC_N(caps, GB_CAP_MAX) < 0) {
    fclose(fp);
    return -1;
  }

  while ((getline(&line, &len, fp)) != -1) {
    if (!line) {
      continue;
    }
    if ((line[0] == '\n') || (line[0] == '#')) {
      GB_FREE(line);
      continue;
    }

    /* Part before ':' */
    p = line;
    sep = strchr(p, ':');
    *sep = '\0';

    ret = gbCapabilitiesEnumParse(p);
    if (ret != GB_CAP_MAX) {
      strncpy(caps[count].cap, gbCapabilitiesLookup[ret], 256);

      /* Part after ':' and before '\n' */
      p = sep + 1;
      sep = strchr(p, '\n');
      *sep = '\0';

      while(isspace((unsigned char)*p)) p++;
      ret = convertStringToTrillianParse(p);
      if (ret >= 0) {
        caps[count].status = ret;
      } else {
        ret = -1;
        goto out;
      }
      count++;
    }

    GB_FREE(line);
  }

  if (GB_CAP_MAX != count) {
    ret = -1;
    goto out;
  }

  reply->xdata.xdata_len = GB_CAP_MAX * sizeof(gbCapObj);
  reply->xdata.xdata_val = (char *) caps;

  ret = 0;
 out:
  GB_FREE(line);
  fclose(fp);

  return ret;
}
