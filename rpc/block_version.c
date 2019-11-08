/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "block_common.h"


static bool *
glusterBlockBuildMinCaps(void *data, operations opt)
{
  blockCreateCli *cblk = NULL;
  blockDeleteCli *dblk = NULL;
  blockModifyCli *mblk = NULL;
  blockModifySizeCli *msblk = NULL;
  blockReplaceCli *rblk = NULL;
  blockReloadCli *rlblk = NULL;
  bool *minCaps = NULL;


  if (GB_ALLOC_N(minCaps, GB_CAP_MAX) < 0) {
    return NULL;
  }

  switch (opt) {
  case CREATE_SRV:
    cblk = (blockCreateCli *)data;

    minCaps[GB_CREATE_CAP] = true;
    if (cblk->mpath > 1) {
      minCaps[GB_CREATE_HA_CAP] = true;
    }
    if (cblk->prealloc) {
      minCaps[GB_CREATE_PREALLOC_CAP] = true;
    }
    if (cblk->auth_mode) {
      minCaps[GB_CREATE_AUTH_CAP] = true;
    }
    if (cblk->rb_size) {
      minCaps[GB_CREATE_RING_BUFFER_CAP] = true;
    }
    if (cblk->blk_size) {
      minCaps[GB_CREATE_BLOCK_SIZE_CAP] = true;
    }
    minCaps[GB_CREATE_LOAD_BALANCE_CAP] = true;
    if (cblk->json_resp) {
      minCaps[GB_JSON_CAP] = true;
    }
    if (cblk->io_timeout) {
      minCaps[GB_CREATE_IO_TIMEOUT_CAP] = true;
    }
    break;
  case DELETE_SRV:
    dblk = (blockDeleteCli *)data;

    minCaps[GB_DELETE_CAP] = true;
    if (dblk->force) {
      minCaps[GB_DELETE_FORCE_CAP] = true;
    }
    if (dblk->json_resp) {
      minCaps[GB_JSON_CAP] = true;
    }
    break;
  case MODIFY_SRV:
    mblk = (blockModifyCli *)data;

    minCaps[GB_MODIFY_CAP] = true;
    if (mblk->auth_mode) {
      minCaps[GB_MODIFY_AUTH_CAP] = true;
    }
    if (mblk->json_resp) {
      minCaps[GB_JSON_CAP] = true;
    }
    break;
  case MODIFY_SIZE_SRV:
    msblk = (blockModifySizeCli *)data;
    minCaps[GB_MODIFY_SIZE_CAP] = true;
    if (msblk->json_resp) {
      minCaps[GB_JSON_CAP] = true;
    }
    break;
  case REPLACE_SRV:
    rblk = (blockReplaceCli *)data;

    minCaps[GB_REPLACE_CAP] = true;
    if (rblk->json_resp) {
      minCaps[GB_JSON_CAP] = true;
    }
    break;
  case RELOAD_SRV:
    rlblk = (blockReloadCli *)data;

    minCaps[GB_RELOAD_CAP] = true;
    if (rlblk->json_resp) {
      minCaps[GB_JSON_CAP] = true;
    }
    break;
  case MODIFY_TPGC_SRV:
  case REPLACE_GET_PORTAL_TPG_SRV:
  case LIST_SRV:
  case INFO_SRV:
  case VERSION_SRV:
  case GENCONFIG_SRV:
    break;
  }

  return minCaps;
}

static void *
glusterBlockCapabilitiesRemote(void *data)
{
  int ret;
  blockRemoteObj *args = (blockRemoteObj *)data;
  bool rpc_sent = FALSE;


  /* Get peers capabilities */
  ret = glusterBlockCallRPC_1(args->addr, NULL, VERSION_SRV, &rpc_sent,
                              &args->reply);
  if (ret && ret != RPC_PROCUNAVAIL) {
    if (!rpc_sent) {
      LOG("mgmt", GB_LOG_ERROR, "%s hence %s on host %s",
          strerror(errno), FAILED_REMOTE_CAPS, args->addr);
      ret = -ENOTCONN;
    } else {
      LOG("mgmt", GB_LOG_ERROR, "%s for on host %s",
          FAILED_REMOTE_CAPS, args->addr);
    }
  }

  args->exit = ret;

  return NULL;
}


/* function to imitate caps of older gluster-block versions */
static gbCapResp *
glusterBlockMimicOldCaps(void)
{
  size_t i;
  gbCapResp *caps = NULL;


  if (GB_ALLOC(caps) < 0) {
    return NULL;
  }

  caps->capMax = GB_OLD_CAP_MAX;
  if (GB_ALLOC_N(caps->response, GB_OLD_CAP_MAX) < 0) {
    GB_FREE(caps);
    return NULL;
  }

  GB_STRCPYSTATIC(caps->response[0].cap, "create");
  GB_STRCPYSTATIC(caps->response[1].cap, "create_ha");
  GB_STRCPYSTATIC(caps->response[2].cap, "create_prealloc");
  GB_STRCPYSTATIC(caps->response[3].cap, "create_auth");

  GB_STRCPYSTATIC(caps->response[4].cap, "delete");
  GB_STRCPYSTATIC(caps->response[5].cap, "delete_force");

  GB_STRCPYSTATIC(caps->response[6].cap, "modify");
  GB_STRCPYSTATIC(caps->response[7].cap, "modify_auth");

  GB_STRCPYSTATIC(caps->response[8].cap, "json");

  for (i = 0; i < GB_OLD_CAP_MAX; i++) {
    caps->response[i].status = true;
  }

  return caps;
}


static int
blockRemoteCapabilitiesRespParse(size_t count, blockRemoteObj *args,
                                 bool *minCaps, bool *resultCaps, char **errMsg)
{
  size_t i, j, k;
  int ret = -1;
  gbCapResp **caps = NULL;
  bool CAP_MATCH;


  if (GB_ALLOC_N(caps, count) < 0) {
    return -1;
  }
  for (i = 0; i < count; i++) {
      caps[i] = (gbCapResp *) args[i].reply;
  }

  for (i = 0; i < count; i++) {
    if (args[i].exit == RPC_PROCUNAVAIL) {
      caps[i] = glusterBlockMimicOldCaps();
    } else if (args[i].exit) {
      ret = args[i].exit;
      GB_ASPRINTF(errMsg, "host %s returned %d", args[i].addr, ret);
      goto out;
    }
  }

  for (i = 0; i < GB_CAP_MAX; i++) {
    if (minCaps[i] == false) {
      continue;
    }
    /* Check if all remotes contain this cap */
    for (j = 0; j < count; j++) {
      CAP_MATCH=false;
      if (!caps[j]) {
        GB_ASPRINTF(errMsg, "capability empty on %s", args[j].addr);
        goto out;
      }
      for (k = 0; k < caps[j]->capMax; k++) {
        if (!strcmp(gbCapabilitiesLookup[i], caps[j]->response[k].cap) &&
            caps[j]->response[k].status) {
            CAP_MATCH=true;
            break;
        }
      }

      if (!CAP_MATCH) {
        GB_ASPRINTF(errMsg, "the capability '%s' is not supported on host %s yet, %s.",
                    gbCapabilitiesLookup[i], args[j].addr,
                    i == GB_CREATE_BLOCK_SIZE_CAP ? "please upgrade rtslib >= 2.1.70" : "please upgrade");
        if (resultCaps) {
          resultCaps[i] = true;
          break;
        } else {
          goto out;
        }
      }
    }
  }

  if (resultCaps) {
    for (i = 0; i < GB_CAP_MAX; i++) {
      if (resultCaps[i]) {
        goto out;
      }
    }
  }
  ret = 0;
 out:
  for (i = 0; i < count; i++) {
    if (caps[i]) {
      GB_FREE(caps[i]->response);
      GB_FREE(caps[i]);
    }
  }
  GB_FREE(caps);
  return ret;
}


static int
glusterBlockCapabilityRemoteAsync(blockServerDef *servers, bool *minCaps,
                                  bool *resultCaps, char **errMsg)
{
  blockRemoteObj *args = NULL;
  pthread_t  *tid = NULL;
  int ret = -1;
  size_t i;


  if (GB_ALLOC_N(tid, servers->nhosts) < 0) {
    goto out;
  }

  if (GB_ALLOC_N(args, servers->nhosts) < 0) {
    goto out;
  }

  for (i = 0; i < servers->nhosts; i++) {
    args[i].addr = servers->hosts[i];
  }

  for (i = 0; i < servers->nhosts; i++) {
    pthread_create(&tid[i], NULL, glusterBlockCapabilitiesRemote, &args[i]);
  }

  for (i = 0; i < servers->nhosts; i++) {
    /* collect exit code */
    pthread_join(tid[i], NULL);
  }

  /* Verify the capabilities */
  ret = blockRemoteCapabilitiesRespParse(servers->nhosts, args, minCaps, resultCaps, errMsg);

 out:
  GB_FREE(args);
  GB_FREE(tid);

  return ret;
}



int
glusterBlockCheckCapabilities(void* blk, operations opt, blockServerDefPtr list,
                              bool *resultCaps, char **errMsg)
{
  int errCode = 0;
  bool *minCaps = NULL;
  char *localErrMsg = NULL;


  if (!list) {
    return 0;
  }

  minCaps = glusterBlockBuildMinCaps(blk, opt);
  if (!minCaps) {
    errCode = GB_DEFAULT_ERRCODE;
    goto out;
  }

  errCode = glusterBlockCapabilityRemoteAsync(list, minCaps, resultCaps, &localErrMsg);
  if (errCode) {
    LOG("mgmt", GB_LOG_WARNING, "glusterBlockCapabilityRemoteAsync() failed (%s)",
                                localErrMsg);
    if (errCode == -ENOTCONN) {
      if (GB_ASPRINTF(errMsg, "Version check failed [%s] (Hint: See if all "
                      "servers are up and running gluster-blockd daemon)",
                      localErrMsg) == -1) {
        errCode = ENOMEM;
      }
    } else {
      if (GB_ASPRINTF(errMsg, "Version check failed between block servers. (%s)",
                      localErrMsg) == -1) {
        errCode = ENOMEM;
      }
    }
    goto out;
  }

 out:
  GB_FREE(minCaps);
  GB_FREE(localErrMsg);
  return errCode;
}

static blockResponse *
block_version_1_svc_st(void *data, struct svc_req *rqstp)
{
  blockResponse *reply = NULL;
  gbCapObj *caps;
  int i;


  LOG("mgmt", GB_LOG_DEBUG, "version check request");

  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }

  if (GB_ALLOC_N(caps, GB_CAP_MAX) < 0) {
    GB_FREE(reply);
    return NULL;
  }

  for (i = 0; i < GB_CAP_MAX; i++) {
    GB_STRCPYSTATIC(caps[i].cap, globalCapabilities[i].cap);
    caps[i].status = globalCapabilities[i].status;
  }

  reply->xdata.xdata_len = GB_CAP_MAX * sizeof(gbCapObj);
  reply->xdata.xdata_val = (char *) caps;
  reply->exit = 0;

  GB_ASPRINTF(&reply->out, "version check routine");

  return reply;
}


bool_t
block_version_1_svc(void *data, blockResponse *reply, struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(version, data, reply, rqstp, ret);
  return ret;
}
