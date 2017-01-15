#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>

#include "rpc/block.h"
#include "utils.h"


blockTrans *
block_exec_1_svc(char **cmd, struct svc_req *rqstp)
{
  FILE *fp;
  static blockTrans *obj;

  if(GB_ALLOC(obj) < 0)
    return NULL;

  if (GB_ALLOC_N(obj->out, 4096) < 0) {
    GB_FREE(obj);
    return NULL;
  }

  fp = popen(*cmd, "r");
  if (fp != NULL) {
    size_t newLen = fread(obj->out, sizeof(char), 4996, fp);
    if (ferror( fp ) != 0) {
      ERROR("%s", "Error reading command output\n");
    } else {
      obj->out[newLen++] = '\0';
    }
    obj->exit = WEXITSTATUS(pclose(fp));
  }

  return obj;
}
