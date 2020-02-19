/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "block_common.h"

# define   GB_GET_PORTAL_TPG    "targetcli /iscsi/" GB_TGCLI_IQN_PREFIX \
                                "'%s' ls | grep -e tpg -e '%s' | grep -B1 '%s' | grep -o 'tpg\\w'"
# define   GB_CHECK_PORTAL      "targetcli /iscsi/" GB_TGCLI_IQN_PREFIX \
                                "'%s' ls | grep '%s' > " DEVNULLPATH


static void
blockRemoteReplaceRespFree(blockRemoteReplaceResp *resp)
{

  if (!resp)
    return;

  blockRemoteRespFree(resp->cop);
  blockRemoteRespFree(resp->dop);
  blockRemoteRespFree(resp->rop);
  GB_FREE(resp);
}


static void *
glusterBlockReplacePortalRemote(void *data)
{
  int ret;
  int saveret;
  blockRemoteObj *args = (blockRemoteObj *)data;
  blockReplace robj = *(blockReplace *)args->obj;
  char *errMsg = NULL;
  bool rpc_sent = FALSE;


  GB_METAUPDATE_OR_GOTO(lock, args->glfs, robj.block_name, robj.volume,
                        ret, errMsg, out, "%s: RPINPROGRESS\n", args->addr);

  ret = glusterBlockCallRPC_1(args->addr, &robj, REPLACE_SRV, &rpc_sent,
                              &args->reply);
  if (ret && ret != GB_OP_SKIPPED) {
    saveret = ret;
    if (!rpc_sent) {
      GB_ASPRINTF(&errMsg, ": %s", strerror(errno));
      LOG("mgmt", GB_LOG_ERROR, "%s hence %s for block %s on "
          "host %s volume %s", strerror(errno), FAILED_REMOTE_REPLACE,
          robj.block_name, args->addr, args->volume);
      goto out;
    } else if (args->reply) {
      errMsg = args->reply;
      args->reply = NULL;
    }

    GB_METAUPDATE_OR_GOTO(lock, args->glfs, robj.block_name, robj.volume,
                          ret, errMsg, out, "%s: RPFAIL\n", args->addr);
    LOG("mgmt", GB_LOG_ERROR, "%s for block %s on host %s volume %s",
        FAILED_REMOTE_CREATE, robj.block_name, args->addr, args->volume);

    ret = saveret;
    goto out;
  }

  GB_METAUPDATE_OR_GOTO(lock, args->glfs, robj.block_name, robj.volume,
                        ret, errMsg, out, "%s: RPSUCCESS\n", args->addr);

out:
  if (!args->reply) {
    if (GB_ASPRINTF(&args->reply, "failed to replace remote portal on %s %s\n", args->addr,
          errMsg?errMsg:"") == -1) {
      ret = ret?ret:-1;
    }
  }
  args->exit = ret;

  GB_FREE (errMsg);
  return NULL;
}


static int
glusterBlockReplaceNodeRemoteAsync(struct glfs *glfs, blockReplaceCli *blk,
                                    MetaInfo *info, char *block,
                                    blockRemoteReplaceResp **savereply)
{
  blockRemoteReplaceResp *reply = NULL;
  pthread_t  *tid = NULL;
  blockRemoteObj *args = NULL;
  blockCreate2 *cobj = NULL;
  blockDelete *dobj = NULL;
  blockReplace *robj = NULL;
  struct gbXdata *xdata = NULL;
  bool Flag = false;
  bool cCheck = false;
  bool dCheck = false;
  bool rCheck = false;
  bool newNodeInUse = false;
  char *tmp = NULL;
  size_t i = 0, j = 1;
  int ret = -1;
  int status = 0;


  if ((GB_ALLOC(reply) < 0) || (GB_ALLOC(reply->cop) < 0) ||
      (GB_ALLOC(reply->dop) < 0) || (GB_ALLOC(reply->rop) < 0)) {
    goto out;
  }
  reply->cop->status = -1;
  reply->dop->status = -1;
  reply->rop->status = -1;
  reply->force = blk->force;

  if ((GB_ALLOC(cobj) < 0) || (GB_ALLOC(robj) < 0)  || (GB_ALLOC(dobj) < 0)){
    goto out;
  }

  if (info->io_timeout) { // Create V4
    unsigned int len;
    struct gbCreate *gbCreate;

    len = sizeof(struct gbXdata) + sizeof(struct gbCreate);
    if (GB_ALLOC_N(xdata, len) < 0) {
	goto out;
    }

    xdata->magic = GB_XDATA_GEN_MAGIC(4);
    gbCreate = (struct gbCreate *)(&xdata->data);
    GB_STRCPY(gbCreate->volServer, (char *)gbConf->volServer, sizeof(gbConf->volServer));
    gbCreate->blk_size = info->blk_size;
    gbCreate->io_timeout = info->io_timeout;

    cobj->xdata.xdata_len = len;
    cobj->xdata.xdata_val = (char *)xdata;
  } else if (info->blk_size) { // Create V3
    unsigned int len;
    struct gbCreate *gbCreate;

    len = sizeof(struct gbXdata) + sizeof(struct gbCreate);
    if (GB_ALLOC_N(xdata, len) < 0) {
	goto out;
    }

    xdata->magic = GB_XDATA_GEN_MAGIC(3);
    gbCreate = (struct gbCreate *)(&xdata->data);
    GB_STRCPY(gbCreate->volServer, (char *)gbConf->volServer, sizeof(gbConf->volServer));
    gbCreate->blk_size = info->blk_size;

    cobj->xdata.xdata_len = len;
    cobj->xdata.xdata_val = (char *)xdata;
  } else { // Create V2
    cobj->xdata.xdata_len = strlen(gbConf->volServer);
    cobj->xdata.xdata_val = (char *) gbConf->volServer;
  }

  GB_STRCPYSTATIC(cobj->ipaddr, blk->new_node);
  GB_STRCPYSTATIC(cobj->volume, info->volume);
  GB_STRCPYSTATIC(cobj->gbid, info->gbid);
  cobj->size = info->size;
  cobj->rb_size = info->rb_size;
  cobj->auth_mode = !!info->passwd[0];
  GB_STRCPYSTATIC(cobj->passwd, info->passwd);
  GB_STRCPYSTATIC(cobj->block_name, block);
  if (info->prio_path[0]) {
    if (!strcmp(info->prio_path, blk->old_node)) {
      GB_STRCPYSTATIC(cobj->prio_path, blk->new_node);
    } else {
      GB_STRCPYSTATIC(cobj->prio_path, info->prio_path);
    }
  }

  GB_STRCPYSTATIC(robj->volume, info->volume);
  GB_STRCPYSTATIC(robj->gbid, info->gbid);
  GB_STRCPYSTATIC(robj->block_name, block);
  GB_STRCPYSTATIC(robj->ipaddr, blk->new_node);
  GB_STRCPYSTATIC(robj->ripaddr, blk->old_node);

  GB_STRCPYSTATIC(dobj->block_name, block);
  GB_STRCPYSTATIC(dobj->gbid, info->gbid);

  /* Fill args[] */
  if (GB_ALLOC_N(args, info->mpath + 1) < 0) {
    goto out;
  }
  args[0].glfs = glfs;
  args[0].obj = (void *)cobj;
  args[0].addr = blk->new_node;
  args[0].volume = blk->volume;
  if (GB_STRDUP(cobj->block_hosts, blk->new_node) < 0) {
    goto out;
  }
  for (i = 0; i < info->nhosts; i++) {
    if (strcmp(info->list[i]->addr, blk->old_node)) {
      if (blockhostIsValid(info->list[i]->status)) {
        /* Construct block_hosts */
        tmp = cobj->block_hosts;
        if (GB_ASPRINTF(&cobj->block_hosts, "%s,%s", tmp, info->list[i]->addr) == -1) {
          GB_FREE (tmp);
          goto out;
        }
        GB_FREE(tmp);

        /* Fill args */
        args[j].glfs = glfs;
        args[j].obj = (void *)robj;
        args[j].addr = info->list[i]->addr;
        args[j].volume = blk->volume;
        j++;
      }
    }
  }
  args[info->mpath].glfs = glfs;
  args[info->mpath].obj = (void *)dobj;
  args[info->mpath].addr = blk->old_node;
  args[info->mpath].volume = blk->volume;

  /* -> Make Sure Old Node is currenly in use */
  if (blockGetHostStatus(info, blk->old_node) == GB_METASTATUS_MAX) {
    ret = GB_NODE_NOT_EXIST;
    LOG("mgmt", GB_LOG_WARNING, "block %s is not configured on node %s for volume %s",
        blk->block_name,  blk->old_node, blk->volume);
    goto out;
  } else if (blockGetHostStatus(info, blk->old_node) == GB_CLEANUP_SUCCESS) {
    dCheck = true; /* Old node deleted, but we are not sure when though */
  }

  /* -> Make Sure New Node is not already consumed by this block */
  for (i = 0; i < info->nhosts; i++) {
    if (!strcmp(info->list[i]->addr, blk->new_node) && blockhostIsValid(info->list[i]->status)) {
      newNodeInUse = true;  /* New node in use */
      status = blockMetaStatusEnumParse(info->list[i]->status);
      if (status == GB_AUTH_ENFORCED || status == GB_CONFIG_SUCCESS) {
        cCheck = true;  /* New node is freshly configured */
      }
      break;
    }
  }

  /* -> New node used && not triggered by previouly issued replace node */
  if (newNodeInUse && !cCheck) {
    ret = GB_NODE_IN_USE;
    LOG("mgmt", GB_LOG_ERROR, "block %s was already configured on node %s for volume %s",
        blk->block_name,  blk->old_node, blk->volume);
    goto out;
  }

  /* -> Was Replace Node already run on this block ? (might not be for same portals) */
  for (i = 1; i < info->mpath; i++) {
    switch(blockGetHostStatus(info, args[i].addr)) {
    case GB_RP_SUCCESS:
    case GB_RP_INPROGRESS:
    case GB_RP_FAIL:
      break;
    default:
      Flag = true;  /* doesn't look like RP run on same portals as now */
      break;
    }
    if (Flag)
      break;
  }

  /* -> If new node got already configured but not as part of previous RP req */
  if (cCheck && Flag) {
    ret = GB_NODE_IN_USE;
    LOG("mgmt", GB_LOG_ERROR, "block %s was already configured on node %s for volume %s",
        blk->block_name,  blk->old_node, blk->volume);
    goto out;
  }

  for (i = 1; i < info->mpath; i++) {
    if (blockGetHostStatus(info, args[i].addr) != GB_RP_SUCCESS) {
      rCheck = true;   /* not all RP req are success */
      break;
    }
  }

  /* All nodes are already replaced ? */
  if (!rCheck) {
    /* Then check for delete of old node and create on new node, to confirm last op was same as this */
    if (dCheck && cCheck) {
      reply->status = GB_OP_SKIPPED; /* replace was already successful in previous RP req */
      ret = 0;
      goto out;  /* We can skip this op  */
    }
  }

  if (GB_ALLOC_N(tid, info->mpath + 1) < 0) {
    goto out;
  }

  /* Create */
  if (!cCheck) {
    pthread_create(&tid[0], NULL, glusterBlockCreateRemote, &args[0]);
  } else {
    reply->cop->status = GB_OP_SKIPPED; /* skip */
    if (GB_STRDUP(reply->cop->skipped, args[0].addr) < 0) {
      goto out;
    }
  }

  /* Replace Portal */
  if (rCheck) {
    for (i = 1; i < info->mpath; i++) {
      pthread_create(&tid[i], NULL, glusterBlockReplacePortalRemote, &args[i]);
    }
  } else {
    reply->rop->status = GB_OP_SKIPPED; /* skip */
    for (i = 1; i < info->mpath; i++) {
      tmp = reply->rop->skipped;
      if (GB_ASPRINTF(&reply->rop->skipped, "%s %s",
                      (tmp==NULL?"":tmp), args[i].addr) == -1) {
        GB_FREE (tmp);
        goto out;
      }
      GB_FREE (tmp);
    }
  }

  /* Delete */
  if (!dCheck) {
    pthread_create(&tid[info->mpath], NULL, glusterBlockDeleteRemote, &args[info->mpath]);
  } else {
    reply->dop->status = GB_OP_SKIPPED; /* skip */
    if (GB_STRDUP(reply->dop->skipped, args[info->mpath].addr) < 0) {
      goto out;
    }
  }

  if (!cCheck) {
    pthread_join(tid[0], NULL);
  }
  if (rCheck) {
    for (i = 1; i < info->mpath; i++) {
      pthread_join(tid[i], NULL);
    }
  }
  if (!dCheck) {
    pthread_join(tid[info->mpath], NULL);
  }

  /* Collect results */
  if (!cCheck) {
    reply->cop->status = args[0].exit;
    if (args[0].exit) {
      if (GB_STRDUP(reply->cop->attempt, args[0].addr) < 0) {
        goto out;
      }
    } else {
      if (GB_STRDUP(reply->cop->success, args[0].addr) < 0) {
        goto out;
      }
    }
  }

  if (!dCheck) {
    reply->dop->status =  args[info->mpath].exit;
    if (args[info->mpath].exit) {
      if (GB_STRDUP(reply->dop->attempt, args[info->mpath].addr) < 0) {
        goto out;
      }
    } else {
      if (GB_STRDUP(reply->dop->success, args[info->mpath].addr) < 0) {
        goto out;
      }
    }
  }

  if (rCheck) {
    for (i = 1; i < info->mpath; i++) {
      if (args[i].exit) {
        tmp = reply->rop->attempt;
        if (GB_ASPRINTF(&reply->rop->attempt, "%s %s", tmp?tmp:"", args[i].addr) == -1) {
          goto out;
        }
        GB_FREE(tmp);
      } else {
        tmp = reply->rop->success;
        if (GB_ASPRINTF(&reply->rop->success, "%s %s", tmp?tmp:"", args[i].addr) == -1) {
          goto out;
        }
        GB_FREE(tmp);
      }
      reply->rop->status = args[i].exit;
    }
  } else {
    if (info->mpath == 1) {
      if (GB_ASPRINTF(&reply->rop->success, "N/A") == -1) {
        goto out;
      }
      reply->rop->status = 0; /* mimic success */
    }
  }

  ret = 0;

 out:
  if (!ret) {
    *savereply = reply;
    reply = NULL;
  }
  GB_FREE(tid);
  GB_FREE(xdata);
  GB_FREE(cobj);
  GB_FREE(dobj);
  GB_FREE(robj);
  GB_FREE(args);
  blockRemoteReplaceRespFree(reply);

  return ret;
}


static void
blockReplaceNodeCliFormatResponse(blockReplaceCli *blk, int errCode, char *errMsg,
                                  blockRemoteReplaceResp *savereply,
                                  blockResponse *reply)
{
  json_object *json_obj = NULL;
  char *tmp = NULL;
  char *entry = NULL;

  if (!reply) {
    return;
  }

  if (errCode < 0) {
    errCode = GB_DEFAULT_ERRCODE;
  }
  reply->exit = errCode;

  if (errMsg) {
    blockFormatErrorResponse(REPLACE_SRV, blk->json_resp, errCode,
                             errMsg, reply);
    return;
  }

  if (!savereply || !savereply->cop || !savereply->dop || !savereply->rop) {
    goto out;
  }

  if (blk->json_resp) {
    json_obj = json_object_new_object();
    json_object_object_add(json_obj, "NAME", GB_JSON_OBJ_TO_STR(blk->block_name));

    if (savereply->status == GB_OP_SKIPPED || savereply->status == -1) {
      if (savereply->status == GB_OP_SKIPPED) {
        json_object_object_add(json_obj, "RESULT", GB_JSON_OBJ_TO_STR("SKIPPED"));
      } else {
        /* see if node in use by this block */
        json_object_object_add(json_obj, "RESULT", GB_JSON_OBJ_TO_STR("FAIL"));
        json_object_object_add(json_obj, "errCode", json_object_new_int(-1));
        json_object_object_add(json_obj, "errMsg",
            GB_JSON_OBJ_TO_STR("Given new-node is already in-use with this block, use other node"));
      }
    } else {
      if (savereply->cop->status == GB_OP_SKIPPED) {
        json_object_object_add(json_obj, "CREATE SKIPPED",
                               GB_JSON_OBJ_TO_STR(savereply->cop->skipped));
      } else if (savereply->cop->status) {
        json_object_object_add(json_obj, "CREATE FAILED",
                               GB_JSON_OBJ_TO_STR(savereply->cop->attempt));
      } else {
        json_object_object_add(json_obj, "CREATE SUCCESS",
                               GB_JSON_OBJ_TO_STR(savereply->cop->success));
      }

      if (savereply->dop->status == GB_OP_SKIPPED) {
        json_object_object_add(json_obj, "DELETE SKIPPED",
                               GB_JSON_OBJ_TO_STR(savereply->dop->skipped));
      } else if (savereply->dop->status) {
        if (savereply->force) {
          json_object_object_add(json_obj, "DELETE FAILED (ignored)",
                                 GB_JSON_OBJ_TO_STR(savereply->dop->attempt));
        } else {
          json_object_object_add(json_obj, "DELETE FAILED",
                                 GB_JSON_OBJ_TO_STR(savereply->dop->attempt));
        }
        /* mimic success */
        savereply->dop->status = 0;
      } else {
        json_object_object_add(json_obj, "DELETE SUCCESS",
                               GB_JSON_OBJ_TO_STR(savereply->dop->success));
      }

      if (savereply->rop->status == GB_OP_SKIPPED) {
        blockStr2arrayAddToJsonObj(json_obj, savereply->rop->skipped,
                                   "REPLACE PORTAL SKIPPED ON");
      } else {
        if (savereply->rop->attempt) {
          blockStr2arrayAddToJsonObj(json_obj, savereply->rop->attempt,
                                     "REPLACE PORTAL FAILED ON");
        }
        if (savereply->rop->success) {
          blockStr2arrayAddToJsonObj(json_obj,
                                     savereply->rop->success?savereply->rop->success:"N/A",
                                     "REPLACE PORTAL SUCCESS ON");
        }
      }

      if ((savereply->cop->status == GB_OP_SKIPPED || !savereply->cop->status) &&
          (savereply->dop->status == GB_OP_SKIPPED || !savereply->dop->status) &&
          (savereply->rop->status == GB_OP_SKIPPED || !savereply->rop->status)) {
        json_object_object_add(json_obj, "RESULT", GB_JSON_OBJ_TO_STR("SUCCESS"));
      } else {
        json_object_object_add(json_obj, "RESULT", GB_JSON_OBJ_TO_STR("FAIL"));
      }
    }
    GB_ASPRINTF(&reply->out, "%s\n", json_object_to_json_string_ext(json_obj,
                                       mapJsonFlagToJsonCstring(blk->json_resp)));
    json_object_put(json_obj);
  } else {
    if (GB_ASPRINTF(&entry, "NAME: %s\n", blk->block_name) == -1) {
      goto out;
    }

    if (savereply->status == GB_OP_SKIPPED || savereply->status == -1) {
      tmp = entry;
      if (savereply->status == GB_OP_SKIPPED) {
        if (GB_ASPRINTF(&entry, "%sRESULT: SKIPPED\n", tmp?tmp:"") == -1) {
          goto out;
        }
      } else {
        /* see if node in use by this block */
        if (GB_ASPRINTF(&entry, "%serrMsg:%s\nRESULT: FAIL\n", tmp?tmp:"",
              "Given new-node is already in-use with this block, use other node") == -1) {
          goto out;
        }
      }
      GB_FREE(tmp);
    } else {
      tmp = entry;
      if (savereply->cop->status == GB_OP_SKIPPED) {
        if (GB_ASPRINTF(&entry, "%sCREATE SKIPPED: %s\n", tmp?tmp:"",
                        savereply->cop->skipped) == -1) {
          goto out;
        }
      } else if (savereply->cop->status) {
        if (GB_ASPRINTF(&entry, "%sCREATE FAILED: %s\n", tmp?tmp:"",
                        savereply->cop->attempt) == -1) {
          goto out;
        }
      } else {
        if (GB_ASPRINTF(&entry, "%sCREATE SUCCESS: %s\n", tmp?tmp:"",
                        savereply->cop->success) == -1) {
          goto out;
        }
      }
      GB_FREE(tmp);

      tmp = entry;
      if (savereply->dop->status == GB_OP_SKIPPED) {
        if (GB_ASPRINTF(&entry, "%sDELETE SKIPPED: %s\n", tmp?tmp:"",
                        savereply->dop->skipped) == -1) {
          goto out;
        }
      } else if (savereply->dop->status) {
        if (savereply->force) {
          if (GB_ASPRINTF(&entry, "%sDELETE FAILED (ignored): %s\n", tmp?tmp:"",
                savereply->dop->attempt) == -1) {
            goto out;
          }
          /* mimic success */
          savereply->dop->status = 0;
        } else {
          if (GB_ASPRINTF(&entry, "%sDELETE FAILED: %s\n", tmp?tmp:"",
                savereply->dop->attempt) == -1) {
            goto out;
          }
        }
      } else {
        if (GB_ASPRINTF(&entry, "%sDELETE SUCCESS: %s\n", tmp?tmp:"",
                        savereply->dop->success) == -1) {
          goto out;
        }
      }
      GB_FREE(tmp);

      if (savereply->rop->status == GB_OP_SKIPPED) {
        tmp = entry;
        if (GB_ASPRINTF(&entry, "%sREPLACE PORTAL SKIPPED ON: %s\n", tmp?tmp:"",
                        savereply->rop->skipped) == -1) {
          goto out;
        }
        GB_FREE(tmp);
      } else {
        if (savereply->rop->attempt) {
          tmp = entry;
          if (GB_ASPRINTF(&entry, "%sREPLACE PORTAL FAILED ON: %s\n", tmp?tmp:"",
                savereply->rop->attempt) == -1) {
            goto out;
          }
          GB_FREE(tmp);
        }
        if (savereply->rop->success) {
          tmp = entry;
          if (GB_ASPRINTF(&entry, "%sREPLACE PORTAL SUCCESS ON: %s\n", tmp?tmp:"",
                savereply->rop->success?savereply->rop->success:"N/A") == -1) {
            goto out;
          }
          GB_FREE(tmp);
        }
      }

      tmp = entry;
      if ((savereply->cop->status == GB_OP_SKIPPED || !savereply->cop->status) &&
          (savereply->dop->status == GB_OP_SKIPPED || !savereply->dop->status) &&
          (savereply->rop->status == GB_OP_SKIPPED || !savereply->rop->status)) {
        if (GB_ASPRINTF(&entry, "%sRESULT: SUCCESS\n", tmp?tmp:"") == -1) {
          goto out;
        }
      } else {
        if (GB_ASPRINTF(&entry, "%sRESULT: FAIL\n", tmp?tmp:"") == -1) {
          goto out;
        }
      }
      GB_FREE(tmp);
    }

    if (GB_ASPRINTF(&reply->out, "%s\n", entry) == -1) {
      goto out;
    }
    GB_FREE(entry);
  }

out:
  /*catch all*/
  if (!reply->out) {
    blockFormatErrorResponse(REPLACE_SRV, blk->json_resp, errCode,
                             GB_DEFAULT_ERRMSG, reply);
  }

  GB_FREE (tmp);
  return;
}


static blockResponse *
block_replace_cli_1_svc_st(blockReplaceCli *blk, struct svc_req *rqstp)
{
  blockRemoteReplaceResp *savereply = NULL;
  blockResponse *reply = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  int errCode = -1;
  char *errMsg = NULL;
  int ret;
  blockServerDefPtr list = NULL;
  MetaInfo *info = NULL;


  LOG("mgmt", GB_LOG_DEBUG,
      "replace cli request, volume=%s, blockname=%s oldnode=%s newnode=%s force=%d",
      blk->volume, blk->block_name, blk->old_node, blk->new_node, blk->force);

  if (GB_ALLOC(reply) < 0) {
    goto optfail;
  }
  reply->exit = -1;

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
  LOG("cmdlog", GB_LOG_INFO, "%s", blk->cmd);

  if (glfs_access(glfs, blk->block_name, F_OK)) {
    errCode = errno;
    if (errCode == ENOENT) {
      GB_ASPRINTF(&errMsg, "block %s/%s doesn't exist",
                  blk->volume, blk->block_name);
      LOG("mgmt", GB_LOG_ERROR,
          "block with name %s doesn't exist in the volume %s",
          blk->block_name, blk->volume);
    } else {
      GB_ASPRINTF(&errMsg, "block %s/%s is not accessible (%s)",
                  blk->volume, blk->block_name, strerror(errCode));
      LOG("mgmt", GB_LOG_ERROR, "block %s/%s is not accessible (%s)",
          blk->volume, blk->block_name, strerror(errCode));
    }
    goto out;
  }

  ret = blockParseValidServers(glfs, blk->block_name, &errCode, &list,
                               blk->force?blk->old_node:NULL);
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "blockParseValidServers(%s): on volume %s failed[%s]",
        blk->block_name, blk->volume, strerror(errno));
    goto out;
  }

  errCode = glusterBlockCheckCapabilities((void *)blk, REPLACE_SRV, list, NULL, &errMsg);
  if (errCode) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockCheckCapabilities() for block %s on volume %s failed",
        blk->block_name, blk->volume);
    goto out;
  }

  if (GB_ALLOC(info) < 0) {
    errCode = ENOMEM;
    goto out;
  }
  ret = blockGetMetaInfo(glfs, blk->block_name, info, NULL);
  if (ret) {
    errCode = ret;
    goto out;
  }

  ret = glusterBlockReplaceNodeRemoteAsync(glfs, blk, info, blk->block_name, &savereply);
  if (ret) {
    LOG("mgmt", GB_LOG_WARNING, "glusterBlockReplaceNodeRemoteAsync: return"
        " %d %s for single block %s on volume %s", ret, FAILED_REMOTE_REPLACE,
        blk->block_name, blk->volume);
    if (ret == GB_NODE_NOT_EXIST) {
      GB_ASPRINTF(&errMsg, "block '%s' is not configured on node '%s' for volume '%s'",
                           blk->block_name,  blk->old_node, blk->volume);
      errCode = ret;
      goto out;
    } else if (ret == GB_NODE_IN_USE) {
      GB_ASPRINTF(&errMsg, "block '%s' was already configured on node '%s' for volume '%s'",
                           blk->block_name,  blk->new_node, blk->volume);
      errCode = ret;
      goto out;
    }
  }
  if (savereply && savereply->force && savereply->dop->status) {
    GB_METAUPDATE_OR_GOTO(lock, glfs,  blk->block_name,  blk->volume,
                          errCode, errMsg, out, "%s: CLEANUPSUCCESS\n", blk->old_node);
  }
  if (info->prio_path[0] && !strcmp(info->prio_path, blk->old_node)) {
    GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
        errCode, errMsg, out, "PRIOPATH: %s\n", blk->new_node);
  }

 out:
  GB_METAUNLOCK(lkfd, blk->volume, errCode, errMsg);
  blockReplaceNodeCliFormatResponse(blk, errCode, errMsg, savereply, reply);
  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s",
      reply ? reply->out : "*Nil*");
  blockServerDefFree(list);
  blockRemoteReplaceRespFree(savereply);
  blockFreeMetaInfo(info);

optfail:
  LOG("mgmt", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO),
      "replace cli return %s, volume=%s",
      errCode ? "failure" : "success", blk->volume);

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): on volume %s for block %s failed[%s]",
        GB_TXLOCKFILE, blk->volume, blk->block_name, strerror(errno));
  }

  GB_FREE(errMsg);

  return reply;
}


static blockResponse *
block_replace_1_svc_st(blockReplace *blk, struct svc_req *rqstp)
{
  blockResponse *reply = NULL;
  char *path = NULL;
  char *save = NULL;
  char *exec = NULL;
  char *tpg;


  LOG("mgmt", GB_LOG_INFO,
      "replace portal request, volume=%s blockname=%s iqn=%s old_portal=%s "
      "new_portal=%s", blk->volume, blk->block_name, blk->gbid, blk->ripaddr, blk->ipaddr);

  if (GB_ALLOC(reply) < 0) {
    goto out;
  }
  reply->exit = -1;

  if (GB_ALLOC_N(reply->out, 8192) < 0) {
    GB_FREE(reply);
    goto out;
  }

  if (GB_ASPRINTF(&exec, GB_CHECK_PORTAL, blk->gbid, blk->ipaddr) == -1) {
    goto out;
  }

  if (!gbRunner(exec)) {
    reply->exit = GB_OP_SKIPPED;
    snprintf(reply->out, 8192, "remote portal %s already exist", blk->ipaddr);
    goto out;
  }
  GB_FREE(exec);

  if (GB_ASPRINTF(&exec, GB_GET_PORTAL_TPG, blk->gbid,
        blk->ripaddr, blk->ripaddr) == -1) {
    goto out;
  }

  /* get number of tpg's for this target */
  GB_CMD_EXEC_AND_VALIDATE(exec, reply, blk, blk->volume, REPLACE_GET_PORTAL_TPG_SRV);
  if (reply->exit) {
    snprintf(reply->out, 8192, "failed to get portal tpg");
    goto out;
  }
  GB_FREE(exec);
  tpg = strtok(reply->out, "\n");

  if (GB_ASPRINTF(&path, "%s/%s%s/%s/portals", GB_TGCLI_ISCSI_PATH,
                  GB_TGCLI_IQN_PREFIX, blk->gbid, tpg) == -1) {
    goto out;
  }

  if (GB_ASPRINTF(&save, GB_TGCLI_GLFS_SAVE, blk->block_name) == -1) {
    goto out;
  }

  if (GB_ASPRINTF(&exec,
                  "targetcli <<EOF\n%s delete %s ip_port=3260\n%s create %s\n%s\nexit\nEOF",
                  path, blk->ripaddr, path, blk->ipaddr, save) == -1) {
    goto out;
  }
  GB_FREE(path);

  GB_CMD_EXEC_AND_VALIDATE(exec, reply, blk, blk->volume, REPLACE_SRV);
  if (reply->exit) {
    snprintf(reply->out, 8192, "replace portal failed");
    goto out;
  }
  GB_FREE(exec);

out:
  GB_FREE(path);
  GB_FREE(exec);
  GB_FREE(save);
  return reply;
}


bool_t
block_replace_1_svc(blockReplace *blk, blockResponse *reply, struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(replace, blk, reply, rqstp, ret);
  return ret;
}


bool_t
block_replace_cli_1_svc(blockReplaceCli *blk, blockResponse *reply,
                        struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(replace_cli, blk, reply, rqstp, ret);
  return ret;
}
