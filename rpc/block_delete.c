/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "block_common.h"

# define   GB_DELETE            "delete"


void *
glusterBlockDeleteRemote(void *data)
{
  int ret;
  int saveret;
  blockRemoteObj *args = (blockRemoteObj *)data;
  blockDelete dobj = *(blockDelete *)args->obj;
  char *errMsg = NULL;
  bool rpc_sent = FALSE;


  GB_METAUPDATE_OR_GOTO(lock, args->glfs, dobj.block_name, args->volume,
                        ret, errMsg, out, "%s: CLEANUPINPROGRESS\n", args->addr);

  ret = glusterBlockCallRPC_1(args->addr, &dobj, DELETE_SRV, &rpc_sent,
                              &args->reply);
  if (ret) {
    saveret = ret;
    if (!rpc_sent) {
      GB_ASPRINTF(&errMsg, ": %s", strerror(errno));
      LOG("mgmt", GB_LOG_ERROR, "%s hence %s for block %s on "
          "host %s volume %s", strerror(errno), FAILED_REMOTE_DELETE,
          dobj.block_name, args->addr, args->volume);
      goto out;
    } else if (args->reply) {
      errMsg = args->reply;
      args->reply = NULL;
    }

    GB_METAUPDATE_OR_GOTO(lock, args->glfs, dobj.block_name, args->volume,
                          ret, errMsg, out, "%s: CLEANUPFAIL\n", args->addr);
    LOG("mgmt", GB_LOG_ERROR, "%s for block %s on host %s volume %s",
        FAILED_REMOTE_DELETE, dobj.block_name, args->addr, args->volume);

    ret = saveret;;
    goto out;
  }
  GB_METAUPDATE_OR_GOTO(lock, args->glfs, dobj.block_name, args->volume,
                        ret, errMsg, out, "%s: CLEANUPSUCCESS\n", args->addr);

 out:
  if (!args->reply) {
    if (GB_ASPRINTF(&args->reply, "failed to delete config on %s %s",
                    args->addr, errMsg?errMsg:"") == -1) {
      ret = ret?ret:-1;
    }
  }
  GB_FREE(errMsg);
  args->exit = ret;

  return NULL;
}


static size_t
glusterBlockDeleteFillArgs(MetaInfo *info, blockRemoteObj *args,
                           struct glfs *glfs, blockDelete *dobj)
{
  int i = 0;
  size_t count = 0;
  unsigned int status;

  for (i = 0, count = 0; i < info->nhosts; i++) {
    status = blockMetaStatusEnumParse(info->list[i]->status);
    if (status == GB_CONFIG_INPROGRESS || status == GB_CLEANUP_SUCCESS) {
      continue;
    }
    if (args) {
      args[count].glfs = glfs;
      args[count].obj = (void *)dobj;
      args[count].volume = info->volume;
      args[count].addr = info->list[i]->addr;
    }
    count++;
  }
  return count;
}


static int
glusterBlockDeleteRemoteAsync(char *blockname,
                              MetaInfo *info,
                              struct glfs *glfs,
                              blockDelete *dobj,
                              size_t count,
                              blockRemoteDeleteResp **savereply)
{
  pthread_t  *tid = NULL;
  blockRemoteDeleteResp *local = *savereply;
  blockRemoteObj *args = NULL;
  char *d_attempt = NULL;
  char *d_success = NULL;
  char *a_tmp = NULL;
  char *s_tmp = NULL;
  int ret = -1;
  size_t i;
  MetaInfo *info_new = NULL;
  int cleanupsuccess = 0;


  if (GB_ALLOC_N(tid, count) < 0  || GB_ALLOC_N(args, count) < 0) {
    goto out;
  }

  count = glusterBlockDeleteFillArgs(info, args, glfs, dobj);

  for (i = 0; i < count; i++) {
    pthread_create(&tid[i], NULL, glusterBlockDeleteRemote, &args[i]);
  }

  for (i = 0; i < count; i++) {
    pthread_join(tid[i], NULL);
  }

  ret = glusterBlockCollectAttemptSuccess(args, info, DELETE_SRV, count,
                                          &d_attempt, &d_success);
  if (ret) {
    goto out;
  }
  ret = -1;

  if (d_attempt) {
    a_tmp = local->d_attempt;
    if (GB_ASPRINTF(&local->d_attempt, "%s %s",
                    (a_tmp==NULL?"":a_tmp), d_attempt) == -1) {
      goto out;
    }
    GB_FREE(a_tmp);
    a_tmp = local->d_attempt;
  }

  if (d_success) {
    s_tmp = local->d_success;
    if (GB_ASPRINTF(&local->d_success, "%s %s",
                    (s_tmp==NULL?"":s_tmp), d_success) == -1) {
      goto out;
    }
    GB_FREE(s_tmp);
    s_tmp = local->d_success;
  }

  for (i = 0; i < count; i++) {
    if (args[i].exit && args[i].exit == GB_BLOCK_NOT_FOUND) {
      cleanupsuccess++;
    }
  }

  /* get new MetaInfo and compare */
  if (GB_ALLOC(info_new) < 0 || blockGetMetaInfo(glfs, blockname, info_new, NULL)) {
    goto out;
  }

  for (i = 0; i < info_new->nhosts; i++) {
    switch (blockMetaStatusEnumParse(info_new->list[i]->status)) {
      case GB_CONFIG_INPROGRESS:  /* un touched */
      case GB_CLEANUP_SUCCESS:
        cleanupsuccess++;
        break;
    }
  }

  if (cleanupsuccess == info->nhosts) {
    ret = 0;
  }
  *savereply = local;

 out:
  GB_FREE(d_attempt);
  GB_FREE(d_success);
  GB_FREE(args);
  GB_FREE(tid);
  GB_FREE(info_new);

  return ret;
}


static void
blockDeleteCliFormatResponse(blockDeleteCli *blk, int errCode, char *errMsg,
                             blockRemoteDeleteResp *savereply,
                             blockResponse *reply)
{
  json_object *json_obj = NULL;
  char *tmp = NULL;

  if (!reply) {
    return;
  }

  if (errCode < 0) {
    errCode = GB_DEFAULT_ERRCODE;
  }
  reply->exit = errCode;

  if (errMsg) {
    blockFormatErrorResponse(DELETE_SRV, blk->json_resp, errCode,
                             errMsg, reply);
    return;
  }

  if (blk->json_resp) {
    json_obj = json_object_new_object();

    blockStr2arrayAddToJsonObj (json_obj, savereply->d_attempt, "FAILED ON");
    blockStr2arrayAddToJsonObj (json_obj, savereply->d_success, "SUCCESSFUL ON");

    json_object_object_add(json_obj, "RESULT",
        errCode?GB_JSON_OBJ_TO_STR("FAIL"):GB_JSON_OBJ_TO_STR("SUCCESS"));

    GB_ASPRINTF(&reply->out, "%s\n",
                json_object_to_json_string_ext(json_obj,
                                     mapJsonFlagToJsonCstring(blk->json_resp)));

    json_object_put(json_obj);
  } else {
    /* save 'failed on'*/
    if (savereply->d_attempt) {
      if (GB_ASPRINTF(&tmp, "FAILED ON: %s\n", savereply->d_attempt) == -1)
        goto out;
    }

    if (GB_ASPRINTF(&reply->out,
          "%sSUCCESSFUL ON: %s\nRESULT: %s\n", tmp?tmp:"",
          savereply->d_success?savereply->d_success:"None",
          errCode?"FAIL":"SUCCESS") == -1) {
            goto out;
    }
  }
 out:
  /*catch all*/
  if (!reply->out) {
    blockFormatErrorResponse(DELETE_SRV, blk->json_resp, errCode,
                             GB_DEFAULT_ERRMSG, reply);
  }

  GB_FREE (tmp);
  return;
}


int
glusterBlockCleanUp(struct glfs *glfs, char *blockname,
                    bool forcedel, bool unlink, blockRemoteDeleteResp *drobj)
{
  int ret = -1;
  blockDelete dobj;
  size_t count = 0;
  MetaInfo *info = NULL;
  int asyncret = 0;
  char *errMsg = NULL;


  if (GB_ALLOC(info) < 0) {
    goto out;
  }

  ret = blockGetMetaInfo(glfs, blockname, info, NULL);
  if (ret) {
    goto out;
  }

  GB_STRCPYSTATIC(dobj.block_name, blockname);
  GB_STRCPYSTATIC(dobj.gbid, info->gbid);

  count = glusterBlockDeleteFillArgs(info, NULL, NULL, NULL);
  asyncret = glusterBlockDeleteRemoteAsync(blockname, info, glfs, &dobj, count,
                                           &drobj);
  if (asyncret) {
    LOG("mgmt", GB_LOG_WARNING,
        "glusterBlockDeleteRemoteAsync: return %d %s for block %s on volume %s",
        asyncret, FAILED_REMOTE_AYNC_DELETE, blockname, info->volume);
  }

  /* delete metafile and block file */
  if (forcedel || !asyncret) {
    GB_METAUPDATE_OR_GOTO(lock, glfs, blockname, info->volume,
                          ret, errMsg, out, "ENTRYDELETE: INPROGRESS\n");
    if (unlink && glusterBlockDeleteEntry(glfs, info->volume, info->gbid)) {
      GB_METAUPDATE_OR_GOTO(lock, glfs, blockname, info->volume,
                            ret, errMsg, out, "ENTRYDELETE: FAIL\n");
      LOG("mgmt", GB_LOG_ERROR, "%s %s for block %s", FAILED_DELETING_FILE,
          info->volume, blockname);
      ret = -1;
      goto out;
    }
    GB_METAUPDATE_OR_GOTO(lock, glfs, blockname, info->volume,
                          ret, errMsg, out, "ENTRYDELETE: SUCCESS\n");
    ret = glusterBlockDeleteMetaFile(glfs, info->volume, blockname);
    if (ret) {
      LOG("mgmt", GB_LOG_ERROR, "%s %s for block %s",
          FAILED_DELETING_META, info->volume, blockname);
      goto out;
    }
  }

 out:
  blockFreeMetaInfo(info);
  GB_FREE (errMsg);

  /* ignore asyncret if force delete is used */
  if (forcedel) {
    asyncret = 0;
  }

  return asyncret?asyncret:ret;
}


static blockResponse *
block_delete_cli_1_svc_st(blockDeleteCli *blk, struct svc_req *rqstp)
{
  blockRemoteDeleteResp *savereply = NULL;
  MetaInfo *info = NULL;
  blockResponse *reply = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  char *errMsg = NULL;
  int errCode = -1;
  int ret;
  blockServerDefPtr list = NULL;


  LOG("mgmt", GB_LOG_INFO, "delete cli request, volume=%s blockname=%s",
                           blk->volume, blk->block_name);

  if (GB_ALLOC(reply) < 0) {
    goto optfail;
  }

  if (GB_ALLOC(savereply) < 0) {
    GB_FREE(reply);
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

  if (GB_ALLOC(info) < 0) {
    errCode = ENOMEM;
    goto out;
  }

  ret = blockGetMetaInfo(glfs, blk->block_name, info, NULL);
  if (ret) {
    errCode = ret;
    goto out;
  }

  if (!blk->force) {
    list = glusterBlockGetListFromInfo(info);
    if (!list) {
      errCode = ENOMEM;
      goto out;
    }

    errCode = glusterBlockCheckCapabilities((void *)blk, DELETE_SRV, list, NULL, &errMsg);
    if (errCode) {
      LOG("mgmt", GB_LOG_ERROR,
          "glusterBlockCheckCapabilities() for block %s on volume %s failed",
          blk->block_name, blk->volume);
      goto out;
    }
  }

  errCode = glusterBlockCleanUp(glfs, blk->block_name, blk->force, blk->unlink, savereply);
  if (errCode) {
    LOG("mgmt", GB_LOG_WARNING, "glusterBlockCleanUp: return %d "
        "on block %s for volume %s", errCode, blk->block_name, blk->volume);
  } else if (info->prio_path[0]) {
    blockDecPrioAttr(glfs, blk->volume, info->prio_path);
  }

 out:
  GB_METAUNLOCK(lkfd, blk->volume, errCode, errMsg);
  blockServerDefFree(list);
  GB_FREE(info);

 optfail:
  LOG("mgmt", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO),
      "delete cli return %s, volume=%s blockname=%s",
      errCode ? "failure" : "success", blk->volume, blk->block_name);

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): for block %s on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->block_name, blk->volume, strerror(errno));
  }

  blockDeleteCliFormatResponse(blk, errCode, errMsg, savereply, reply);
  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s",
      reply ? reply->out : "*Nil*");

  if (savereply) {
    GB_FREE(savereply->d_attempt);
    GB_FREE(savereply->d_success);
    GB_FREE(savereply);
  }
  GB_FREE(errMsg);

  return reply;
}


static blockResponse *
block_delete_1_svc_st(blockDelete *blk, struct svc_req *rqstp)
{
  int ret;
  char *iqn = NULL;
  char *backstore = NULL;
  char *exec = NULL;
  blockResponse *reply = NULL;


  LOG("mgmt", GB_LOG_INFO,
      "delete request, blockname=%s filename=%s", blk->block_name, blk->gbid);

  if (GB_ALLOC(reply) < 0) {
    goto out;
  }
  reply->exit = -1;

  ret = blockCheckBlockLoadedStatus(blk->block_name, blk->gbid, reply);
  if (ret) {
    goto out;
  }

  if (GB_ASPRINTF(&iqn, "%s %s %s%s", GB_TGCLI_ISCSI_PATH, GB_DELETE,
                  GB_TGCLI_IQN_PREFIX, blk->gbid) == -1) {
    goto out;
  }

  if (GB_ASPRINTF(&backstore, "%s %s name=%s save=True", GB_TGCLI_GLFS_PATH,
                  GB_DELETE, blk->block_name) == -1) {
    goto out;
  }

  if (GB_ASPRINTF(&exec, "targetcli <<EOF\n%s\n%s\nexit\nEOF", backstore, iqn) == -1) {
    goto out;
  }

  if (GB_ALLOC_N(reply->out, 8192) < 0) {
    GB_FREE(reply);
    goto out;
  }

  GB_CMD_EXEC_AND_VALIDATE(exec, reply, blk, NULL, DELETE_SRV);
  if (reply->exit) {
    snprintf(reply->out, 8192, "delete failed");
  }

 out:
  GB_FREE(exec);
  GB_FREE(backstore);
  GB_FREE(iqn);

  return reply;
}

bool_t
block_delete_1_svc(blockDelete *blk, blockResponse *reply, struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(delete, blk, reply, rqstp, ret);
  return ret;
}


bool_t
block_delete_cli_1_svc(blockDeleteCli *blk, blockResponse *reply,
                       struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(delete_cli, blk, reply, rqstp, ret);
  return ret;
}
