/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "block_common.h"


static void *
glusterBlockModifyRemote(void *data)
{
  int ret;
  int saveret;
  blockRemoteObj *args = (blockRemoteObj *)data;
  blockModify cobj = *(blockModify *)args->obj;
  char *errMsg = NULL;
  bool rpc_sent = FALSE;


  GB_METAUPDATE_OR_GOTO(lock, args->glfs, cobj.block_name, cobj.volume,
                        ret, errMsg, out, "%s: AUTH%sENFORCEING\n", args->addr,
                        cobj.auth_mode?"":"CLEAR");

  ret = glusterBlockCallRPC_1(args->addr, &cobj, MODIFY_SRV, &rpc_sent,
                              &args->reply);
  if (ret) {
    saveret = ret;
    if (!rpc_sent) {
      GB_ASPRINTF(&errMsg, ": %s", strerror(errno));
      LOG("mgmt", GB_LOG_ERROR, "%s hence %s for block %s on "
          "host %s volume %s", strerror(errno), FAILED_REMOTE_MODIFY,
          cobj.block_name, args->addr, args->volume);
      goto out;
    } else if (args->reply) {
      errMsg = args->reply;
      args->reply = NULL;
    }

    GB_METAUPDATE_OR_GOTO(lock, args->glfs, cobj.block_name, cobj.volume,
                          ret, errMsg, out, "%s: AUTH%sENFORCEFAIL\n",
                          args->addr, cobj.auth_mode?"":"CLEAR");
    LOG("mgmt", GB_LOG_ERROR, "%s for block %s on host %s volume %s",
        FAILED_REMOTE_MODIFY, cobj.block_name, args->addr, args->volume);

    ret = saveret;
    goto out;
  }

  GB_METAUPDATE_OR_GOTO(lock, args->glfs, cobj.block_name, cobj.volume,
                        ret, errMsg, out, "%s: AUTH%sENFORCED\n", args->addr,
                        cobj.auth_mode?"":"CLEAR");

 out:
  if (!args->reply) {
    if (GB_ASPRINTF(&args->reply, "failed to configure auth on %s %s",
                 args->addr, errMsg?errMsg:"") == -1) {
      ret = ret?ret:-1;
    }
  }
  GB_FREE(errMsg);
  args->exit = ret;

  return NULL;
}


static void *
glusterBlockModifySizeRemote(void *data)
{
  int ret;
  int saveret;
  blockRemoteObj *args = (blockRemoteObj *)data;
  blockModifySize mobj = *(blockModifySize *)args->obj;
  char *errMsg = NULL;
  bool rpc_sent = FALSE;


  GB_METAUPDATE_OR_GOTO(lock, args->glfs, mobj.block_name, mobj.volume,
                        ret, errMsg, out, "%s: RSINPROGRESS-%zu\n",
                        args->addr, mobj.size);

  ret = glusterBlockCallRPC_1(args->addr, &mobj, MODIFY_SIZE_SRV, &rpc_sent,
                              &args->reply);
  if (ret) {
    saveret = ret;
    if (!rpc_sent) {
      GB_ASPRINTF(&errMsg, ": %s", strerror(errno));
      LOG("mgmt", GB_LOG_ERROR,
          "%s hence %s for block %s on volume %s for size %zu on host %s",
          strerror(errno), FAILED_REMOTE_MODIFY_SIZE,
          mobj.block_name, mobj.volume, mobj.size, args->addr);
      goto out;
    } else if (args->reply) {
      errMsg = args->reply;
      args->reply = NULL;
    }
    GB_METAUPDATE_OR_GOTO(lock, args->glfs, mobj.block_name, mobj.volume,
                          ret, errMsg, out, "%s: RSFAIL-%zu\n", args->addr, mobj.size);

    LOG("mgmt", GB_LOG_ERROR, "%s for block %s on volume %s for size %zu on host %s",
        FAILED_REMOTE_MODIFY, mobj.block_name, mobj.volume, mobj.size, args->addr);

    ret = saveret;
    goto out;
  }

  GB_METAUPDATE_OR_GOTO(lock, args->glfs, mobj.block_name, mobj.volume,
                        ret, errMsg, out, "%s: RSSUCCESS-%zu\n",
                        args->addr, mobj.size);

 out:
  if (!args->reply) {
    if (GB_ASPRINTF(&args->reply, "failed to modify size on %s %s",
                    args->addr, errMsg?errMsg:"") == -1) {
      ret = ret?ret:-1;
    }
  }
  GB_FREE(errMsg);
  args->exit = ret;

  return NULL;
}


static size_t
glusterBlockModifyArgsFill(blockModify *mobj, MetaInfo *info,
                           blockRemoteObj *args, struct glfs *glfs)
{
  int i = 0;
  size_t count = 0;
  bool fill = FALSE;

  for (i = 0, count = 0; i < info->nhosts; i++) {
    switch (blockMetaStatusEnumParse(info->list[i]->status)) {
      case GB_CONFIG_SUCCESS:
      case GB_CLEANUP_INPROGRESS:
      case GB_AUTH_ENFORCE_FAIL:
      case GB_AUTH_CLEAR_ENFORCED:
      case GB_RP_SUCCESS:
      case GB_RP_FAIL:
      case GB_RP_INPROGRESS:
      case GB_RS_SUCCESS:
      case GB_RS_FAIL:
      case GB_RS_INPROGRESS:
        if (mobj->auth_mode) {
          fill = TRUE;
        }
        break;
      /* case GB_AUTH_ENFORCED: this is not required to be configured */
      case GB_AUTH_ENFORCED:
        if (!mobj->auth_mode) {
          fill = TRUE;
        }
        break;
      case GB_AUTH_ENFORCEING:
      case GB_AUTH_CLEAR_ENFORCEING:
      case GB_AUTH_CLEAR_ENFORCE_FAIL:
        fill = TRUE;
        break;
    }
    if (fill) {
      if (args) {
        args[count].glfs = glfs;
        args[count].obj = (void *)mobj;
        args[count].volume = info->volume;
        args[count].addr = info->list[i]->addr;
      }
      count++;
    }
    fill = FALSE;
  }
  return count;
}


static int
glusterBlockModifyRemoteAsync(MetaInfo *info,
                              struct glfs *glfs,
                              blockModify *mobj,
                              blockRemoteModifyResp **savereply,
                              bool rollback)
{
  pthread_t  *tid = NULL;
  blockRemoteModifyResp *local = *savereply;
  blockRemoteObj *args = NULL;
  int ret = -1;
  size_t i;
  size_t count = 0;


  /* get all (configured - already auth enforced) node count */
  count = glusterBlockModifyArgsFill(mobj, info, NULL, glfs);

  if (GB_ALLOC_N(tid, count) < 0) {
    goto out;
  }

  if (GB_ALLOC_N(args, count) < 0) {
    goto out;
  }

  count = glusterBlockModifyArgsFill(mobj, info, args, glfs);

  for (i = 0; i < count; i++) {
    pthread_create(&tid[i], NULL, glusterBlockModifyRemote, &args[i]);
  }

  for (i = 0; i < count; i++) {
    /* collect exit code */
    pthread_join(tid[i], NULL);
  }

  if (!rollback) {
    /* collect return */
    ret = glusterBlockCollectAttemptSuccess(args, info, MODIFY_SRV, count, &local->attempt,
                                            &local->success);
    if (ret)
      goto out;
  } else {
    /* collect return */
    ret = glusterBlockCollectAttemptSuccess(args, info, MODIFY_SRV, count, &local->rb_attempt,
                                            &local->rb_success);
    if (ret)
      goto out;
  }
  for (i = 0; i < count; i++) {
    if (args[i].exit) {
      ret = -1;
      break;
    }
  }

  *savereply = local;

 out:
  GB_FREE(args);
  GB_FREE(tid);

  return ret;
}


static size_t
glusterBlockModifySizeArgsFill(blockModifySize *mobj, MetaInfo *info,
                               blockRemoteObj *args, struct glfs *glfs,
                               char **skipped)
{
  int i = 0;
  size_t count = 0;
  char *saveptr = NULL;

  for (i = 0, count = 0; i < info->nhosts; i++) {
    if (skipped && (info->size == mobj->size) &&
        (blockMetaStatusEnumParse(info->list[i]->status) == GB_RS_SUCCESS)) {
      saveptr = *skipped;
      GB_ASPRINTF(skipped, "%s %s", (saveptr?saveptr:""), info->list[i]->addr);
      GB_FREE(saveptr);
    } else if (blockhostIsValid(info->list[i]->status)) {
      if (args) {
        args[count].glfs = glfs;
        args[count].obj = (void *)mobj;
        args[count].volume = info->volume;
        args[count].addr = info->list[i]->addr;
      }
      count++;
    }
  }

  return count;
}


static int
glusterBlockModifySizeRemoteAsync(MetaInfo *info,
                                  struct glfs *glfs,
                                  blockModifySize *mobj,
                                  blockRemoteResp **savereply)
{
  pthread_t  *tid = NULL;
  blockRemoteResp *local = *savereply;
  blockRemoteObj *args = NULL;
  int ret = -1;
  size_t i;
  size_t count = 0;


  count = glusterBlockModifySizeArgsFill(mobj, info, NULL, glfs, NULL);

  if (GB_ALLOC_N(tid, count) < 0) {
    goto out;
  }

  if (GB_ALLOC_N(args, count) < 0) {
    goto out;
  }

  count = glusterBlockModifySizeArgsFill(mobj, info, args, glfs, &local->skipped);

  for (i = 0; i < count; i++) {
    pthread_create(&tid[i], NULL, glusterBlockModifySizeRemote, &args[i]);
  }

  for (i = 0; i < count; i++) {
    /* collect exit code */
    pthread_join(tid[i], NULL);
  }

  /* collect return */
  ret = glusterBlockCollectAttemptSuccess(args, info, MODIFY_SIZE_SRV, count,
                                          &local->attempt, &local->success);
  if (ret)
    goto out;

  for (i = 0; i < count; i++) {
    if (args[i].exit) {
      ret = -1;
      break;
    }
  }

  *savereply = local;

 out:
  GB_FREE(args);
  GB_FREE(tid);

  return ret;
}


static void
blockModifyCliFormatResponse (blockModifyCli *blk, blockModify *mobj,
                              int errCode, char *errMsg,
                              blockRemoteModifyResp *savereply,
                              MetaInfo *info, blockResponse *reply,
                              bool rollback)
{
  json_object *json_obj = NULL;
  char        *tmp2 = NULL;
  char        *tmp3 = NULL;
  char        *tmp = NULL;

  if (!reply) {
    return;
  }

  if (errCode < 0) {
    errCode = GB_DEFAULT_ERRCODE;
  }
  reply->exit = errCode;

  if (errMsg) {
    blockFormatErrorResponse(MODIFY_SRV, blk->json_resp, errCode,
                             errMsg, reply);
    return;
  }

  if (blk->json_resp) {
    json_obj = json_object_new_object();

    GB_ASPRINTF(&tmp, "%s%s", GB_TGCLI_IQN_PREFIX, info->gbid);
    json_object_object_add(json_obj, "IQN",
                           GB_JSON_OBJ_TO_STR(tmp?tmp:""));
    if (!errCode && mobj->auth_mode) {
      json_object_object_add(json_obj, "USERNAME",
                             GB_JSON_OBJ_TO_STR(info->gbid));
      json_object_object_add(json_obj, "PASSWORD",
                             GB_JSON_OBJ_TO_STR(mobj->passwd));
    }

    if (savereply->attempt) {
      blockStr2arrayAddToJsonObj(json_obj, savereply->attempt, "FAILED ON");
    }

    if (savereply->success) {
      blockStr2arrayAddToJsonObj(json_obj, savereply->success, "SUCCESSFUL ON");
    }

    if (rollback) {
      if (savereply->rb_attempt) {
        blockStr2arrayAddToJsonObj(json_obj, savereply->rb_attempt,
                                   "ROLLBACK FAILED ON");
      }

      if (savereply->rb_success) {
        blockStr2arrayAddToJsonObj(json_obj, savereply->rb_success,
                                   "ROLLBACK SUCCESS ON");
      }
    }

    json_object_object_add(json_obj, "RESULT",
      errCode?GB_JSON_OBJ_TO_STR("FAIL"):GB_JSON_OBJ_TO_STR("SUCCESS"));

    GB_ASPRINTF(&reply->out, "%s\n", json_object_to_json_string_ext(json_obj,
                                     mapJsonFlagToJsonCstring(blk->json_resp)));

    json_object_put(json_obj);
  } else {
    /* save 'failed on'*/
    if (savereply->attempt) {
      GB_ASPRINTF(&tmp, "FAILED ON: %s\n", savereply->attempt);
    }

    if (savereply->success) {
      GB_ASPRINTF(&tmp2, "SUCCESSFUL ON: %s\n", savereply->success);
    }

    if (!errCode && mobj->auth_mode) {
      GB_ASPRINTF(&tmp3, "IQN: %s%s\nUSERNAME: %s\nPASSWORD: %s\n%s%s",
                  GB_TGCLI_IQN_PREFIX, info->gbid,
                  info->gbid, mobj->passwd, tmp?tmp:"", tmp2?tmp2:"");
    } else {
      GB_ASPRINTF(&tmp3, "IQN: %s%s\n%s%s",
                  GB_TGCLI_IQN_PREFIX, info->gbid,
                  tmp?tmp:"", tmp2?tmp2:"");
    }

    GB_FREE(tmp);
    GB_FREE(tmp2);

    if (rollback) {
      if (savereply->rb_attempt) {
        GB_ASPRINTF(&tmp, "ROLLBACK FAILED ON: %s\n", savereply->rb_attempt);
      }
      if (savereply->rb_success) {
        GB_ASPRINTF(&tmp2, "ROLLBACK SUCCESS ON: %s\n", savereply->rb_success);
      }
    }

    GB_ASPRINTF(&reply->out, "%s%s%sRESULT: %s\n", tmp3, savereply->rb_attempt?tmp:"",
                savereply->rb_success?tmp2:"", errCode?"FAIL":"SUCCESS");
    GB_FREE(tmp2);
    GB_FREE(tmp3);
  }
  GB_FREE(tmp);

  /*catch all*/
  if (!reply->out) {
    blockFormatErrorResponse(MODIFY_SRV, blk->json_resp, errCode,
                             GB_DEFAULT_ERRMSG, reply);
  }

}


static blockResponse *
block_modify_cli_1_svc_st(blockModifyCli *blk, struct svc_req *rqstp)
{
  int ret = -1;
  blockModify mobj = {{0}};
  blockRemoteModifyResp *savereply = NULL;
  blockResponse *reply = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  MetaInfo *info = NULL;
  uuid_t uuid;
  char passwd[UUID_BUF_SIZE];
  int asyncret = 0;
  bool rollback = false;
  int errCode = -1;
  char *errMsg = NULL;
  blockServerDefPtr list = NULL;


  LOG("mgmt", GB_LOG_INFO,
      "modify auth cli request, volume=%s blockname=%s authmode=%d",
      blk->volume, blk->block_name, blk->auth_mode);

  if ((GB_ALLOC(reply) < 0) || (GB_ALLOC(savereply) < 0) ||
      (GB_ALLOC (info) < 0)) {
    GB_FREE (reply);
    GB_FREE (savereply);
    GB_FREE (info);
    goto initfail;
  }

  errCode = 0;
  glfs = glusterBlockVolumeInit(blk->volume, &errCode, &errMsg);
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockVolumeInit(%s) for block %s failed [%s]",
        blk->volume, blk->block_name, strerror(errno));
    goto initfail;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume, &errCode, &errMsg);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s %s for block %s",
        FAILED_CREATING_META, blk->volume, blk->block_name);
    goto nolock;
  }

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, errCode, errMsg, nolock);
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

  ret = blockGetMetaInfo(glfs, blk->block_name, info, NULL);
  if (ret) {
    errCode = ret;
    goto out;
  }

  list = glusterBlockGetListFromInfo(info);
  if (!list) {
    errCode = ENOMEM;
    goto out;
  }

  if (list->nhosts != info->mpath) {
    errCode = ENOENT;

    GB_ASPRINTF(&errMsg, "Some of the nodes are missing configuration, " \
                "please look at logs for recent operations on the %s.",
                blk->block_name);
    LOG("mgmt", GB_LOG_ERROR, "%s", errMsg);

    goto out;
  }

  errCode = glusterBlockCheckCapabilities((void *)blk, MODIFY_SRV, list, NULL, &errMsg);
  if (errCode) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockCheckCapabilities() for block %s on volume %s failed",
        blk->block_name, blk->volume);
    goto out;
  }

  GB_STRCPYSTATIC(mobj.block_name, blk->block_name);
  GB_STRCPYSTATIC(mobj.volume, blk->volume);
  GB_STRCPYSTATIC(mobj.gbid, info->gbid);

  if (blk->auth_mode) {
    if(info->passwd[0] == '\0') {
      uuid_generate(uuid);
      uuid_unparse(uuid, passwd);
      GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                            errCode, errMsg, out, "PASSWORD: %s\n", passwd);
      GB_STRCPYSTATIC(mobj.passwd, passwd);
    } else {
      GB_STRCPYSTATIC(mobj.passwd, info->passwd);
    }
    mobj.auth_mode = 1;
  } else {
    GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                          errCode, errMsg, out, "PASSWORD: \n");
    mobj.auth_mode = 0;
  }

  asyncret = glusterBlockModifyRemoteAsync(info, glfs, &mobj,
                                           &savereply, rollback);
  if (asyncret) {   /* asyncret decides result is success/fail */
    errCode = asyncret;
    LOG("mgmt", GB_LOG_WARNING,
        "glusterBlockModifyRemoteAsync(auth=%d): return %d %s for block %s on volume %s",
        blk->auth_mode, asyncret, FAILED_REMOTE_AYNC_MODIFY, blk->block_name, info->volume);

    /* Unwind by removing authentication */
    if (blk->auth_mode) {
      GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                            errCode, errMsg, out, "PASSWORD: \n");
    }

    /* Collect new Meta status */
    blockFreeMetaInfo(info);
    if (GB_ALLOC(info) < 0) {
      errCode = ENOMEM;
      goto out;
    }
    ret = blockGetMetaInfo(glfs, blk->block_name, info, NULL);
    if (ret) {
      errCode = ret;
      goto out;
    }

    /* toggle */
    mobj.auth_mode = !mobj.auth_mode;

    rollback = true;

    /* undo */
    ret = glusterBlockModifyRemoteAsync(info, glfs, &mobj,
                                        &savereply, rollback);
    if (ret) {
      LOG("mgmt", GB_LOG_WARNING,
          "glusterBlockModifyRemoteAsync(auth=%d): on rollback return %d %s "
          "for block %s on volume %s",  blk->auth_mode, ret, FAILED_REMOTE_AYNC_MODIFY,
          blk->block_name, info->volume);
      /* do nothing ? */
      errCode = ret;
      goto out;
    }
  }

 out:
  GB_METAUNLOCK(lkfd, blk->volume, errCode, errMsg);
  blockServerDefFree(list);

 nolock:
  LOG("mgmt", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO),
      "modify auth cli return %s, volume=%s blockname=%s",
      errCode ? "failure" : "success", blk->volume, blk->block_name);

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): for block %s on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->block_name, blk->volume, strerror(errno));
  }

 initfail:
  blockModifyCliFormatResponse (blk, &mobj, asyncret?asyncret:errCode,
                                errMsg, savereply, info, reply, rollback);
  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s",
      reply ? reply->out : "*Nil*");
  blockFreeMetaInfo(info);

  if (savereply) {
    GB_FREE(savereply->attempt);
    GB_FREE(savereply->success);
    GB_FREE(savereply->rb_attempt);
    GB_FREE(savereply->rb_success);
    GB_FREE(savereply);
  }
  GB_FREE(errMsg);

  return reply;
}


static void
blockModifySizeCliFormatResponse(blockModifySizeCli *blk, blockModifySize *mobj,
                                 int errCode, char *errMsg,
                                 blockRemoteResp *savereply,
                                 MetaInfo *info, blockResponse *reply)
{
  json_object *json_obj = NULL;
  char        *tmp = NULL;
  char        *tmp2 = NULL;
  char        *tmp3 = NULL;
  char        *hr_size = NULL;           /* Human Readable size */
  int i;

  if (!reply) {
    return;
  }

  if (errCode < 0) {
    errCode = GB_DEFAULT_ERRCODE;
  }
  reply->exit = errCode;

  if (errMsg) {
    blockFormatErrorResponse(MODIFY_SIZE_SRV, blk->json_resp, errCode,
                             errMsg, reply);
    return;
  }

  hr_size = glusterBlockFormatSize("mgmt", mobj->size);
  if (!hr_size) {
    GB_ASPRINTF (&errMsg, "failed in glusterBlockFormatSize");
    blockFormatErrorResponse(MODIFY_SIZE_SRV, blk->json_resp, ENOMEM,
                             errMsg, reply);
    GB_FREE(errMsg);
    return;
  }

  if (blk->json_resp) {
    json_obj = json_object_new_object();

    GB_ASPRINTF(&tmp, "%s%s", GB_TGCLI_IQN_PREFIX, info->gbid);
    json_object_object_add(json_obj, "IQN", GB_JSON_OBJ_TO_STR(tmp?tmp:""));
    if (!errCode) {
      json_object_object_add(json_obj, "SIZE", GB_JSON_OBJ_TO_STR(hr_size));
    }

    if (savereply->attempt) {
      blockStr2arrayAddToJsonObj(json_obj, savereply->attempt, "FAILED ON");
    }

    if (savereply->success) {
      blockStr2arrayAddToJsonObj(json_obj, savereply->success, "SUCCESSFUL ON");
    }

    if (savereply->skipped) {
      blockStr2arrayAddToJsonObj(json_obj, savereply->skipped, "SKIPPED ON");
    }

    json_object_object_add(json_obj, "RESULT",
      errCode?GB_JSON_OBJ_TO_STR("FAIL"):GB_JSON_OBJ_TO_STR("SUCCESS"));

  } else {
    /* save 'failed on'*/
    if (savereply->attempt) {
      GB_ASPRINTF(&tmp, "FAILED ON: %s\n", savereply->attempt);
    }

    if (savereply->success) {
      GB_ASPRINTF(&tmp2, "SUCCESSFUL ON: %s\n", savereply->success);
    }

    if (!errCode) {
      GB_ASPRINTF(&tmp3, "IQN: %s%s\nSIZE: %s\n%s%s",
                  GB_TGCLI_IQN_PREFIX, info->gbid,
                  hr_size, tmp?tmp:"", tmp2?tmp2:"");
    } else {
      GB_ASPRINTF(&tmp3, "IQN: %s%s\n%s%s",
                  GB_TGCLI_IQN_PREFIX, info->gbid,
                  tmp?tmp:"", tmp2?tmp2:"");
    }

    if (savereply->skipped) {
      GB_FREE(tmp);
      tmp = tmp3;
      GB_ASPRINTF(&tmp3, "%sSKIPPED ON:%s\n",tmp, savereply->skipped);
    }

    GB_ASPRINTF(&reply->out, "%sRESULT: %s\n", tmp3, errCode?"FAIL":"SUCCESS");
  }
  GB_FREE(tmp);
  GB_FREE(tmp2);
  GB_FREE(tmp3);

  /*catch all*/
  if (errCode && !errMsg) {
    GB_ASPRINTF (&errMsg, "block volume resize failed:");
    for (i = 0; i < info->nhosts; i++) {
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
        tmp = errMsg;
        GB_ASPRINTF (&errMsg, "%s %s:[%s]", tmp?tmp:"",
                     info->list[i]->addr, hr_size?hr_size:"null");
        GB_FREE(tmp);
      }
    }
  }
  if (blk->json_resp) {
    if (errCode) {
      json_object_object_add(json_obj, "errCode", json_object_new_int(errCode));
      json_object_object_add(json_obj, "errMsg", GB_JSON_OBJ_TO_STR(errMsg));
    }
    GB_ASPRINTF(&reply->out, "%s\n", json_object_to_json_string_ext(json_obj,
                mapJsonFlagToJsonCstring(blk->json_resp)));
    json_object_put(json_obj);
  } else {
    if (errCode) {
      GB_ASPRINTF (&reply->out, "%s\n", (errMsg?errMsg:"null"));
    }
  }

  GB_FREE(errMsg);
  GB_FREE(hr_size);
}


static bool
glusterBlockIsResizeFailed(MetaInfo *info)
{
  int i;

  for (i = 0; i < info->nhosts; i++) {
    switch (blockMetaStatusEnumParse(info->list[i]->status)) {
      case GB_RS_FAIL:
      case GB_RS_INPROGRESS:
        return true;
    }
  }

  return false;
}


static blockResponse *
block_modify_size_cli_1_svc_st(blockModifySizeCli *blk, struct svc_req *rqstp)
{
  int ret = -1;
  blockModifySize mobj = {{0},};
  blockRemoteResp *savereply = NULL;
  blockResponse *reply = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  MetaInfo *info = NULL;
  int asyncret = 0;
  int errCode = -1;
  char *errMsg = NULL;
  char *cSize = NULL;
  char *rSize = NULL;
  blockServerDefPtr list = NULL;


  LOG("mgmt", GB_LOG_INFO,
      "modify size cli request, volume=%s blockname=%s size=%zu",
      blk->volume, blk->block_name, blk->size);

  if ((GB_ALLOC(reply) < 0) || (GB_ALLOC(savereply) < 0) ||
      (GB_ALLOC (info) < 0)) {
    GB_FREE (reply);
    GB_FREE (savereply);
    GB_FREE (info);
    goto initfail;
  }

  errCode = 0;
  glfs = glusterBlockVolumeInit(blk->volume, &errCode, &errMsg);
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockVolumeInit(%s) for block %s failed [%s]",
        blk->volume, blk->block_name, strerror(errno));
    goto initfail;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume, &errCode, &errMsg);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s %s for block %s",
        FAILED_CREATING_META, blk->volume, blk->block_name);
    goto nolock;
  }

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, errCode, errMsg, nolock);
  LOG("cmdlog", GB_LOG_INFO, "%s",  blk->cmd);

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

  ret = blockGetMetaInfo(glfs, blk->block_name, info, NULL);
  if (ret) {
    errCode = ret;
    goto out;
  }

  if (info->blk_size && (blk->size % info->blk_size)) {
    GB_ASPRINTF(&errMsg, "size (%lu) is incorrect, it should be aligned to block size (%lu)",
                blk->size, info->blk_size);
    LOG("mgmt", GB_LOG_ERROR,
        "size (%lu) is incorrect, it should be aligned to block size (%lu)",
        blk->size, info->blk_size);
    errCode = EINVAL;
    goto out;
  }

  if ((info->size > blk->size && !blk->force) ||
      (info->size == blk->size && !glusterBlockIsResizeFailed(info))) {
    cSize = glusterBlockFormatSize("mgmt", info->size);
    rSize = glusterBlockFormatSize("mgmt", blk->size);
    if (info->size == blk->size) {
      GB_ASPRINTF(&errMsg, "request size (%s) is same as current size (%s) ]",
                  cSize, rSize);
    } else {
      GB_ASPRINTF(&errMsg, "Shrink size ?\nuse 'force' option [current size %s, request size %s]",
                  cSize, rSize);
    }
    GB_FREE(cSize);
    GB_FREE(rSize);
    errCode = -1;
    goto out;
  }

  list = glusterBlockGetListFromInfo(info);
  if (!list) {
    errCode = ENOMEM;
    goto out;
  }

  if (list->nhosts != info->mpath) {
    errCode = ENOENT;

    GB_ASPRINTF(&errMsg, "Some of the nodes are missing configuration, " \
                "please look at logs for recent operations on the %s.",
                blk->block_name);
    LOG("mgmt", GB_LOG_ERROR, "%s", errMsg);

    goto out;
  }

  errCode = glusterBlockCheckCapabilities((void *)blk, MODIFY_SIZE_SRV, list, NULL, &errMsg);
  if (errCode) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockCheckCapabilities() for block %s on volume %s failed",
        blk->block_name, blk->volume);
    goto out;
  }

  GB_STRCPYSTATIC(mobj.block_name, blk->block_name);
  GB_STRCPYSTATIC(mobj.volume, blk->volume);
  GB_STRCPYSTATIC(mobj.gbid, info->gbid);
  mobj.size = blk->size;

  ret = glusterBlockResizeEntry(glfs, &mobj, &errCode, &errMsg);
  if (ret) {
    errCode = ret;
    LOG("mgmt", GB_LOG_ERROR, "%s block: %s volume: %s file: %s size: %zu",
        FAILED_MODIFY_SIZE, mobj.block_name, mobj.volume, mobj.gbid, mobj.size);
    goto out;
  }

  GB_METAUPDATE_OR_GOTO(lock, glfs, mobj.block_name, mobj.volume,
                        errCode, errMsg, out, "SIZE: %zu\n",  mobj.size);

  asyncret = glusterBlockModifySizeRemoteAsync(info, glfs, &mobj, &savereply);
  if (asyncret) {   /* asyncret decides result is success/fail */
    errCode = asyncret;
    LOG("mgmt", GB_LOG_WARNING,
        "glusterBlockModifySizeRemoteAsync(size=%zu): return %d %s for block %s on volume %s",
        blk->size, asyncret, FAILED_REMOTE_AYNC_MODIFY, blk->block_name, info->volume);
    /* Refresh for latest details */
    blockFreeMetaInfo(info);
    if (GB_ALLOC(info) < 0) {
      errCode = ENOMEM;
      goto out;
    }
    blockGetMetaInfo(glfs, blk->block_name, info, NULL);
    goto out;
  }

 out:
  GB_METAUNLOCK(lkfd, blk->volume, errCode, errMsg);
  blockServerDefFree(list);

 nolock:
  LOG("mgmt", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO),
      "modify size cli return %s, volume=%s blockname=%s",
      errCode ? "failure" : "success", blk->volume, blk->block_name);

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): for block %s on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->block_name, blk->volume, strerror(errno));
  }

 initfail:
  blockModifySizeCliFormatResponse(blk, &mobj, asyncret?asyncret:errCode,
                                   errMsg, savereply, info, reply);
  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s",
      reply ? reply->out : "*Nil*");
  blockFreeMetaInfo(info);

  blockRemoteRespFree(savereply);
  GB_FREE(errMsg);

  return reply;
}


static blockResponse *
block_modify_1_svc_st(blockModify *blk, struct svc_req *rqstp)
{
  int ret;
  char *authattr = NULL;
  char *authcred = NULL;
  char *save = NULL;
  char *exec = NULL;
  blockResponse *reply = NULL;
  size_t tpgs = 0;
  size_t i;
  char *tmp = NULL;


  LOG("mgmt", GB_LOG_INFO,
      "modify request, volume=%s blockname=%s filename=%s authmode=%d passwd=%s",
      blk->volume, blk->block_name, blk->gbid, blk->auth_mode,
      blk->auth_mode?blk->passwd:"");

  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }
  reply->exit = -1;

  ret = blockCheckBlockLoadedStatus(blk->block_name, blk->gbid, reply);
  if (ret) {
    goto out;
  }

  if (GB_ASPRINTF(&exec, "targetcli %s/%s%s status", GB_TGCLI_ISCSI_PATH,
                  GB_TGCLI_IQN_PREFIX, blk->gbid) == -1) {
    goto out;
  }

  if (GB_ALLOC_N(reply->out, 8192) < 0) {
    GB_FREE(reply);
    goto out;
  }

  /* get number of tpg's for this target */
  GB_CMD_EXEC_AND_VALIDATE(exec, reply, blk, blk->volume, MODIFY_TPGC_SRV);
  if (reply->exit) {
    snprintf(reply->out, 8192, "modify failed");
    goto out;
  }

  /* out looks like, "Status for /iscsi/iqn.abc:xyz: TPGs: 2" */
  tmp = strrchr(reply->out, ':');
  if (tmp) {
    sscanf(tmp+1, "%zu", &tpgs);
    tmp = NULL;
  }

  for (i = 1; i <= tpgs; i++) {
    if (blk->auth_mode) {  /* set auth */
      if (GB_ASPRINTF(&authattr, "%s/%s%s/tpg%zu set attribute authentication=1",
                   GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i) == -1) {
        goto out;
      }

      if (GB_ASPRINTF(&authcred, "%s/%s%s/tpg%zu set auth userid=%s password=%s",
                   GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
                   blk->gbid, blk->passwd) == -1) {
        goto out;
      }

      if (!tmp) {
        if (GB_ASPRINTF(&exec, "%s\n%s", authattr, authcred) == -1) {
          goto out;
        }
        tmp = exec;
      } else {   /* append next series of commands */
        if (GB_ASPRINTF(&exec, "%s\n%s\n%s", tmp, authattr, authcred) == -1) {
          goto out;
        }
        GB_FREE(tmp);
        tmp = exec;
      }
    } else {      /* unset auth */
      if (!tmp) {
        if (GB_ASPRINTF(&exec, "%s/%s%s/tpg%zu set attribute authentication=0",
                     GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i) == -1) {
          goto out;
        }
        tmp = exec;
      } else {   /* append next series of commands */
        if (GB_ASPRINTF(&exec, "%s\n%s/%s%s/tpg%zu set attribute authentication=0",
                     tmp, GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i) == -1) {
          goto out;
        }
        GB_FREE(tmp);
        tmp = exec;
      }
    }
  }

  if (GB_ASPRINTF(&save, GB_TGCLI_GLFS_SAVE, blk->block_name) == -1) {
    goto out;
  }

  if (GB_ASPRINTF(&exec, "targetcli <<EOF\n%s\n%s\nexit\nEOF", tmp, save) == -1) {
    goto out;
  }

  GB_CMD_EXEC_AND_VALIDATE(exec, reply, blk, blk->volume, MODIFY_SRV);
  if (reply->exit) {
    snprintf(reply->out, 8192, "modify failed");
  }

 out:
  GB_FREE(tmp);
  GB_FREE(exec);
  GB_FREE(save);
  GB_FREE(authattr);
  GB_FREE(authcred);

  return reply;
}


static blockResponse *
block_modify_size_1_svc_st(blockModifySize *blk, struct svc_req *rqstp)
{
  int ret;
  char *save = NULL;
  char *exec = NULL;
  blockResponse *reply = NULL;
  char *tmp = NULL;


  LOG("mgmt", GB_LOG_INFO,
      "modify size request, volume=%s blockname=%s filename=%s size=%zu",
      blk->volume, blk->block_name, blk->gbid, blk->size);

  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }
  reply->exit = -1;

  ret = blockCheckBlockLoadedStatus(blk->block_name, blk->gbid, reply);
  if (ret) {
    goto out;
  }

  if (GB_ASPRINTF(&tmp, "%s/%s set attribute dev_size=%zu", GB_TGCLI_GLFS_PATH,
                  blk->block_name, blk->size) == -1) {
    goto out;
  }

  if (GB_ASPRINTF(&save, GB_TGCLI_GLFS_SAVE, blk->block_name) == -1) {
    goto out;
  }

  if (GB_ASPRINTF(&exec, "targetcli <<EOF\n%s\n%s\nexit\nEOF", tmp, save) == -1) {
    goto out;
  }

  if (GB_ALLOC_N(reply->out, 8192) < 0) {
    GB_FREE(reply);
    goto out;
  }

  GB_CMD_EXEC_AND_VALIDATE(exec, reply, blk, blk->volume, MODIFY_SIZE_SRV);
  if (reply->exit) {
    snprintf(reply->out, 8192, "modify size failed");
  }

 out:
  GB_FREE(tmp);
  GB_FREE(exec);
  GB_FREE(save);

  return reply;
}


bool_t
block_modify_1_svc(blockModify *blk, blockResponse *reply, struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(modify, blk, reply, rqstp, ret);
  return ret;
}


bool_t
block_modify_size_1_svc(blockModifySize *blk, blockResponse *reply,
                        struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(modify_size, blk, reply, rqstp, ret);
  return ret;
}


bool_t
block_modify_cli_1_svc(blockModifyCli *blk, blockResponse *reply,
                       struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(modify_cli, blk, reply, rqstp, ret);
  return ret;
}


bool_t
block_modify_size_cli_1_svc(blockModifySizeCli *blk, blockResponse *reply,
                            struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(modify_size_cli, blk, reply, rqstp, ret);
  return ret;
}
