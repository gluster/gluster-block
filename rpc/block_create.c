/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "block_common.h"

# define   GB_CREATE              "create"
# define   GB_MSERVER_DELIMITER   ","
# define   GB_SAVECFG_NAME_CHECK  "grep -csh '\"name\": \"%s\",' " GB_SAVECONFIG " " GB_SAVECONFIG_TEMP


static char *
getLastWordNoDot(char *line)
{
  char *tptr = line;
  char *sptr = NULL;


  if (!tptr) {
    return NULL;
  }

  /* get last word i.e last space char in the line + 1 */
  while(*tptr) {
    if (*tptr == ' ') {
      sptr = tptr + 1;
    }
    tptr++;
  }

  /* remove last char i.e. dot, if exist*/
  if (sptr && *(sptr + strlen(sptr) - 1) == '.') {
    *(sptr + strlen(sptr) - 1) = '\0';
  }

  return sptr;
}


static void
removeDuplicateSubstr(char **line)
{
  char *temp = *line;
  char *out;
  char *element;
  size_t len;


  if (!temp) {
    return;
  }

  /* Allocate size for out including trailing space and \0. */
  len = strlen(temp) + strlen(" ") + 1;
  if (GB_ALLOC_N(out, len) < 0) {
    return;
  }

  /* Split string into tokens */
  element = strtok(temp, " ");
  while (element) {
    if (!strstr(out, element)) {
      GB_STRCAT(out, element, len);
      GB_STRCAT(out, " ", 2);
    }
    element = strtok(NULL, " ");
  }

  GB_FREE(*line);
  *line = out;
}


static blockServerDefPtr
blockServerParse(char *blkServers)
{
  blockServerDefPtr list;
  char *tmp;
  char *base;
  size_t i = 0;

  if (!blkServers) {
    return NULL;
  }

  if (GB_STRDUP(tmp, blkServers) < 0) {
    return NULL;
  }
  base = tmp;

  if (GB_ALLOC(list) < 0) {
    goto out;
  }

  /* count number of servers */
  while (*tmp) {
    if (*tmp == ',') {
      list->nhosts++;
    }
    tmp++;
  }
  list->nhosts++;
  tmp = base; /* reset addr */


  if (GB_ALLOC_N(list->hosts, list->nhosts) < 0) {
    goto out;
  }

  for (i = 0; tmp != NULL; i++) {
    if (GB_STRDUP(list->hosts[i], strsep(&tmp, GB_MSERVER_DELIMITER)) < 0) {
      goto out;
    }
  }

  GB_FREE(base);
  return list;

 out:
  GB_FREE(base);
  blockServerDefFree(list);
  return NULL;
}

static void
blockCreateParsedRespFree(blockRemoteCreateResp *savereply)
{
  size_t i;


  if (!savereply) {
    return;
  }

  GB_FREE(savereply->backend_size);
  GB_FREE(savereply->iqn);
  GB_FREE(savereply->errMsg);

  for (i = 0; i < savereply->nportal; i++) {
    GB_FREE(savereply->portal[i]);
  }
  GB_FREE(savereply->portal);

  if (savereply->obj) {
    GB_FREE(savereply->obj->d_attempt);
    GB_FREE(savereply->obj->d_success);
    GB_FREE(savereply->obj);
  }

  GB_FREE(savereply);
}


static int
blockRemoteCreateRespParse(char *output, blockRemoteCreateResp **savereply)
{
  char *line;
  blockRemoteCreateResp *local = *savereply;
  char *portal = NULL;
  char *errMsg = NULL;
  size_t i;
  bool dup;


  if (!local) {
    if (GB_ALLOC(local) < 0) {
      goto out;
    }
    if (GB_ALLOC(local->obj) < 0) {
      GB_FREE(local);
      goto out;
    }
  }

  /* get the first line */
  line = strtok(output, "\n");
  while (line)
  {
    switch (blockRemoteCreateRespEnumParse(line)) {
    case GB_BACKEND_RESP:
      if (!local->backend_size) {
        if (GB_STRDUP(local->backend_size, getLastWordNoDot(line)) < 0) {
          goto out;
        }
      }
      break;
    case GB_IQN_RESP:
      if (!local->iqn) {
        if (GB_STRDUP(local->iqn, getLastWordNoDot(line)) < 0) {
          goto out;
        }
      }
      break;
    case GB_TPG_NO_RESP:
    case GB_LUN_NO_RESP:
    case GB_IP_PORT_RESP:
      /* Not needed as reponse doesn't need them now. */
      break;
    case GB_PORTAL_RESP:
      portal = getLastWordNoDot(line);
      if (!local->nportal) {
        if (GB_ALLOC(local->portal) < 0) {
          goto out;
        }
        if (GB_STRDUP(local->portal[local->nportal], portal) < 0) {
          goto out;
        }
        local->nportal++;
      } else {
        dup = false;
        for (i = 0; i < local->nportal; i++) {
          if (strstr(local->portal[i], portal)){
            /* portal already feeded */
            dup = true;
            break;
          }
        }
        if (dup) {
          break;
        }
        if (GB_REALLOC_N(local->portal, local->nportal + 1) < 0) {
          goto out;
        }
        if (GB_STRDUP(local->portal[local->nportal], portal) < 0) {
          goto out;
        }
        local->nportal++;
      }
      break;
    case GB_FAILED_RESP:
      errMsg = local->errMsg;
      if (errMsg == NULL) {
        GB_ASPRINTF(&local->errMsg, "%s", line);
      } else {
        GB_ASPRINTF(&local->errMsg, "%s\n%s", errMsg, line);
      }
      GB_FREE (errMsg);
      break;
    }

    line = strtok(NULL, "\n");
  }

  *savereply = local;

  return 0;

 out:
  *savereply = local;

  return -1;
}


static int
glusterBlockAuditRequest(struct glfs *glfs,
                         blockCreateCli *blk,
                         blockCreate2 *cobj,
                         blockServerDefPtr list,
                         blockRemoteCreateResp **reply)
{
  int ret = -1;
  size_t i;
  size_t successcnt = 0;
  MetaInfo *info;


  if (GB_ALLOC(info) < 0) {
    goto out;
  }

  ret = blockGetMetaInfo(glfs, blk->block_name, info, NULL);
  if (ret) {
    goto out;
  }

  for (i = 0; i < info->nhosts; i++) {
    switch (blockMetaStatusEnumParse(info->list[i]->status)) {
    case GB_CONFIG_SUCCESS:
    case GB_AUTH_ENFORCED:
      successcnt++;
      break;
    }
  }

  /* check if mpath is satisfied */
  if (blk->mpath == successcnt) {
    LOG("mgmt", GB_LOG_INFO, "Block create request satisfied for target:"
        " %s on volume %s with given hosts %s",
          blk->block_name, blk->volume, blk->block_hosts);
    blockFreeMetaInfo(info);
    return 0;
  }

 out:
  glusterBlockCleanUp(glfs, blk->block_name, FALSE, TRUE, (*reply)->obj);

  blockFreeMetaInfo(info);
  return ret;
}


void *
glusterBlockCreateRemote(void *data)
{
  int ret;
  int saveret;
  blockRemoteObj *args = (blockRemoteObj *)data;
  blockCreate2 cobj = *(blockCreate2 *)args->obj;
  char *errMsg = NULL;
  bool rpc_sent = FALSE;


  GB_METAUPDATE_OR_GOTO(lock, args->glfs, cobj.block_name, cobj.volume,
                        ret, errMsg, out, "%s: CONFIGINPROGRESS\n", args->addr);

  ret = glusterBlockCallRPC_1(args->addr, &cobj, CREATE_SRV, &rpc_sent,
                              &args->reply);
  if (ret) {
    saveret = ret;
    if (!rpc_sent) {
      GB_ASPRINTF(&errMsg, ": %s", strerror(errno));
      LOG("mgmt", GB_LOG_ERROR, "%s hence %s for block %s on "
          "host %s volume %s", strerror(errno), FAILED_REMOTE_CREATE,
          cobj.block_name, args->addr, args->volume);
      goto out;
    } else if (args->reply) {
      errMsg = args->reply;
      args->reply = NULL;
    }

    GB_METAUPDATE_OR_GOTO(lock, args->glfs, cobj.block_name, cobj.volume,
                          ret, errMsg, out, "%s: CONFIGFAIL\n", args->addr);
    LOG("mgmt", GB_LOG_ERROR, "%s for block %s on host %s volume %s",
        FAILED_REMOTE_CREATE, cobj.block_name, args->addr, args->volume);

    ret = saveret;
    goto out;
  }

  GB_METAUPDATE_OR_GOTO(lock, args->glfs, cobj.block_name, cobj.volume,
                        ret, errMsg, out, "%s: CONFIGSUCCESS\n", args->addr);
  if (cobj.auth_mode) {
    GB_METAUPDATE_OR_GOTO(lock, args->glfs, cobj.block_name, cobj.volume,
                          ret, errMsg, out, "%s: AUTHENFORCED\n", args->addr);
  }

 out:
  if (!args->reply) {
    if (GB_ASPRINTF(&args->reply, "failed to configure on %s %s\n", args->addr,
                    errMsg?errMsg:"") == -1) {
      ret = ret?ret:-1;
    }
  }
  args->exit = ret;

  GB_FREE (errMsg);
  return NULL;
}


static int
glusterBlockCreateRemoteAsync(blockServerDefPtr list, size_t mpath,
                            struct glfs *glfs, blockCreate2 *cobj,
                            blockRemoteCreateResp **savereply)
{
  pthread_t  *tid = NULL;
  blockRemoteObj *args = NULL;
  int ret = -1;
  size_t i;


  if (GB_ALLOC_N(tid, mpath) < 0 || GB_ALLOC_N(args, mpath) < 0) {
    goto out;
  }

  for (i = 0; i < mpath; i++) {
    args[i].glfs = glfs;
    args[i].obj = (void *)cobj;
    args[i].volume = cobj->volume;
    args[i].addr = list->hosts[i];
  }

  for (i = 0; i < mpath; i++) {
    pthread_create(&tid[i], NULL, glusterBlockCreateRemote, &args[i]);
  }

  for (i = 0; i < mpath; i++) {
    /* collect exit code */
    pthread_join(tid[i], NULL);
  }

  for (i = 0; i < mpath; i++) {
    /* TODO: use glusterBlockCollectAttemptSuccess */
    ret = blockRemoteCreateRespParse(args[i].reply, savereply);
    if (ret) {
      goto out;
    }
  }

  ret = 0;
  for (i = 0; i < mpath; i++) {
    if (args[i].exit) {
      ret = -1;
      break;
    }
  }

 out:
  for (i = 0; i < mpath; i++) {
    if (args) {
      GB_FREE(args[i].reply);
    }
  }
  GB_FREE(args);
  GB_FREE(tid);

  return ret;
}


static blockResponse *
block_create_common(blockCreate *blk, char *control, char *volServer,
                    char *prio_path, size_t io_timeout)
{
  char *tmp = NULL;
  char *backstore = NULL;
  char *io_timeout_str = NULL;
  char *backstore_attr = NULL;
  char *iqn = NULL;
  char *tpg = NULL;
  char *glfs_alua = NULL;
  char *glfs_alua_type = NULL;
  char *glfs_alua_sup = NULL;
  char *lun = NULL;
  char *lun0 = NULL;
  char *portal = NULL;
  char *attr = NULL;
  char *authcred = NULL;
  char *save = NULL;
  char *exec = NULL;
  blockResponse *reply = NULL;
  blockServerDefPtr list = NULL;
  size_t i;
  bool prioCap = false;


  LOG("mgmt", GB_LOG_INFO,
      "create request, volume=%s volserver=%s blockname=%s blockhosts=%s "
      "filename=%s authmode=%d passwd=%s size=%lu control=%s "
      "io_timeout=%lu", blk->volume, volServer?volServer:blk->ipaddr,
      blk->block_name, blk->block_hosts, blk->gbid, blk->auth_mode,
      blk->auth_mode?blk->passwd:"", blk->size, control?control:"", io_timeout);

  if (GB_ALLOC(reply) < 0) {
    goto out;
  }
  reply->exit = -1;

  if (GB_ALLOC_N(reply->out, 8192) < 0) {
    goto out;
  }

  if (GB_ASPRINTF(&exec, GB_SAVECFG_NAME_CHECK, blk->block_name) == -1) {
    goto out;
  }

  save = gbRunnerGetOutput(exec);
  if (save && atol(save)) {
    snprintf(reply->out, 8192,
            "block with name '%s' already exist (Hint: may be hosted on a different block-hosting volume)",
             blk->block_name);
    goto out;
  }
  GB_FREE(exec);
  GB_FREE(save);

  if (prio_path && prio_path[0]) {
    prioCap = true;
  }

  if (!io_timeout && globalCapabilities[GB_CREATE_IO_TIMEOUT_CAP].status) {
    io_timeout = GB_IO_TIMEOUT_DEF;
  }

  if (io_timeout) {
    if (GB_ASPRINTF(&io_timeout_str, ";tcmur_cmd_time_out=%lu",
                    io_timeout) == -1) {
      goto out;
    }
  }

  if (GB_ASPRINTF(&backstore, "%s %s name=%s size=%zu cfgstring=%s@%s%s/%s%s%s wwn=%s",
                  GB_TGCLI_GLFS_PATH, GB_CREATE, blk->block_name, blk->size,
                  blk->volume, volServer?volServer:blk->ipaddr, GB_STOREDIR,
                  blk->gbid, io_timeout_str ? io_timeout_str : "",
                  control ? control : "", blk->gbid) == -1) {
    goto out;
  }

  if (GB_ASPRINTF(&backstore_attr,
                  "%s/%s set attribute cmd_time_out=%d",
                  GB_TGCLI_GLFS_PATH, blk->block_name, GB_CMD_TIME_OUT) == -1) {
    goto out;
  }

  if (prioCap) {
    if (GB_ASPRINTF(&glfs_alua,
                    "%s/%s/alua create name=glfs_tg_pt_gp_ao tag=1\n"
                    "%s/%s/alua create name=glfs_tg_pt_gp_ano tag=2",
                    GB_TGCLI_GLFS_PATH, blk->block_name,
                    GB_TGCLI_GLFS_PATH, blk->block_name) == -1) {
      goto out;
    }

    /*
     * Only the 1/"implicit" type is support. Set AO TPG's ALUA state
     * to 0(Active/optimized state) and set the ANO TPG's ALUA state
     * to 1(Active/non-optimized state).
     */
    if (GB_ASPRINTF(&glfs_alua_type,
                    "%s/%s/alua/glfs_tg_pt_gp_ao set alua alua_access_type=1\n"
                    "%s/%s/alua/glfs_tg_pt_gp_ano set alua alua_access_type=1\n"
                    "%s/%s/alua/glfs_tg_pt_gp_ao set alua alua_access_state=0\n"
                    "%s/%s/alua/glfs_tg_pt_gp_ano set alua alua_access_state=1",
                    GB_TGCLI_GLFS_PATH, blk->block_name,
                    GB_TGCLI_GLFS_PATH, blk->block_name,
                    GB_TGCLI_GLFS_PATH, blk->block_name,
                    GB_TGCLI_GLFS_PATH, blk->block_name) == -1) {
      goto out;
    }

    /*
     * The ALUA states and transition:
     *
     * a) active/optimized: supported as default
     * b) active/non-optimized: supported as default
     * c) standby: non-supported
     * d) unavailable: non-supported
     * e) offline: non-supported
     * f) transition: supported as default
     */
    if (GB_ASPRINTF(&glfs_alua_sup,
                    "%s/%s/alua/glfs_tg_pt_gp_ao set alua alua_support_offline=0\n"
                    "%s/%s/alua/glfs_tg_pt_gp_ao set alua alua_support_standby=0\n"
                    "%s/%s/alua/glfs_tg_pt_gp_ao set alua alua_support_unavailable=0\n"
                    "%s/%s/alua/glfs_tg_pt_gp_ano set alua alua_support_offline=0\n"
                    "%s/%s/alua/glfs_tg_pt_gp_ano set alua alua_support_standby=0\n"
                    "%s/%s/alua/glfs_tg_pt_gp_ano set alua alua_support_unavailable=0\n",
                    GB_TGCLI_GLFS_PATH, blk->block_name,
                    GB_TGCLI_GLFS_PATH, blk->block_name,
                    GB_TGCLI_GLFS_PATH, blk->block_name,
                    GB_TGCLI_GLFS_PATH, blk->block_name,
                    GB_TGCLI_GLFS_PATH, blk->block_name,
                    GB_TGCLI_GLFS_PATH, blk->block_name) == -1) {
      goto out;
    }
  }

  if (GB_ASPRINTF(&iqn, "%s %s %s%s", GB_TGCLI_ISCSI_PATH, GB_CREATE,
                  GB_TGCLI_IQN_PREFIX, blk->gbid) == -1) {
    goto out;
  }

  list = blockServerParse(blk->block_hosts);
  if (!list) {
    goto out;
  }

  /* i = 2; because tpg1 is created by default while iqn create */
  for (i = 2; i <= list->nhosts; i++) {
    if (!tmp) {
      if (GB_ASPRINTF(&tpg, "%s/%s%s create tpg%zu\n",
                   GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i) == -1) {
        goto out;
      }
      tmp = tpg;
    } else {
      if (GB_ASPRINTF(&tpg, "%s %s/%s%s create tpg%zu\n", tmp,
                   GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i) == -1) {
        goto out;
      }
      GB_FREE(tmp);
      tmp = tpg;
    }
  }
  tmp = NULL;

  for (i = 1; i <= list->nhosts; i++) {
    if (GB_ASPRINTF(&lun, "%s/%s%s/tpg%zu/luns %s %s/%s",  GB_TGCLI_ISCSI_PATH,
                 GB_TGCLI_IQN_PREFIX, blk->gbid, i, GB_CREATE,
                 GB_TGCLI_GLFS_PATH, blk->block_name) == -1) {
      goto out;
    }

    if (prioCap) {
      if (!strcmp(prio_path, list->hosts[i-1])) {
        if (GB_ASPRINTF(&lun0, "%s/%s%s/tpg%zu/luns/lun0 set alua alua_tg_pt_gp_name=glfs_tg_pt_gp_ao",
                        GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i) == -1) {
          goto out;
        }
      } else {
        if (GB_ASPRINTF(&lun0, "%s/%s%s/tpg%zu/luns/lun0 set alua alua_tg_pt_gp_name=glfs_tg_pt_gp_ano",
                        GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i) == -1) {
          goto out;
        }
      }
    }

    if (!strcmp(blk->ipaddr, list->hosts[i-1])) {
      if (GB_ASPRINTF(&attr, "%s/%s%s/tpg%zu enable\n%s/%s%s/tpg%zu set attribute %s %s",
                   GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
                   GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
                   blk->auth_mode?"authentication=1":"", GB_TGCLI_ATTRIBUTES) == -1) {
        goto out;
      }
      if (GB_ASPRINTF(&portal, "%s/%s%s/tpg%zu/portals create %s ",
                   GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
                   blk->ipaddr) == -1) {
        goto out;
      }
    } else {
      if (GB_ASPRINTF(&attr, "%s/%s%s/tpg%zu set attribute tpg_enabled_sendtargets=0 %s %s",
                   GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
                   blk->auth_mode?"authentication=1":"", GB_TGCLI_ATTRIBUTES) == -1) {
        goto out;
      }
      if (GB_ASPRINTF(&portal, "%s/%s%s/tpg%zu/portals create %s",
                   GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
                   list->hosts[i-1]) == -1) {
        goto out;
      }
    }

    if (blk->auth_mode &&
        GB_ASPRINTF(&authcred, "\n%s/%s%s/tpg%zu set auth userid=%s password=%s",
          GB_TGCLI_ISCSI_PATH, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
          blk->gbid, blk->passwd) == -1) {
      goto out;
    }
    if (!tmp) {
      if (!prioCap) {
        if (GB_ASPRINTF(&exec, "%s\n%s\n%s\n%s %s\n%s\n%s %s",
            backstore, backstore_attr, iqn,
            tpg?tpg:"", lun, portal, attr, blk->auth_mode?authcred:"") == -1) {
          goto out;
        }
      } else {
        if (GB_ASPRINTF(&exec, "%s\n%s\n%s\n%s\n%s\n%s\n%s %s\n%s\n%s\n%s %s",
            backstore, backstore_attr, glfs_alua, glfs_alua_type, glfs_alua_sup, iqn,
            tpg?tpg:"", lun, lun0, portal, attr, blk->auth_mode?authcred:"") == -1) {
          goto out;
        }
      }
      tmp = exec;
    } else {
      if (!prioCap) {
        if (GB_ASPRINTF(&exec, "%s\n%s\n%s\n%s\n%s",
            tmp, lun, portal, attr, blk->auth_mode?authcred:"") == -1) {
          goto out;
        }
      } else {
        if (GB_ASPRINTF(&exec, "%s\n%s\n%s\n%s\n%s\n%s",
            tmp, lun, lun0, portal, attr, blk->auth_mode?authcred:"") == -1) {
          goto out;
        }
      }
      GB_FREE(tmp);
      tmp = exec;
    }

    GB_FREE(authcred);
    GB_FREE(attr);
    GB_FREE(portal);
    GB_FREE(lun);
    GB_FREE(lun0);
  }

  if (GB_ASPRINTF(&save, GB_TGCLI_GLFS_SAVE, blk->block_name) == -1) {
    goto out;
  }

  if (GB_ASPRINTF(&exec, "targetcli <<EOF\n%s\n%s\nexit\nEOF", tmp, save) == -1) {
    goto out;
  }
  GB_FREE(tmp);

  GB_CMD_EXEC_AND_VALIDATE(exec, reply, blk, blk->volume, CREATE_SRV);
  if (reply->exit) {
    snprintf(reply->out, 8192, "configure failed");
  }

 out:
  GB_FREE(exec);
  GB_FREE(save);
  GB_FREE(authcred);
  GB_FREE(attr);
  GB_FREE(portal);
  GB_FREE(lun);
  GB_FREE(tpg);
  GB_FREE(iqn);
  GB_FREE(backstore);
  GB_FREE(glfs_alua);
  GB_FREE(glfs_alua_type);
  GB_FREE(control);
  GB_FREE(backstore_attr);
  blockServerDefFree(list);
  GB_FREE(glfs_alua_sup);
  GB_FREE(io_timeout_str);

  return reply;
}


static blockResponse *
block_create_1_svc_st(blockCreate *blk, struct svc_req *rqstp)
{
  return block_create_common(blk, NULL, NULL, NULL, 0);
}


static blockResponse *
block_create_v2_1_svc_st(blockCreate2 *blk, struct svc_req *rqstp)
{
  char buf[1024] = {0,};
  char *control = NULL;
  blockCreate blk_v1 = {{0},};
  char *volServer = NULL;
  size_t len = blk->xdata.xdata_len;
  size_t blk_size = 0;
  size_t io_timeout = 0;
  struct gbXdata *xdata_val = (struct gbXdata*)blk->xdata.xdata_val;
  struct gbCreate *gbCreate = (struct gbCreate *)xdata_val->data;
  int n = 0;

  if (len > 0 && xdata_val && GB_XDATA_IS_MAGIC(xdata_val->magic)) {
    switch (GB_XDATA_GET_MAGIC_VER(xdata_val->magic)) {
    case 4:
      io_timeout = gbCreate->io_timeout;
    case 3:
      blk_size = gbCreate->blk_size;
      volServer = gbCreate->volServer;
      break;
    default:
      LOG("mgmt", GB_LOG_ERROR, "Shouldn't be here and getting unknown verion number!");
      break;
    }
  } else if (len > 0 && len <= HOST_NAME_MAX) {
    volServer = (char *)blk->xdata.xdata_val;
    volServer[len] = '\0';
  }

  if (blk->rb_size || blk_size) {
    n = snprintf(buf, 1024, " control=");
  }

  if (blk->rb_size) {
    n += snprintf(buf + n, 1024 - n, "%s=%u,", GB_RING_BUFFER_STR, blk->rb_size);
  }

  if (blk_size) {
    n += snprintf(buf + n, 1024 - n, "%s=%lu", GB_BLOCK_SIZE_STR, blk_size);
  }

  if (n) {
    if (GB_STRDUP(control, buf) < 0) {
        return NULL;
    }
  }

  convertTypeCreate2ToCreate(blk, &blk_v1);

  return block_create_common(&blk_v1, control, volServer, blk->prio_path,
                             io_timeout);
}

static void
blockCreateCliFormatResponse(struct glfs *glfs, blockCreateCli *blk,
                             blockCreate2 *cobj, int errCode,
                             char *errMsg, blockRemoteCreateResp *savereply,
                             blockResponse *reply)
{
  MetaInfo *info = NULL;
  json_object *json_obj = NULL;
  json_object *json_array = NULL;
  char         *tmp      = NULL;
  char         *tmp2     = NULL;
  char         *portals  = NULL;
  int          i         = 0;
  int          infoErrCode = 0;


  if (!reply) {
    return;
  }

  if (errCode < 0) {
    errCode = GB_DEFAULT_ERRCODE;
  }
  reply->exit = errCode;

  if (errMsg) {
    blockFormatErrorResponse(CREATE_SRV, blk->json_resp, errCode,
                             errMsg, reply);
    return;
  }

  if (GB_ALLOC(info) < 0) {
    blockFormatErrorResponse(CREATE_SRV, blk->json_resp, ENOMEM,
                             "Allocatoin Failed\n", reply);
    return;
  }

  if (blockGetMetaInfo(glfs, blk->block_name, info, &infoErrCode)) {
    if (infoErrCode == ENOENT) {
      blockFormatErrorResponse(CREATE_SRV, blk->json_resp,
                               (errCode?errCode:GB_DEFAULT_ERRCODE),
                               (savereply?savereply->errMsg:NULL), reply);
    }
    goto out;
  }

  if (!savereply)
    goto out;

  for (i = 0; i < info->nhosts; i++) {
    tmp = savereply->obj->d_attempt;
    if (blockMetaStatusEnumParse(info->list[i]->status) == GB_CONFIG_INPROGRESS) {
      if (GB_ASPRINTF(&savereply->obj->d_attempt, "%s %s",
                      (tmp==NULL?"":tmp), info->list[i]->addr) == -1) {
        goto out;
      }
      GB_FREE(tmp);
    }
  }
  tmp = NULL;

  if (savereply->obj->d_success || savereply->obj->d_attempt) {
    removeDuplicateSubstr(&savereply->obj->d_success);
    removeDuplicateSubstr(&savereply->obj->d_attempt);
  }

  if (blk->json_resp) {
    json_obj = json_object_new_object();
    json_object_object_add(json_obj, "IQN",
                           GB_JSON_OBJ_TO_STR(savereply->iqn));
    if (blk->auth_mode && savereply->iqn) {
      json_object_object_add(json_obj, "USERNAME",
                             GB_JSON_OBJ_TO_STR(cobj->gbid));
      json_object_object_add(json_obj, "PASSWORD",
                             GB_JSON_OBJ_TO_STR(cobj->passwd));
    }

    json_array = json_object_new_array();

    for (i = 0; i < savereply->nportal; i++) {
      json_object_array_add(json_array,
                            GB_JSON_OBJ_TO_STR(savereply->portal[i]));
    }

    json_object_object_add(json_obj, "PORTAL(S)", json_array);

    if (savereply->obj->d_attempt) {
         blockStr2arrayAddToJsonObj(json_obj, savereply->obj->d_attempt,
                                    "ROLLBACK FAILED ON");
    }

    if (savereply->obj->d_success) {
         blockStr2arrayAddToJsonObj(json_obj, savereply->obj->d_success,
                                    "ROLLBACK SUCCESS ON");
    }

    json_object_object_add(json_obj, "RESULT",
      errCode?GB_JSON_OBJ_TO_STR("FAIL"):GB_JSON_OBJ_TO_STR("SUCCESS"));

    GB_ASPRINTF(&reply->out, "%s\n",
                json_object_to_json_string_ext(json_obj,
                                     mapJsonFlagToJsonCstring(blk->json_resp)));
    json_object_put(json_obj);
  } else {
    for (i = 0; i < savereply->nportal; i++) {
      if (GB_ASPRINTF(&portals, "%s %s",
                      tmp!=NULL?tmp:"", savereply->portal[i]) == -1) {
        goto out;
      }
      GB_FREE(tmp);
      tmp = portals;
    }

    /* save 'failed on'*/
    tmp = NULL;
    if (savereply->obj->d_attempt) {
      if (GB_ASPRINTF(&tmp, "ROLLBACK FAILED ON: %s\n",
            savereply->obj->d_attempt?savereply->obj->d_attempt:"") == -1) {
        goto out;
      }
    }

    if (savereply->obj->d_success) {
      tmp2 = tmp;
      if (GB_ASPRINTF(&tmp, "%sROLLBACK SUCCESS ON: %s\n", tmp2?tmp2:"",
            savereply->obj->d_success?savereply->obj->d_success:"") == -1) {
        goto out;
      }
      GB_FREE(tmp2);
    }

    /* if savereply->iqn==NULL no point in printing auth */
    if (blk->auth_mode && savereply->iqn) {
      if (GB_ASPRINTF(&tmp2, "USERNAME: %s\nPASSWORD: %s\n",
                      cobj->gbid, cobj->passwd) == 1) {
        goto out;
      }
    }

    GB_ASPRINTF(&reply->out, "IQN: %s\n%sPORTAL(S): %s\n%sRESULT: %s\n",
                savereply->iqn?savereply->iqn:"-",
                blk->auth_mode?tmp2:"", portals?portals:"-", tmp?tmp:"",
                errCode?"FAIL":"SUCCESS");
  }

 out:
  /*catch all*/
  if (!reply->out) {
    blockFormatErrorResponse(CREATE_SRV, blk->json_resp, errCode,
                             GB_DEFAULT_ERRMSG, reply);
  }

  blockFreeMetaInfo(info);
  GB_FREE(tmp);
  GB_FREE(tmp2);
  return;
}


static blockResponse *
block_create_cli_1_svc_st(blockCreateCli *blk, struct svc_req *rqstp)
{
  int errCode = -1;
  uuid_t uuid;
  blockRemoteCreateResp *savereply = NULL;
  char gbid[UUID_BUF_SIZE];
  char passwd[UUID_BUF_SIZE];
  blockResponse *reply;
  struct glfs *glfs = NULL;
  struct glfs_fd *lkfd = NULL;
  blockServerDefPtr list = NULL;
  char *errMsg = NULL;
  blockCreate2 cobj = {{0},};
  bool *resultCaps = NULL;
  struct gbXdata *xdata = NULL;


  LOG("mgmt", GB_LOG_INFO,
      "create cli request, volume=%s blockname=%s mpath=%d blockhosts=%s "
      "authmode=%d size=%lu rbsize=%d blksize=%d io_timeout=%d",
      blk->volume, blk->block_name, blk->mpath, blk->block_hosts,
      blk->auth_mode, blk->size, blk->rb_size, blk->blk_size,
      blk->io_timeout);

  if (GB_ALLOC(reply) < 0) {
    goto optfail;
  }
  reply->exit = -1;

  list = blockServerParse(blk->block_hosts);
  if (!list) {
    goto optfail;
  }

  /* Fail if mpath > list->nhosts */
  if (blk->mpath > list->nhosts) {
    LOG("mgmt", GB_LOG_ERROR, "for block %s multipath request:%d is greater "
                              "than provided block-hosts:%s on volume %s",
         blk->block_name, blk->mpath, blk->block_hosts, blk->volume);
    if (GB_ASPRINTF(&errMsg, "multipath req: %d > block-hosts: %s\n",
                    blk->mpath, blk->block_hosts) == -1) {
      goto optfail;
    }
    reply->exit = ENODEV;
    goto optfail;
  }

  if (GB_ALLOC_N(resultCaps, GB_CAP_MAX) < 0) {
    goto optfail;
  }

  errCode = glusterBlockCheckCapabilities((void *)blk, CREATE_SRV, list, resultCaps, &errMsg);
  if (errCode && !resultCaps[GB_CREATE_LOAD_BALANCE_CAP]) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockCheckCapabilities() for block %s on volume %s failed",
        blk->block_name, blk->volume);
    goto optfail;
  } else if (resultCaps[GB_CREATE_LOAD_BALANCE_CAP]) {
    GB_FREE(errMsg);
    errCode = 0;
  }

  glfs = glusterBlockVolumeInit(blk->volume, &errCode, &errMsg);
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockVolumeInit(%s) for block %s with hosts %s failed",
        blk->volume, blk->block_name, blk->block_hosts);
    goto optfail;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume, &errCode, &errMsg);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s %s for block %s with hosts %s",
        FAILED_CREATING_META, blk->volume, blk->block_name, blk->block_hosts);
    goto optfail;
  }

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, errCode, errMsg, out);
  LOG("cmdlog", GB_LOG_INFO, "%s", blk->cmd);

  if (!glfs_access(glfs, blk->block_name, F_OK)) {
    LOG("mgmt", GB_LOG_ERROR,
        "block with name %s already exist in the volume %s",
        blk->block_name, blk->volume);
    if (GB_ASPRINTF(&errMsg, "BLOCK with name: '%s' already EXIST\n",
                    blk->block_name) == -1) {
      errCode = ENOMEM;
      goto exist;
    }
    errCode = EEXIST;
    goto exist;
  }

  if (!resultCaps[GB_CREATE_LOAD_BALANCE_CAP]) {
    blockGetPrioPath(glfs, blk->volume, list, cobj.prio_path, sizeof(cobj.prio_path));
  }

  uuid_generate(uuid);
  uuid_unparse(uuid, gbid);

  if (cobj.prio_path[0]) {
    GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                          errCode, errMsg, exist,
                          "VOLUME: %s\nGBID: %s\n"
                          "HA: %d\nENTRYCREATE: INPROGRESS\nPRIOPATH: %s\n",
                          blk->volume, gbid, blk->mpath, cobj.prio_path);
  } else {
    GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                          errCode, errMsg, exist,
                          "VOLUME: %s\nGBID: %s\n"
                          "HA: %d\nENTRYCREATE: INPROGRESS\n",
                          blk->volume, gbid, blk->mpath);
  }

  if (glusterBlockCreateEntry(glfs, blk, gbid, &errCode, &errMsg)) {
    LOG("mgmt", GB_LOG_ERROR, "%s volume: %s block: %s file: %s host: %s",
        FAILED_CREATING_FILE, blk->volume, blk->block_name, gbid, blk->block_hosts);
    goto exist;
  }

  if (!resultCaps[GB_CREATE_IO_TIMEOUT_CAP]) {
    GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                          errCode, errMsg, exist,
                          "SIZE: %zu\nRINGBUFFER: %u\nBLKSIZE: %u\n"
                          "IOTIMEOUT: %u\nENTRYCREATE: SUCCESS\n",
                          blk->size, blk->rb_size, blk->blk_size,
                          blk->io_timeout);
  } else {
    GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                          errCode, errMsg, exist,
                          "SIZE: %zu\nRINGBUFFER: %u\nBLKSIZE: %u\n"
                          "ENTRYCREATE: SUCCESS\n",
                          blk->size, blk->rb_size, blk->blk_size);
  }

  GB_STRCPYSTATIC(cobj.volume, blk->volume);
  GB_STRCPYSTATIC(cobj.block_name, blk->block_name);
  cobj.size = blk->size;
  cobj.rb_size = blk->rb_size;
  GB_STRCPYSTATIC(cobj.gbid, gbid);
  GB_STRDUP(cobj.block_hosts,  blk->block_hosts);

  if (!resultCaps[GB_CREATE_IO_TIMEOUT_CAP]) { // Create V4
    unsigned int len;
    struct gbCreate *gbCreate;

    len = sizeof(struct gbXdata) + sizeof(struct gbCreate);
    if (GB_ALLOC_N(xdata, len) < 0) {
      errCode = ENOMEM;
      goto exist;
    }

    xdata->magic = GB_XDATA_GEN_MAGIC(4);
    gbCreate = (struct gbCreate *)(&xdata->data);
    GB_STRCPY(gbCreate->volServer, (char *)gbConf->volServer, sizeof(gbConf->volServer));
    gbCreate->blk_size = blk->blk_size;
    gbCreate->io_timeout = blk->io_timeout;

    cobj.xdata.xdata_len = len;
    cobj.xdata.xdata_val = (char *)xdata;
  } else if (blk->blk_size) { // Create V3
    unsigned int len;
    struct gbCreate *gbCreate;

    len = sizeof(struct gbXdata) + sizeof(struct gbCreate);
    if (GB_ALLOC_N(xdata, len) < 0) {
      errCode = ENOMEM;
      goto exist;
    }

    xdata->magic = GB_XDATA_GEN_MAGIC(3);
    gbCreate = (struct gbCreate *)(&xdata->data);
    GB_STRCPY(gbCreate->volServer, (char *)gbConf->volServer, sizeof(gbConf->volServer));
    gbCreate->blk_size = blk->blk_size;

    cobj.xdata.xdata_len = len;
    cobj.xdata.xdata_val = (char *)xdata;
  } else { // Create V2
    cobj.xdata.xdata_len = strlen(gbConf->volServer);
    cobj.xdata.xdata_val = (char *)gbConf->volServer;
  }

  if (blk->auth_mode) {
    uuid_generate(uuid);
    uuid_unparse(uuid, passwd);

    GB_STRCPYSTATIC(cobj.passwd, passwd);
    cobj.auth_mode = 1;

    GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                          errCode, errMsg, exist, "PASSWORD: %s\n", passwd);
  }

  errCode = glusterBlockCreateRemoteAsync(list, blk->mpath,
                                          glfs, &cobj, &savereply);
  if (errCode) {
    LOG("mgmt", GB_LOG_WARNING, "glusterBlockCreateRemoteAsync: return %d"
        " %s for block %s on volume %s with hosts %s", errCode,
        FAILED_REMOTE_AYNC_CREATE, blk->block_name,
        blk->volume, blk->block_hosts);
  }

  /* Check Point */
  errCode = glusterBlockAuditRequest(glfs, blk, &cobj, list, &savereply);
  if (errCode) {
    LOG("mgmt", GB_LOG_ERROR, "glusterBlockAuditRequest: return %d"
        " volume: %s hosts: %s blockname %s", errCode,
        blk->volume, blk->block_hosts, blk->block_name);
  } else if (!resultCaps[GB_CREATE_LOAD_BALANCE_CAP] && cobj.prio_path[0]) {
    blockIncPrioAttr(glfs, blk->volume, cobj.prio_path);
  }

 exist:
  GB_METAUNLOCK(lkfd, blk->volume, errCode, errMsg);

 out:
  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "glfs_close(%s): on volume %s for "
        "block %s failed[%s]", GB_TXLOCKFILE, blk->volume,
        blk->block_name, strerror(errno));
  }

 optfail:
  LOG("mgmt", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO),
      "create cli return %s, volume=%s blockname=%s",
      errCode ? "failure" : "success", blk->volume, blk->block_name);

  blockCreateCliFormatResponse(glfs, blk, &cobj, errCode, errMsg, savereply, reply);
  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s",
      reply ? reply->out : "*Nil*");
  GB_FREE(errMsg);
  blockServerDefFree(list);
  blockCreateParsedRespFree(savereply);
  GB_FREE (cobj.block_hosts);
  GB_FREE(resultCaps);
  GB_FREE(xdata);

  return reply;
}

bool_t
block_create_1_svc(blockCreate *blk, blockResponse *reply, struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(create, blk, reply, rqstp, ret);
  return ret;
}

bool_t
block_create_v2_1_svc(blockCreate2 *blk, blockResponse *reply, struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(create_v2, blk, reply, rqstp, ret);
  return ret;
}

bool_t
block_create_cli_1_svc(blockCreateCli *blk, blockResponse *reply,
                       struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(create_cli, blk, reply, rqstp, ret);
  return ret;
}
