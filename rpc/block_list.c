/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "block_common.h"


static blockResponse *
block_list_cli_1_svc_st(blockListCli *blk, struct svc_req *rqstp)
{
  blockResponse *reply = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  struct glfs_fd *tgmdfd = NULL;
  struct dirent *entry;
  char *tmp = NULL;
  char *filelist = NULL;
  json_object *json_obj = NULL;
  json_object *json_array = NULL;
  int errCode = -1;
  char *errMsg = NULL;


  LOG("mgmt", GB_LOG_INFO, "list cli request, volume=%s", blk->volume);

  if (GB_ALLOC(reply) < 0) {
    goto optfail;
  }

  if (blk->json_resp) {
    json_obj = json_object_new_object();
    json_array = json_object_new_array();
  }

  errCode = 0;
  glfs = glusterBlockVolumeInit(blk->volume, &errCode, &errMsg);
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockVolumeInit(%s) failed", blk->volume);
    goto optfail;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume, &errCode, &errMsg);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s %s", FAILED_CREATING_META, blk->volume);
    goto optfail;
  }

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, errCode, errMsg, optfail);

  tgmdfd = glfs_opendir (glfs, GB_METADIR);
  if (!tgmdfd) {
    unsigned int errorCode = errno;
    GB_ASPRINTF (&errMsg, "Not able to open metadata directory for volume "
                 "%s[%s]", blk->volume, strerror(errorCode));
    LOG("mgmt", GB_LOG_ERROR, "glfs_opendir(%s): on volume %s failed[%s]",
        GB_METADIR, blk->volume, strerror(errorCode));
    errCode = errorCode;
    goto out;
  }

  while ((entry = glfs_readdir (tgmdfd))) {
    if (!strchr(entry->d_name, '.')) {
      if (blk->json_resp) {
        json_object_array_add(json_array,
                              GB_JSON_OBJ_TO_STR(entry->d_name));
      } else {
        if (GB_ASPRINTF(&filelist, "%s%s\n", (tmp==NULL?"":tmp),
                        entry->d_name)  == -1) {
          filelist = NULL;
          GB_FREE(tmp);
          errCode = ENOMEM;
          goto out;
        }
        GB_FREE(tmp);
        tmp = filelist;
      }
    }
  }

  errCode = 0;

  if (blk->json_resp) {
    json_object_object_add(json_obj, "blocks", json_array);
  }

 out:
  GB_METAUNLOCK(lkfd, blk->volume, errCode, errMsg);

 optfail:
  LOG("mgmt", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO),
      "info cli return %s, volume=%s",
      errCode ? "failure" : "success", blk->volume);

  if (tgmdfd && glfs_closedir (tgmdfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "glfs_closedir(%s): on volume %s failed[%s]",
        GB_METADIR, blk->volume, strerror(errno));
  }

  if (errCode < 0) {
    errCode = GB_DEFAULT_ERRCODE;
  }

  if (reply) {
    reply->exit = errCode;

    if (blk->json_resp) {
      if (errCode) {
        json_object_object_add(json_obj, "RESULT", GB_JSON_OBJ_TO_STR("FAIL"));
        json_object_object_add(json_obj, "errCode", json_object_new_int(errCode));
        json_object_object_add(json_obj, "errMsg",  GB_JSON_OBJ_TO_STR(errMsg));
      } else {
        json_object_object_add(json_obj, "RESULT", GB_JSON_OBJ_TO_STR("SUCCESS"));
      }
      GB_ASPRINTF(&reply->out, "%s\n",
                  json_object_to_json_string_ext(json_obj,
                                  mapJsonFlagToJsonCstring(blk->json_resp)));
      json_object_put(json_obj);
    } else {
      if (errCode) {
        if (errMsg) {
          GB_ASPRINTF (&reply->out, "%s\n", errMsg);
        } else {
          GB_ASPRINTF (&reply->out, "Not able to complete operation "
                       "successfully\n");
        }
      } else {
        reply->out = filelist? filelist:strdup("*Nil*\n");
      }
    }
  }
  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s",
      reply ? reply->out : "*Nil*");

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "glfs_close(%s): on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->volume, strerror(errno));
  }

  GB_FREE(errMsg);

  return reply;
}


bool_t
block_list_cli_1_svc(blockListCli *blk, blockResponse *reply,
                     struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(list_cli, blk, reply, rqstp, ret);
  return ret;
}
