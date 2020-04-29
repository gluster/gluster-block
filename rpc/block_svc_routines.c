/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "block_common.h"

# define   GB_SAVECFG_GBID_CHECK "grep -m 1 '\"config\":.*/block-store/%s\",' " GB_SAVECONFIG " > " DEVNULLPATH


pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


int
mapJsonFlagToJsonCstring(int jsonflag)
{
  switch (jsonflag) {
    case GB_JSON_PLAIN:
      return JSON_C_TO_STRING_PLAIN;

    case GB_JSON_DEFAULT:
    case GB_JSON_SPACED:
      return JSON_C_TO_STRING_SPACED;

    case GB_JSON_PRETTY:
      return JSON_C_TO_STRING_PRETTY;

    default:
      return JSON_C_TO_STRING_SPACED;
  }
}


void
blockFormatErrorResponse(operations op, int json_resp, int errCode,
                         char *errMsg, blockResponse *reply)
{
  json_object *json_obj = NULL;

  if (!reply) {
    return;
  }

  reply->exit = errCode;
  if (json_resp) {
    json_obj = json_object_new_object();
    json_object_object_add(json_obj, "RESULT", GB_JSON_OBJ_TO_STR("FAIL"));
    json_object_object_add(json_obj, "errCode", json_object_new_int(errCode));
    json_object_object_add(json_obj, "errMsg", GB_JSON_OBJ_TO_STR(errMsg));
    GB_ASPRINTF(&reply->out, "%s\n",
                json_object_to_json_string_ext(json_obj,
                mapJsonFlagToJsonCstring(json_resp)));
    json_object_put(json_obj);
  } else {
    if (op != INFO_SRV) {
      GB_ASPRINTF (&reply->out, "%s\nRESULT:FAIL\n", (errMsg?errMsg:"null"));
    } else {
      GB_ASPRINTF (&reply->out, "%s\n", (errMsg?errMsg:"null"));
    }
  }
}


void
blockStr2arrayAddToJsonObj(json_object *json_obj, char *string, char *label)
{
  char *tmp = NULL;
  json_object *json_array = NULL;

  if (!string)
    return;

  json_array = json_object_new_array();
  tmp = strtok (string, " ");
  while (tmp != NULL)
  {
    json_object_array_add(json_array, GB_JSON_OBJ_TO_STR(tmp));
    tmp = strtok (NULL, " ");
  }
  json_object_object_add(json_obj, label, json_array);
}


int
blockCheckBlockLoadedStatus(char *block_name, char *gbid, blockResponse *reply)
{

  int ret = -1;
  char *exec = NULL;
  int is_loaded = true;


  if (GB_ASPRINTF(&exec, GB_TGCLI_CHECK, block_name, gbid) == -1) {
    goto out;
  }

  ret = gbRunner(exec);
  if (ret == -1) {
    GB_ASPRINTF(&reply->out, "command exit abnormally for '%s'.", block_name);
    LOG("mgmt", GB_LOG_ERROR, "%s", reply->out);
    goto out;
  } else if (ret == 1) {
    is_loaded = false;
    GB_ASPRINTF(&reply->out, "Block '%s' may be not loaded.", block_name);
    LOG("mgmt", GB_LOG_ERROR, "%s", reply->out);
  }
  GB_FREE(exec);

  if (!ret) {
    if (GB_ASPRINTF(&exec, GB_TGCLI_ISCSI_CHECK, GB_TGCLI_IQN_PREFIX, gbid) == -1) {
      goto out;
    }

    ret = gbRunner(exec);
    if (ret == -1) {
      GB_ASPRINTF(&reply->out, "command exit abnormally for '%s'.", block_name);
      LOG("mgmt", GB_LOG_ERROR, "%s", reply->out);
      goto out;
    } else if (ret == 1) {
      is_loaded = false;
      GB_ASPRINTF(&reply->out, "Target for '%s' may be not loaded.", block_name);
      LOG("mgmt", GB_LOG_ERROR, "%s", reply->out);
    }
  }

  /* if block is loaded, skip the rest */
  if (!ret) {
    goto out;
  }
  GB_FREE(exec);

  if (GB_ASPRINTF(&exec, GB_SAVECFG_GBID_CHECK, gbid) == -1) {
    ret = -1;
    goto out;
  }

  ret = gbRunner(exec);
  if (ret == -1) {
    GB_FREE(reply->out);
    GB_ASPRINTF(&reply->out, "command exit abnormally for '%s'.", block_name);
    LOG("mgmt", GB_LOG_ERROR, "%s", reply->out);
    goto out;
  } else if (ret == 1 || ret == 2) {  /* search not found or savefile not exist */
    if (!is_loaded) {
      reply->exit = GB_BLOCK_NOT_FOUND;
      GB_FREE(reply->out);
      GB_ASPRINTF(&reply->out, "Block '%s' already deleted.", block_name);
      LOG("mgmt", GB_LOG_ERROR, "%s", reply->out);
      goto out;
    }
  } else {
    if (!is_loaded) {
      reply->exit = GB_BLOCK_NOT_LOADED;
      GB_FREE(reply->out);
      GB_ASPRINTF(&reply->out, "Block '%s' not loaded.", block_name);
      LOG("mgmt", GB_LOG_ERROR, "%s", reply->out);
      ret = -1;
      goto out;
    }
  }

 out:
  GB_FREE(exec);

  return ret;
}


char*
blockInfoGetCurrentSizeOfNode(char *block_name, MetaInfo *info, char *host)
{
  int i, j;
  char *tok;
  char *hr_size = NULL;
  size_t size = 0;
  bool noRsSuccess = true;

  if (!host)
    return NULL;

  for (i = 0; i < info->nhosts; i++) {
    if(strcmp(info->list[i]->addr, host)) {
      continue;
    }
    for (j = info->list[i]->nenties-1; j >= 0; j--) {
      if (!strstr(info->list[i]->st_journal[j], MetaStatusLookup[GB_RS_SUCCESS])) {
        continue;
      }
      noRsSuccess = false;
      tok = strstr(info->list[i]->st_journal[j], "-");
      if (!tok) {
        return NULL;
      }
      sscanf(tok+1, "%zu", &size);
      goto success;
    }
  }

  if (noRsSuccess) {
    size = info->initial_size;
    goto success;
  }

  return NULL;

 success:
  hr_size = glusterBlockFormatSize("mgmt", size);
  if (!hr_size) {
    LOG("mgmt", GB_LOG_WARNING,
        "failed to get previous size of portal %s for blockname=%s",
        info->list[i]->addr, block_name);
    return NULL;
  }
  return hr_size;
}


static struct addrinfo *
glusterBlockGetSockaddr(char *host)
{
  int ret;
  struct addrinfo hints, *res;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  ret = getaddrinfo(host, GB_TCP_PORT_STR, &hints, &res);
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "getaddrinfo(%s) failed (%s)",
        host, gai_strerror(ret));
    goto out;
  }

  return res;

 out:
  return NULL;
}


void
convertTypeCreate2ToCreate(blockCreate2 *blk_v2, blockCreate *blk_v1)
{

  if (!blk_v2) {
    return;
  }

  GB_STRCPYSTATIC(blk_v1->ipaddr, blk_v2->ipaddr);
  GB_STRCPYSTATIC(blk_v1->volume, blk_v2->volume);
  GB_STRCPYSTATIC(blk_v1->gbid, blk_v2->gbid);
  GB_STRCPYSTATIC(blk_v1->passwd, blk_v2->passwd);
  GB_STRCPYSTATIC(blk_v1->block_name, blk_v2->block_name);
  blk_v1->block_hosts = blk_v2->block_hosts;
  blk_v1->size = blk_v2->size;
  blk_v1->auth_mode = blk_v2->auth_mode;

  return;
}


int
glusterBlockCallRPC_1(char *host, void *cobj,
                      operations opt, bool *rpc_sent, char **out)
{
  CLIENT *clnt = NULL;
  int ret = -1;
  int sockfd = RPC_ANYSOCK;
  size_t i;
  blockResponse reply = {0,};
  struct addrinfo *res = NULL;
  gbCapResp *obj = NULL;
  blockCreate cblk_v1 = {{0},};
  blockCreate2 *cblk_v2 = NULL;


  *rpc_sent = FALSE;

  if (!(res = glusterBlockGetSockaddr(host))) {
    goto out;
  }

  clnt = clnttcp_create ((struct sockaddr_in *)res->ai_addr, GLUSTER_BLOCK, GLUSTER_BLOCK_VERS, &sockfd, 0, 0);
  if (!clnt) {
    LOG("mgmt", GB_LOG_ERROR, "%son inet host %s",
        clnt_spcreateerror("client create failed"), host);
    goto out;
  }

  switch(opt) {
  case CREATE_SRV:
    cblk_v2 = (blockCreate2 *)cobj;
    GB_STRCPYSTATIC(cblk_v2->ipaddr, host);

    if (GB_XDATA_IS_MAGIC(((struct gbXdata*)cblk_v2->xdata.xdata_val)->magic) ||
        cblk_v2->rb_size || cblk_v2->prio_path[0]) {
      *rpc_sent = TRUE;
      if (block_create_v2_1(cblk_v2, &reply, clnt) != RPC_SUCCESS) {
        LOG("mgmt", GB_LOG_ERROR, "%son host %s",
            clnt_sperror(clnt, "block remote create2 call failed"), host);
        goto out;
      }
    } else {
      convertTypeCreate2ToCreate(cblk_v2, &cblk_v1);
      *rpc_sent = TRUE;
      if (block_create_1(&cblk_v1, &reply, clnt) != RPC_SUCCESS) {
        LOG("mgmt", GB_LOG_ERROR, "%son host %s",
            clnt_sperror(clnt, "block remote create call failed"), host);
        goto out;
      }
    }
    break;
  case VERSION_SRV:
    *rpc_sent = TRUE;
    ret = block_version_1((void*)cobj, &reply, clnt);
    if (ret != RPC_SUCCESS) {
      LOG("mgmt", GB_LOG_WARNING, "%son host %s",
          clnt_sperror(clnt, "block remote version check call failed"), host);
      goto out;
    }
    break;
  case DELETE_SRV:
    *rpc_sent = TRUE;
    if (block_delete_1((blockDelete *)cobj, &reply, clnt) != RPC_SUCCESS) {
      LOG("mgmt", GB_LOG_ERROR, "%son host %s",
          clnt_sperror(clnt, "block remote delete call failed"), host);
      goto out;
    }
    break;
  case MODIFY_SRV:
    *rpc_sent = TRUE;
    if (block_modify_1((blockModify *)cobj, &reply, clnt) != RPC_SUCCESS) {
      LOG("mgmt", GB_LOG_ERROR, "%son host %s",
          clnt_sperror(clnt, "block remote modify call failed"), host);
      goto out;
    }
    break;
  case MODIFY_SIZE_SRV:
    *rpc_sent = TRUE;
    if (block_modify_size_1((blockModifySize *)cobj, &reply, clnt) != RPC_SUCCESS) {
      LOG("mgmt", GB_LOG_ERROR, "%son host %s",
          clnt_sperror(clnt, "block remote modify size call failed"), host);
      goto out;
    }
    break;
  case RELOAD_SRV:
    *rpc_sent = TRUE;
    if (block_reload_1((blockReload *)cobj, &reply, clnt) != RPC_SUCCESS) {
      LOG("mgmt", GB_LOG_ERROR, "%son host %s",
          clnt_sperror(clnt, "block remote reload call failed"), host);
      goto out;
    }
    break;
  case MODIFY_TPGC_SRV:
  case LIST_SRV:
  case INFO_SRV:
  case REPLACE_GET_PORTAL_TPG_SRV:
  case GENCONFIG_SRV:
      goto out;
  case REPLACE_SRV:
      *rpc_sent = TRUE;
      if (block_replace_1((blockReplace *)cobj, &reply, clnt) != RPC_SUCCESS) {
        LOG("mgmt", GB_LOG_ERROR, "%son host %s",
            clnt_sperror(clnt, "block remote replace call failed"), host);
        goto out;
      }
      break;
  }
  ret = -1;

  if (opt != VERSION_SRV) {
    if (GB_STRDUP(*out, reply.out) < 0) {
      goto out;
    }
  } else {
    if (GB_ALLOC(obj) < 0) {
      goto out;
    }
    obj->capMax = reply.xdata.xdata_len/sizeof(gbCapObj);
    gbCapObj *caps = (gbCapObj *)reply.xdata.xdata_val;
    if (GB_ALLOC_N(obj->response, obj->capMax) < 0) {
      GB_FREE(obj);
      goto out;
    }
    for (i = 0; i < obj->capMax; i++) {
      GB_STRCPYSTATIC(obj->response[i].cap, caps[i].cap);
      obj->response[i].status = caps[i].status;
    }
    *out = (char *) obj;
  }
  ret = reply.exit;

 out:
  if (clnt) {
    if (reply.out && !clnt_freeres(clnt, (xdrproc_t)xdr_blockResponse,
                                   (char *)&reply)) {
      LOG("mgmt", GB_LOG_ERROR, "%s",
          clnt_sperror(clnt, "clnt_freeres failed"));

    }
    clnt_destroy (clnt);
  }

  if (res) {
    freeaddrinfo(res);
  }

  return ret;
}


blockServerDefPtr
blockMetaInfoToServerParse(MetaInfo *info)
{
  blockServerDefPtr list;
  size_t i, j;


  if (!info) {
    return NULL;
  }

  if (GB_ALLOC(list) < 0) {
    goto out;
  }

  j = 0;
  for (i = 0; i < info->nhosts; i++) {
    if (!blockhostIsValid(info->list[i]->status)) {
      continue;
    }
    j++;
  }

  list->nhosts = j;

  if (GB_ALLOC_N(list->hosts, list->nhosts) < 0) {
    goto out;
  }

  j = 0;
  for (i = 0; i < info->nhosts; i++) {
    if (!blockhostIsValid(info->list[i]->status)) {
      continue;
    }
    if (GB_STRDUP(list->hosts[j++], info->list[i]->addr) < 0) {
      goto out;
    }
  }

  return list;

 out:
  blockServerDefFree(list);
  return NULL;
}


int
glusterBlockCollectAttemptSuccess(blockRemoteObj *args, MetaInfo *info,
                                  operations opt, size_t count,
                                  char **attempt, char **success)
{
  char *a_tmp = NULL;
  char *s_tmp = NULL;
  int i = 0;

  for (i = 0; i < count; i++) {
    /* GB_BLOCK_NOT_FOUND:
     *        Delete: Success
     *        Modify: Fail
     * GB_BLOCK_NOT_LOADED:
     *        Delete: Fail
     *        Modify: Fail
     */
    if (args[i].exit &&
        !(args[i].exit == GB_BLOCK_NOT_FOUND && opt == DELETE_SRV)) {
        if (GB_ASPRINTF(attempt, "%s %s",
              (a_tmp==NULL?"":a_tmp), args[i].addr) == -1) {
          goto fail;
        }
        GB_FREE(a_tmp);
        a_tmp = *attempt;
        LOG("mgmt", GB_LOG_ERROR, "%s: on volume %s on host %s",
            args[i].reply, args[i].volume, args[i].addr);
    } else {
      if (GB_ASPRINTF(success, "%s %s",
            (s_tmp==NULL?"":s_tmp), args[i].addr) == -1) {
        goto fail;
      }
      GB_FREE(s_tmp);
      s_tmp = *success;
    }
  }

  return 0;

 fail:
  GB_FREE(a_tmp);
  GB_FREE(s_tmp);
  *attempt = NULL;
  *success = NULL;
  return -1;
}


blockServerDefPtr
glusterBlockGetListFromInfo(MetaInfo *info)
{
  size_t i;
  blockServerDefPtr list = NULL;


  if (!info || GB_ALLOC(list) < 0) {
    return NULL;
  }

  if (GB_ALLOC_N(list->hosts, info->mpath) < 0) {
    goto out;
  }

  for (i = 0; i < info->nhosts; i++) {
    if (blockhostIsValid (info->list[i]->status)) {
      if (GB_STRDUP(list->hosts[list->nhosts++], info->list[i]->addr) < 0) {
        goto out;
      }
    }
  }

  return list;

 out:
  blockServerDefFree(list);
  return NULL;
}


int
blockGetHostStatus(MetaInfo *info, char *host)
{
  size_t i;


  if (!info || !host)
    return GB_METASTATUS_MAX;

  for (i = 0; i < info->nhosts; i++) {
    if (!strcmp(info->list[i]->addr, host)) {
      return blockMetaStatusEnumParse(info->list[i]->status);
    }
  }

  return GB_METASTATUS_MAX;
}


void
blockRemoteRespFree(blockRemoteResp *resp)
{

  if (!resp)
    return;

  GB_FREE(resp->attempt);
  GB_FREE(resp->success);
  GB_FREE(resp->skipped);
  GB_FREE(resp);
}


int
blockValidateCommandOutput(const char *out, int opt, void *data)
{
  blockCreate *cblk = data;
  blockDelete *dblk = data;
  blockModify *mblk = data;
  blockModifySize *msblk = data;
  blockReplace *rblk = data;
  blockReload *rlblk = data;
  int ret = -1;


  switch (opt) {
  case CREATE_SRV:
    /* backend create validation */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "backend creation failed for: %s", cblk,
                    cblk->volume,
                    "Created user-backed storage object %s size %zu.",
                    cblk->block_name, cblk->size);

    /* target iqn create validation */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "target iqn creation failed for: %s",
                    cblk, cblk->volume, "Created target %s%s.",
                    GB_TGCLI_IQN_PREFIX, cblk->gbid);

    /* LUN create validation */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "LUN creation failed for: %s",cblk,
                    cblk->volume,
                    "Created LUN 0.");

    /* Portal create validation */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "portal creation failed for %s", cblk,
                    cblk->volume,
                    "Created network portal %s:3260.", cblk->ipaddr);

    /* TPG enable validation */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "TPGT enablement failed for: %s", cblk,
                    cblk->volume,
                    "The TPGT has been enabled.");

    GB_OUT_VALIDATE_OR_GOTO(out, out, "generate_node_acls set failed for: %s",
                    cblk, cblk->volume,
                    "Parameter generate_node_acls is now '1'");

    GB_OUT_VALIDATE_OR_GOTO(out, out, "demo_mode_write_protect set failed "
                    "for: %s", cblk, cblk->volume,
                    "Parameter demo_mode_write_protect is now '0'.");

    if (cblk->auth_mode) {
      /* authentication validation */
      GB_OUT_VALIDATE_OR_GOTO(out, out, "attribute authentication set failed "
                      "for: %s", cblk, cblk->volume,
                      "Parameter authentication is now '1'.");

      /* userid set validation */
      GB_OUT_VALIDATE_OR_GOTO(out, out, "userid set failed for: %s", cblk,
                      cblk->volume,
                      "Parameter userid is now '%s'.", cblk->gbid);

      /* password set validation */
      GB_OUT_VALIDATE_OR_GOTO(out, out, "password set failed for: %s", cblk,
                      cblk->volume,
                      "Parameter password is now '%s'.", cblk->passwd);
    }

    ret = 0;
    break;

  case DELETE_SRV:
    /* backend delete validation */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "backend deletion failed for block: %s",
                    dblk, NULL, "Deleted storage object %s.", dblk->block_name);
    /* target iqn delete validation */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "target iqn deletion failed for block: "
                    "%s", dblk, NULL,
                    "Deleted Target %s%s.",
                    GB_TGCLI_IQN_PREFIX,
                    dblk->gbid);
    ret = 0;
    break;

  case MODIFY_SRV:
    if (mblk->auth_mode) {
      /* authentication validation */
      GB_OUT_VALIDATE_OR_GOTO(out, out, "attribute authentication set failed "
                      "for: %s", mblk,
                      mblk->volume, "Parameter authentication is now '1'.");

      /* userid set validation */
      GB_OUT_VALIDATE_OR_GOTO(out, out, "userid set failed for: %s", mblk,
                      mblk->volume,
                      "Parameter userid is now '%s'.", mblk->gbid);

      /* password set validation */
      GB_OUT_VALIDATE_OR_GOTO(out, out, "password set failed for: %s", mblk,
                      mblk->volume,
                      "Parameter password is now '%s'.", mblk->passwd);
    } else {
      GB_OUT_VALIDATE_OR_GOTO(out, out, "attribute authentication unset "
                      "failed for: %s", mblk,
                      mblk->volume, "Parameter authentication is now '0'.");
    }

    ret = 0;
    break;

  case MODIFY_SIZE_SRV:
    /* dev_size set validation */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "dev_size set failed for block: %s", msblk,
                            msblk->volume,
                            "Parameter dev_size is now '%zu'.", msblk->size);

    ret = 0;
    break;
  case MODIFY_TPGC_SRV:
    /* iscsi iqn status validation */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "iscsi status check failed for: %s",
                    mblk, mblk->volume,
                    "Status for /iscsi/%s%s: TPGs:", GB_TGCLI_IQN_PREFIX,
                    mblk->gbid);
    ret = 0;
    break;

  case REPLACE_SRV:
    /* old portal deletion validation */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "portal delete failed: %s:3260",
                            rblk, rblk->ripaddr, "Deleted network portal %s:3260",
                            rblk->ripaddr);

    /* re-create portal validation */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "portal (re)create failed: %s:3260",
                            rblk, rblk->ipaddr, "Created network portal %s:3260.",
                            rblk->ipaddr);
    ret = 0;
    break;

  case REPLACE_GET_PORTAL_TPG_SRV:
    /* get tpg of the portal */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "failed to get tpg number for portal : %s",
                            rblk, rblk->ripaddr, "tpg");
    ret = 0;
    break;

  case RELOAD_SRV:
    /* get tpg of the portal */
    GB_OUT_VALIDATE_OR_GOTO(out, out, "failed to reload block volume: %s",
                            rlblk, rlblk->block_name, "Configuration restored");
    ret = 0;
    break;
  }

out:
  return ret;
}


int
gluster_block_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
  xdr_free (xdr_result, result);

  return 1;
}


int
gluster_block_cli_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
  xdr_free (xdr_result, result);

  return 1;
}
