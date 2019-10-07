/*
  Copyright (c) 2019 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "block_common.h"

# define   GB_RELOAD            "restoreconfig " GB_SAVECONFIG " clear_existing"


void *
glusterBlockReloadRemote(void *data)
{
  int ret;
  int saveret;
  blockRemoteObj *args = (blockRemoteObj *)data;
  blockReload robj = *(blockReload *)args->obj;
  char *errMsg = NULL;
  bool rpc_sent = FALSE;


  ret = glusterBlockCallRPC_1(args->addr, &robj, RELOAD_SRV, &rpc_sent,
                              &args->reply);
  if (ret) {
    saveret = ret;
    if (!rpc_sent) {
      GB_ASPRINTF(&errMsg, ": %s", strerror(errno));
      LOG("mgmt", GB_LOG_ERROR, "%s hence %s for block %s on "
          "host %s volume %s", strerror(errno), FAILED_REMOTE_RELOAD,
          robj.block_name, args->addr, args->volume);
      goto out;
    } else if (args->reply) {
      errMsg = args->reply;
      args->reply = NULL;
    }

    LOG("mgmt", GB_LOG_ERROR, "%s for block %s on host %s volume %s",
        FAILED_REMOTE_RELOAD, robj.block_name, args->addr, args->volume);

    ret = saveret;;
    goto out;
  }

 out:
  if (!args->reply) {
    if (GB_ASPRINTF(&args->reply, "failed to reload config on %s %s",
                    args->addr, errMsg?errMsg:"") == -1) {
      ret = ret?ret:-1;
    }
  }
  GB_FREE(errMsg);
  args->exit = ret;

  return NULL;
}


static size_t
glusterBlockReloadFillArgs(MetaInfo *info, blockRemoteObj *args,
                           struct glfs *glfs, blockReload *robj)
{
  int i = 0;
  size_t count = 0;

  for (i = 0, count = 0; i < info->nhosts; i++) {
    if (blockhostIsValid(info->list[i]->status)) {
      if (args) {
        args[count].glfs = glfs;
        args[count].obj = (void *)robj;
        args[count].volume = info->volume;
        args[count].addr = info->list[i]->addr;
      }
      count++;
    }
  }
  return count;
}


static int
glusterBlockReloadRemoteAsync(MetaInfo *info,
                              struct glfs *glfs,
                              blockReload *robj,
                              bool force,
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
  size_t count;


  count = glusterBlockReloadFillArgs(info, NULL, NULL, NULL);
  if (GB_ALLOC_N(tid, count) < 0  || GB_ALLOC_N(args, count) < 0) {
    goto out;
  }

  count = glusterBlockReloadFillArgs(info, args, glfs, robj);

  for (i = 0; i < count; i++) {
    pthread_create(&tid[i], NULL, glusterBlockReloadRemote, &args[i]);
  }

  for (i = 0; i < count; i++) {
    pthread_join(tid[i], NULL);
  }

  ret = glusterBlockCollectAttemptSuccess(args, info, RELOAD_SRV, count,
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


  /* Ignore failures with force option */
  if (force) {
    ret = 0;
    goto out;
  }

  ret = 0;
  for (i = 0; i < count; i++) {
    if (args[i].exit) {
      ret = -1;
      break;
    }
  }

 out:
  GB_FREE(d_attempt);
  GB_FREE(d_success);
  GB_FREE(args);
  GB_FREE(tid);

  return ret;
}


static void
blockReloadCliFormatResponse(blockReloadCli *blk, int errCode, char *errMsg,
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
    blockFormatErrorResponse(RELOAD_SRV, blk->json_resp, errCode,
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
    blockFormatErrorResponse(RELOAD_SRV, blk->json_resp, errCode,
                             GB_DEFAULT_ERRMSG, reply);
  }

  GB_FREE (tmp);
  return;
}


static blockResponse *
block_reload_cli_1_svc_st(blockReloadCli *blk, struct svc_req *rqstp)
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
  blockReload robj = {{0},};

  LOG("mgmt", GB_LOG_INFO, "reload cli request, volume=%s blockname=%s",
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

  list = glusterBlockGetListFromInfo(info);
  if (!list) {
    errCode = ENOMEM;
    goto out;
  }

  if (!blk->force) {
    errCode = glusterBlockCheckCapabilities((void *)blk, RELOAD_SRV, list, NULL, &errMsg);
    if (errCode) {
      LOG("mgmt", GB_LOG_ERROR,
          "glusterBlockCheckCapabilities() for block %s on volume %s failed",
          blk->block_name, blk->volume);
      goto out;
    }
  }

  GB_STRCPYSTATIC(robj.block_name, blk->block_name);
  GB_STRCPYSTATIC(robj.gbid, info->gbid);

  errCode = glusterBlockReloadRemoteAsync(info, glfs, &robj, blk->force, &savereply);
  if (errCode) {
    LOG("mgmt", GB_LOG_WARNING, "glusterBlockReloadRemoteAsync: return %d "
        "on block %s for volume %s", errCode, blk->block_name, blk->volume);
  }

 out:
  GB_METAUNLOCK(lkfd, blk->volume, errCode, errMsg);
  blockFreeMetaInfo(info);
  blockServerDefFree(list);

 optfail:
  LOG("mgmt", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO),
      "reload cli return %s, volume=%s blockname=%s",
      errCode ? "failure" : "success", blk->volume, blk->block_name);

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): for block %s on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->block_name, blk->volume, strerror(errno));
  }

  blockReloadCliFormatResponse(blk, errCode, errMsg, savereply, reply);
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
block_reload_1_svc_st(blockReload *blk, struct svc_req *rqstp)
{
  char *exec = NULL;
  blockResponse *reply = NULL;


  LOG("mgmt", GB_LOG_INFO,
      "reload request, blockname=%s filename=%s", blk->block_name, blk->gbid);

  if (GB_ALLOC(reply) < 0) {
    goto out;
  }
  reply->exit = -1;

  if (GB_ASPRINTF(&exec, "targetcli %s target=%s%s storage_object=%s", GB_RELOAD,
                  GB_TGCLI_IQN_PREFIX, blk->gbid, blk->block_name) == -1) {
    goto out;
  }

  if (GB_ALLOC_N(reply->out, 8192) < 0) {
    GB_FREE(reply);
    goto out;
  }

  /* currently targetcli throw few error msgs for keywords in saveconfig.json
   * which are unknown and also return few warning msgs and error code, hence
   * not using GB_CMD_EXEC_AND_VALIDATE() macro and not caring for return
   * value for now. For now lets just run the restoreconfig command and use
   * blockCheckBlockLoadedStatus() to check we get success.
   */
  gbRunner(exec);

  if (blockCheckBlockLoadedStatus(blk->block_name, blk->gbid, reply)) {
    goto out;
  }
  snprintf(reply->out, 8192, "reload success");
  reply->exit = 0;

 out:
  GB_FREE(exec);

  return reply;
}

bool_t
block_reload_1_svc(blockReload *blk, blockResponse *reply, struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(reload, blk, reply, rqstp, ret);
  return ret;
}


bool_t
block_reload_cli_1_svc(blockReloadCli *blk, blockResponse *reply,
                       struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(reload_cli, blk, reply, rqstp, ret);
  return ret;
}
