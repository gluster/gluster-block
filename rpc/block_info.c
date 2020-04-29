/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "block_common.h"


static void
blockInfoCliFormatResponse(blockInfoCli *blk, int errCode,
                           char *errMsg, MetaInfo *info,
                           blockResponse *reply)
{
  json_object  *json_obj    = NULL;
  json_object  *json_obj2   = NULL;
  json_object  *json_array1 = NULL;
  json_object  *json_array2 = NULL;
  char         *tmp         = NULL;
  char         *tmp2        = NULL;
  char         *tmp3        = NULL;
  char         *tmp4        = NULL;
  char         *tmp5        = NULL;
  char         *out         = NULL;
  int          i            = 0;
  char         *hr_size     = NULL;           /* Human Readable size */
  char         *timeout     = NULL;
  char         *rsf_nodes   = NULL;

  if (!reply) {
    return;
  }

  if (errCode < 0) {
    errCode = GB_DEFAULT_ERRCODE;
  }
  reply->exit = errCode;

  if (errMsg) {
    blockFormatErrorResponse(INFO_SRV, blk->json_resp, errCode,
                             errMsg, reply);
    return;
  }

  if (!info)
    goto out;

  hr_size = glusterBlockFormatSize("mgmt", info->size);
  if (!hr_size) {
    GB_ASPRINTF (&errMsg, "failed in glusterBlockFormatSize");
    blockFormatErrorResponse(INFO_SRV, blk->json_resp, ENOMEM,
                             errMsg, reply);
    GB_FREE(errMsg);
    goto out;
  }

  if (GB_ASPRINTF(&timeout, "%lu Seconds", info->io_timeout) < 0) {
    GB_ASPRINTF (&errMsg, "failed in blockInfoCliFormatResponse");
    blockFormatErrorResponse(INFO_SRV, blk->json_resp, ENOMEM,
                             errMsg, reply);
    goto out;
  }

  if (blk->json_resp) {
    json_obj = json_object_new_object();
    json_object_object_add(json_obj, "NAME", GB_JSON_OBJ_TO_STR(blk->block_name));
    json_object_object_add(json_obj, "VOLUME", GB_JSON_OBJ_TO_STR(info->volume));
    json_object_object_add(json_obj, "GBID", GB_JSON_OBJ_TO_STR(info->gbid));
    json_object_object_add(json_obj, "SIZE", GB_JSON_OBJ_TO_STR(hr_size));
    json_object_object_add(json_obj, "HA", json_object_new_int(info->mpath));
    json_object_object_add(json_obj, "IOTIMEOUT", GB_JSON_OBJ_TO_STR(timeout));
    json_object_object_add(json_obj, "PASSWORD", GB_JSON_OBJ_TO_STR(info->passwd));

    json_array1 = json_object_new_array();

    for (i = 0; i < info->nhosts; i++) {
      if (blockhostIsValid (info->list[i]->status)) {
        json_object_array_add(json_array1, GB_JSON_OBJ_TO_STR(info->list[i]->addr));
      } else {
        switch (blockMetaStatusEnumParse(info->list[i]->status)) {
        case GB_CONFIG_FAIL:
        case GB_CLEANUP_FAIL:
          if (!json_array2) {
            json_array2 = json_object_new_array();
          }
          json_object_array_add(json_array2, GB_JSON_OBJ_TO_STR(info->list[i]->addr));
          break;
        }
      }
      switch (blockMetaStatusEnumParse(info->list[i]->status)) {
      case GB_RS_FAIL:
      case GB_RS_INPROGRESS:
        GB_FREE(hr_size);
        hr_size = blockInfoGetCurrentSizeOfNode(blk->block_name, info, info->list[i]->addr);
        if (!hr_size) {
          LOG("mgmt", GB_LOG_WARNING,
              "blockInfoGetCurrentSizeOfNode retuns null, block: %s/%s node: %s",
              blk->volume, blk->block_name, info->list[i]->addr);
        }
        if (!json_obj2) {
          json_obj2 = json_object_new_object();
        }
        json_object_object_add(json_obj2, info->list[i]->addr, GB_JSON_OBJ_TO_STR(hr_size));
      }
    }

    json_object_object_add(json_obj, "EXPORTED ON", json_array1);
    if (json_obj2) {
      json_object_object_add(json_obj, "RESIZE FAILED ON", json_obj2);
    }
    if (json_array2) {
      json_object_object_add(json_obj, "ENCOUNTERED FAILURES ON", json_array2);
    }

    GB_ASPRINTF(&reply->out, "%s\n",
                json_object_to_json_string_ext(json_obj,
                                mapJsonFlagToJsonCstring(blk->json_resp)));
    json_object_put(json_obj);
  } else {
    if (GB_ASPRINTF(&tmp, "NAME: %s\nVOLUME: %s\nGBID: %s\nSIZE: %s\n"
                    "HA: %zu\nIOTIMEOUT: %s\nPASSWORD: %s\nEXPORTED ON:",
                    blk->block_name, info->volume, info->gbid, hr_size,
                    info->mpath, timeout, info->passwd) == -1) {
      goto out;
    }
    for (i = 0; i < info->nhosts; i++) {
      if (blockhostIsValid (info->list[i]->status)) {
        if (GB_ASPRINTF(&out, "%s %s", tmp, info->list[i]->addr) == -1) {
          GB_FREE (tmp);
          goto out;
        }
        GB_FREE (tmp);
        tmp = out;
      } else {
        switch (blockMetaStatusEnumParse(info->list[i]->status)) {
        case GB_CONFIG_FAIL:
        case GB_CLEANUP_FAIL:
          if (GB_ASPRINTF(&tmp2, "%s %s", tmp3?tmp3:"", info->list[i]->addr) == -1) {
            GB_FREE (tmp3);
            goto out;
          }
          GB_FREE (tmp3);
          tmp3 = tmp2;
          break;
        }
      }
      tmp4 = rsf_nodes;
      switch (blockMetaStatusEnumParse(info->list[i]->status)) {
      case GB_RS_FAIL:
      case GB_RS_INPROGRESS:
        GB_FREE(hr_size);
        hr_size = blockInfoGetCurrentSizeOfNode(blk->block_name, info, info->list[i]->addr);
        if (!hr_size) {
          LOG("mgmt", GB_LOG_WARNING,
              "blockInfoGetCurrentSizeOfNode retuns null, block: %s/%s node: %s",
              blk->volume, blk->block_name, info->list[i]->addr);
        }
        if (GB_ASPRINTF (&tmp5, "[%s]", hr_size?hr_size:"null") < 0 ) {
          goto out;
        }
        if (GB_ASPRINTF(&rsf_nodes, "%s %s:%s", tmp4?tmp4:"", info->list[i]->addr, tmp5?tmp5:"") == -1) {
          GB_FREE (tmp5);
          goto out;
        }
        GB_FREE (tmp4);
        GB_FREE (tmp5);
      }
    }

    if (rsf_nodes) {
      tmp4 = tmp;
      if (GB_ASPRINTF(&tmp, "%s\nRESIZE FAILED ON:%s", tmp4, rsf_nodes) == -1) {
        goto out;
      }
    }

    if (tmp2) {
      tmp3 = tmp;
      if (GB_ASPRINTF(&tmp, "%s\nENCOUNTERED FAILURES ON:%s", tmp3, tmp2) == -1) {
        goto out;
      }
    }

    if (GB_ASPRINTF(&reply->out, "%s\n", tmp) == -1) {
      goto out;
    }
  }
 out:
  /*catch all*/
  if (!reply->out) {
    blockFormatErrorResponse(INFO_SRV, blk->json_resp, errCode,
                             GB_DEFAULT_ERRMSG, reply);
  }
  GB_FREE (hr_size);
  GB_FREE (timeout);
  GB_FREE (rsf_nodes);
  GB_FREE (tmp);
  GB_FREE (tmp2);
  GB_FREE (tmp3);
  GB_FREE (tmp4);
  return;
}

static blockResponse *
block_info_cli_1_svc_st(blockInfoCli *blk, struct svc_req *rqstp)
{
  blockResponse *reply = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  MetaInfo *info = NULL;
  int errCode = -1;
  char *errMsg = NULL;


  LOG("mgmt", GB_LOG_INFO,
      "info cli request, volume=%s blockname=%s", blk->volume, blk->block_name);

  if ((GB_ALLOC(reply) < 0) || (GB_ALLOC(info) < 0)) {
    GB_FREE (reply);
    GB_FREE (info);
    goto optfail;
  }

  errCode = 0;
  glfs = glusterBlockVolumeInit(blk->volume, &errCode, &errMsg);
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockVolumeInit(%s) for block %s failed",
        blk->volume, blk->block_name);
    goto optfail;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume, &errCode, &errMsg);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s %s for block %s",
        FAILED_CREATING_META, blk->volume, blk->block_name);
    goto optfail;
  }

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, errCode, errMsg, optfail);

  if (blockGetMetaInfo(glfs, blk->block_name, info, &errCode)) {
    if (errCode == ENOENT) {
      GB_ASPRINTF (&errMsg, "block %s/%s doesn't exist", blk->volume,
                   blk->block_name);
    } else {
      GB_ASPRINTF (&errMsg, "Not able to get metadata information for %s/%s[%s]",
                   blk->volume, blk->block_name, strerror(errCode));
    }
    goto out;
  }

 out:
  GB_METAUNLOCK(lkfd, blk->volume, errCode, errMsg);

 optfail:
  LOG("mgmt", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO),
      "info cli return %s, volume=%s blockname=%s",
      errCode ? "failure" : "success", blk->volume, blk->block_name);

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): on volume %s for block %s failed[%s]",
        GB_TXLOCKFILE, blk->volume, blk->block_name, strerror(errno));
  }


  blockInfoCliFormatResponse(blk, errCode, errMsg, info, reply);
  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s",
      reply ? reply->out : "*Nil*");
  GB_FREE(errMsg);
  blockFreeMetaInfo(info);

  return reply;
}


bool_t
block_info_cli_1_svc(blockInfoCli *blk, blockResponse *reply,
                     struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(info_cli, blk, reply, rqstp, ret);
  return ret;
}
