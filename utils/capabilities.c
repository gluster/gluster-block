/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include "capabilities.h"


gbCapObj *globalCapabilities;


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


void
gbSetCapabilties(void)
{
  FILE *fp = NULL;
  char *line = NULL;
  size_t len = 0;
  int count = 0;
  int ret;
  gbCapObj *caps = NULL;
  char *p, *sep;
  bool free_caps = true;

  fp = fopen(GB_CAPS_FILE, "r");
  if (fp == NULL) {
    LOG("mgmt", GB_LOG_ERROR,
        "gbSetCapabilties: fopen() failed (%s)", strerror(errno));
    goto out;
  }

  if (GB_ALLOC_N(caps, GB_CAP_MAX) < 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "gbSetCapabilties: calloc() failed (%s)", strerror(errno));
    goto out;
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
      GB_STRCPYSTATIC(caps[count].cap, gbCapabilitiesLookup[ret]);

      /* Part after ':' and before '\n' */
      p = sep + 1;
      sep = strchr(p, '\n');
      *sep = '\0';

      while(isspace((unsigned char)*p)) p++;
      ret = convertStringToTrillianParse(p);
      if (ret >= 0) {
        caps[count].status = ret;
      } else {
        LOG("mgmt", GB_LOG_ERROR,
            "gbSetCapabilties: convertStringToTrillianParse(%s) failed (%s)", p,
            strerror(errno));
        goto out;
      }
      count++;
    }

    GB_FREE(line);
  }

  if (GB_CAP_MAX != count) {
    LOG("mgmt", GB_LOG_ERROR, "gbSetCapabilties: GB_CAP_MAX != %d", count);
    goto out;
  }

  globalCapabilities = caps;
  free_caps = false;

 out:
  if (free_caps) {
    GB_FREE(caps);
  }
  GB_FREE(line);
  if (fp) {
    fclose(fp);
  }

  return;
}
