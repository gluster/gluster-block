/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "block_common.h"

# define   GB_ALUA_AO_TPG_NAME          "glfs_tg_pt_gp_ao"
# define   GB_ALUA_ANO_TPG_NAME         "glfs_tg_pt_gp_ano"


static struct json_object *
getTpgObj(char *block, MetaInfo *info, blockGenConfigCli *blk, char *portal, int tag)
{
  int auth = 0;
  uuid_t uuid;
  char alias[UUID_BUF_SIZE];
  char alias_10[11] = {'\0', };
  /*
   * 256 + extra 32 bytes for string "/backstores/user/" to
   * suppress the truncated warning when compiling.
   */
  char lun_so[288] = {'\0', };

  struct json_object *tpg_obj = json_object_new_object();
  struct json_object *tpg_attr_obj = json_object_new_object();
  struct json_object *tpg_luns_arr = json_object_new_array();
  struct json_object *tpg_lun_obj = json_object_new_object();
  struct json_object *tpg_portals_arr = json_object_new_array();
  struct json_object *tpg_portal_obj = json_object_new_object();
  struct json_object *tpg_params_obj = json_object_new_object();


  // {  Tpg Object open
  auth = 1;
  if(info->passwd[0] == '\0') {
    auth = 0;
  }

  json_object_object_add(tpg_attr_obj, "authentication", json_object_new_int(auth));
  json_object_object_add(tpg_attr_obj, "cache_dynamic_acls", json_object_new_int(1));
  json_object_object_add(tpg_attr_obj, "demo_mode_write_protect", json_object_new_int(0));
  json_object_object_add(tpg_attr_obj, "generate_node_acls", json_object_new_int(1));
  if (strcmp(blk->addr, portal)) {
    json_object_object_add(tpg_attr_obj, "tpg_enabled_sendtargets", json_object_new_int(0));
  }
  json_object_object_add(tpg_obj, "attributes", tpg_attr_obj);
  if (auth) {
    json_object_object_add(tpg_obj, "chap_password", GB_JSON_OBJ_TO_STR(info->passwd));
    json_object_object_add(tpg_obj, "chap_userid", GB_JSON_OBJ_TO_STR(info->gbid));
  }

  if (!strcmp(blk->addr, portal)) {
    json_object_object_add(tpg_obj, "enable", json_object_new_boolean(TRUE));
  } else {
    json_object_object_add(tpg_obj, "enable", json_object_new_boolean(FALSE));
  }

  // "luns" : [
  uuid_generate(uuid);
  uuid_unparse(uuid, alias);
  snprintf(alias_10, 11, "%.10s", alias+24);
  json_object_object_add(tpg_lun_obj, "alias", GB_JSON_OBJ_TO_STR(alias_10[0]?alias_10:NULL));
  snprintf(lun_so, 288, "/backstores/user/%s", block);
  if (info->prio_path[0]) {
    if (!strcmp(info->prio_path, portal)) {
      json_object_object_add(tpg_lun_obj, "alua_tg_pt_gp_name",
                             GB_JSON_OBJ_TO_STR(GB_ALUA_AO_TPG_NAME));
    } else {
      json_object_object_add(tpg_lun_obj, "alua_tg_pt_gp_name",
                             GB_JSON_OBJ_TO_STR(GB_ALUA_ANO_TPG_NAME));
    }
  }
  json_object_object_add(tpg_lun_obj, "storage_object", GB_JSON_OBJ_TO_STR(lun_so[0]?lun_so:NULL));
  json_object_object_add(tpg_lun_obj, "index", json_object_new_int(0));
  json_object_array_add(tpg_luns_arr, tpg_lun_obj);
  json_object_object_add(tpg_obj, "luns", tpg_luns_arr);
  // ]

  if (auth) {
    json_object_object_add(tpg_params_obj, "AuthMethod", GB_JSON_OBJ_TO_STR("CHAP"));
  }
  json_object_object_add(tpg_obj, "parameters", tpg_params_obj);

  // "portals" : [
  json_object_object_add(tpg_portal_obj, "ip_address", GB_JSON_OBJ_TO_STR(portal));
  json_object_object_add(tpg_portal_obj, "port", json_object_new_int(3260));
  json_object_array_add(tpg_portals_arr, tpg_portal_obj);
  json_object_object_add(tpg_obj, "portals", tpg_portals_arr);
  // ]

  json_object_object_add(tpg_obj, "tag", json_object_new_int(tag));
  // }  Tpg Object close

  return tpg_obj;
}


static struct json_object *
getTgObj(char *block, MetaInfo *info, blockGenConfigCli *blk)
{
  size_t i, tag = 1;
  struct json_object *tg_obj = json_object_new_object();
  struct json_object *tpgs_arr = json_object_new_array();
  struct json_object *tpg_obj = NULL;
  char iqn[128] = {'\0', };


  json_object_object_add(tg_obj, "fabric", GB_JSON_OBJ_TO_STR("iscsi"));

  // "tpgs:" : [
  // {
  for (i = 0; i < info->nhosts; i++) {
    if (blockhostIsValid (info->list[i]->status)) {
      tpg_obj = getTpgObj(block, info, blk, info->list[i]->addr, tag);
      json_object_array_add(tpgs_arr, tpg_obj);
      tag++;
    }
  }
  // },
  // ]

  json_object_object_add(tg_obj, "tpgs", tpgs_arr);

  snprintf(iqn, 128, "%s%s", GB_TGCLI_IQN_PREFIX, info->gbid);
  json_object_object_add(tg_obj, "wwn", GB_JSON_OBJ_TO_STR(iqn[0]?iqn:NULL));

  return tg_obj;
}


static struct json_object *
getSoObj(char *block, MetaInfo *info, blockGenConfigCli *blk)
{
  char cfgstr[2048] = {'\0', };
  char control[1024] = {'\0', };
  char io_timeout[128] = {'\0', };
  struct json_object *so_obj = json_object_new_object();
  struct json_object *so_obj_alua_ao_tpg;
  struct json_object *so_obj_alua_ano_tpg;
  struct json_object *so_obj_alua_tpgs_arr;
  struct json_object *so_obj_attr = json_object_new_object();
  int n = 0;


  // "alua_tpgs": [
  // {
  if (info->prio_path[0]) {
    so_obj_alua_ao_tpg = json_object_new_object();
    so_obj_alua_ano_tpg = json_object_new_object();
    so_obj_alua_tpgs_arr = json_object_new_array();

    json_object_object_add(so_obj_alua_ao_tpg, "alua_access_type", json_object_new_int(1));
    json_object_object_add(so_obj_alua_ao_tpg, "alua_access_state", json_object_new_int(0));
    json_object_object_add(so_obj_alua_ao_tpg, "alua_support_offline", json_object_new_int(0));
    json_object_object_add(so_obj_alua_ao_tpg, "alua_support_standby", json_object_new_int(0));
    json_object_object_add(so_obj_alua_ao_tpg, "alua_support_unavailable", json_object_new_int(0));
    json_object_object_add(so_obj_alua_ao_tpg, "name", GB_JSON_OBJ_TO_STR(GB_ALUA_AO_TPG_NAME));
    json_object_object_add(so_obj_alua_ao_tpg, "tg_pt_gp_id", json_object_new_int(1));

    json_object_object_add(so_obj_alua_ano_tpg, "alua_access_type", json_object_new_int(1));
    json_object_object_add(so_obj_alua_ano_tpg, "alua_access_state", json_object_new_int(1));
    json_object_object_add(so_obj_alua_ano_tpg, "alua_support_offline", json_object_new_int(0));
    json_object_object_add(so_obj_alua_ano_tpg, "alua_support_standby", json_object_new_int(0));
    json_object_object_add(so_obj_alua_ano_tpg, "alua_support_unavailable", json_object_new_int(0));
    json_object_object_add(so_obj_alua_ano_tpg, "name", GB_JSON_OBJ_TO_STR(GB_ALUA_ANO_TPG_NAME));
    json_object_object_add(so_obj_alua_ano_tpg, "tg_pt_gp_id", json_object_new_int(2));

  // }

    json_object_array_add(so_obj_alua_tpgs_arr, so_obj_alua_ao_tpg);
    json_object_array_add(so_obj_alua_tpgs_arr, so_obj_alua_ano_tpg);

    json_object_object_add(so_obj, "alua_tpgs", so_obj_alua_tpgs_arr);
  }
  // ]

  // "attributes": {
  json_object_object_add(so_obj_attr, "cmd_time_out", json_object_new_int(GB_CMD_TIME_OUT));
  json_object_object_add(so_obj_attr, "dev_size", json_object_new_int64(info->size));

  json_object_object_add(so_obj, "attributes", so_obj_attr);
  // }

  if (info->io_timeout) {
    snprintf(io_timeout, 128, ";%s=%lu", GB_IO_TIMEOUT_STR, info->io_timeout);
  }

  if (!strcmp(gbConf->volServer, "localhost")) {
    snprintf(cfgstr, 2048, "glfs/%s@%s/block-store/%s%s", info->volume,
             blk->addr, info->gbid, io_timeout[0]?io_timeout:"");
  } else {
    snprintf(cfgstr, 2048, "glfs/%s@%s/block-store/%s%s", info->volume,
             gbConf->volServer, info->gbid, io_timeout[0]?io_timeout:"");
  }
  json_object_object_add(so_obj, "config", GB_JSON_OBJ_TO_STR(cfgstr[0]?cfgstr:NULL));
  if (info->rb_size) {
    n = snprintf(control, 1024, "%s=%zu", GB_RING_BUFFER_STR, info->rb_size);
  }
  if (info->blk_size) {
    if (n) {
      control[n++] = ',';
    }
    snprintf(control + n, 1024 - n, "%s=%zu", GB_BLOCK_SIZE_STR, info->blk_size);
  }
  if (control[0]) {
    json_object_object_add(so_obj, "control", GB_JSON_OBJ_TO_STR(control[0]?control:NULL));
  }
  json_object_object_add(so_obj, "name", GB_JSON_OBJ_TO_STR(block));
  json_object_object_add(so_obj, "plugin", GB_JSON_OBJ_TO_STR("user"));
  json_object_object_add(so_obj, "size", json_object_new_int64(info->size));
  json_object_object_add(so_obj, "wwn", GB_JSON_OBJ_TO_STR(info->gbid));

  return so_obj;
}


static int
getSoTgArraysForAllVolume(struct soTgObj *obj, blockGenConfigCli *blk,
                          char **errMsg, int *errCode)
{
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  struct glfs_fd *tgmdfd = NULL;
  struct dirent *entry;
  MetaInfo *info = NULL;
  strToCharArrayDefPtr vols;
  size_t i, j;
  int ret = -1;
  bool partOfBlock;
  blockServerDefPtr list = NULL;
  struct json_object *so_obj = NULL;
  struct json_object *tg_obj = NULL;


  vols = getCharArrayFromDelimitedStr(blk->volume, GB_DELIMITER);
  if (!vols) {
    LOG("mgmt", GB_LOG_ERROR,
        "getCharArrayFromDelimitedStr(%s) failed", blk->volume);
    goto optfail;
  }

  for (i = 0; i < vols->len; i++) {
    glfs = glusterBlockVolumeInit(vols->data[i], errCode, errMsg);
    if (!glfs) {
      LOG("mgmt", GB_LOG_ERROR,
          "glusterBlockVolumeInit(%s) failed", vols->data[i]);
      goto optfail;
    }

    lkfd = glusterBlockCreateMetaLockFile(glfs, vols->data[i], errCode, errMsg);
    if (!lkfd) {
      LOG("mgmt", GB_LOG_ERROR, "%s %s", FAILED_CREATING_META, vols->data[i]);
      goto optfail;
    }

    GB_METALOCK_OR_GOTO(lkfd, vols->data[i], *errCode, *errMsg, out);

    tgmdfd = glfs_opendir(glfs, GB_METADIR);
    if (!tgmdfd) {
      *errCode = errno;
      GB_ASPRINTF(errMsg, "Not able to open metadata directory for volume "
          "%s[%s]", vols->data[i], strerror(*errCode));
      LOG("mgmt", GB_LOG_ERROR, "glfs_opendir(%s): on volume %s failed[%s]",
          GB_METADIR, vols->data[i], strerror(errno));
      ret = -1;
      goto out;
    }

    while ((entry = glfs_readdir(tgmdfd))) {
      if (!strchr(entry->d_name, '.')) {
        if (GB_ALLOC(info) < 0) {
          ret = -1;
          goto out;
        }
        ret = blockGetMetaInfo(glfs, entry->d_name, info, NULL);
        if (ret) {
          goto out;
        }

        if (!info->prio_path[0]) {
          if (list) {
            /* if 'list' is set, it means, it came here continuing the loop */
            blockServerDefFree(list);
          }

          /* default as the load balancing is enabled */
          list = blockMetaInfoToServerParse(info);
          if (!list) {
            ret = -1;
            goto out;
          }

          blockGetPrioPath(glfs, blk->volume, list, info->prio_path, sizeof(info->prio_path));
          blockIncPrioAttr(glfs, blk->volume, info->prio_path);

          GB_METAUPDATE_OR_GOTO(lock, glfs, entry->d_name, vols->data[i],
                                *errCode, *errMsg, out, "PRIOPATH: %s\n", info->prio_path);
        }

        partOfBlock = false;
        for (j = 0; j < info->nhosts; j++) {
          if (blockhostIsValid(info->list[j]->status) && !strcmp(info->list[j]->addr, blk->addr)) {
            partOfBlock = true;
          }
        }
        if (!partOfBlock) {
          blockFreeMetaInfo(info);
          continue;
        }

        /* storage_objects */
        so_obj = getSoObj(entry->d_name, info, blk);
        json_object_array_add(obj->so_arr, so_obj);

        /* targets */
        tg_obj = getTgObj(entry->d_name, info, blk);
        json_object_array_add(obj->tg_arr, tg_obj);

        blockFreeMetaInfo(info);
      }
    }

    GB_METAUNLOCK(lkfd, vols->data[i], *errCode, *errMsg);
    if (tgmdfd && glfs_closedir (tgmdfd) != 0) {
      LOG("mgmt", GB_LOG_ERROR, "glfs_closedir(%s): on volume %s failed[%s]",
          GB_METADIR, vols->data[i], strerror(errno));
    }
    if (lkfd && glfs_close(lkfd) != 0) {
      LOG("mgmt", GB_LOG_ERROR, "glfs_close(%s): on volume %s failed[%s]",
          GB_TXLOCKFILE, vols->data[i], strerror(errno));
    }
  }

  ret = 0;
  goto free;

 out:
  GB_METAUNLOCK(lkfd, vols->data[i], *errCode, *errMsg);
  if (tgmdfd && glfs_closedir (tgmdfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "glfs_closedir(%s): on volume %s failed[%s]",
        GB_METADIR, vols->data[i], strerror(errno));
  }
  blockFreeMetaInfo(info);

 optfail:
  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "glfs_close(%s): on volume %s failed[%s]",
        GB_TXLOCKFILE, vols->data[i], strerror(errno));
  }

 free:
  strToCharArrayDefFree(vols);
  blockServerDefFree(list);

  return ret;
}


static int
glusterBlockGenConfigSvc(blockGenConfigCli *blk,
                         blockResponse *reply, char **errMsg, int *errCode)
{
  struct soTgObj *obj = NULL;
  struct json_object *jobj = json_object_new_object();


  if (GB_ALLOC(obj) < 0) {
    goto out;
  }
  obj->so_arr = json_object_new_array();
  obj->tg_arr = json_object_new_array();

  reply->exit = getSoTgArraysForAllVolume(obj, blk, errMsg, errCode);
  if(reply->exit) {
    LOG("mgmt", GB_LOG_ERROR, "getSoTgArraysPerVolume(): on volume[s] %s failed",
        blk->volume);
    goto out;
  }

  json_object_object_add(jobj, "storage_objects", obj->so_arr);
  json_object_object_add(jobj, "targets", obj->tg_arr);

 out:
  if(reply->exit) {
    blockFormatErrorResponse(GENCONFIG_SRV, blk->json_resp, *errCode,
                             GB_DEFAULT_ERRMSG, reply);
  } else {
    GB_ASPRINTF(&reply->out, "%s\n", json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY));
  }
  json_object_put(jobj);
  GB_FREE(obj);

  return reply->exit;
}


static blockResponse *
block_gen_config_cli_1_svc_st(blockGenConfigCli *blk, struct svc_req *rqstp)
{
  blockResponse *reply = NULL;
  int errCode = -1;
  char *errMsg = NULL;


  LOG("mgmt", GB_LOG_INFO,
      "genconfig cli request, volume[s]=%s addr=%s", blk->volume, blk->addr);

  if (GB_ALLOC(reply) < 0) {
    goto out;
  }
  reply->exit = -1;

  errCode = 0;
  if (glusterBlockGenConfigSvc(blk, reply, &errMsg, &errCode)) {
    LOG("mgmt", GB_LOG_ERROR, "glusterBlockGenConfigSvc(): on volume[s] %s failed with %s",
        blk->volume, errMsg?errMsg:"");
    goto out;
  }

 out:
  LOG("mgmt", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO),
      "genconfig cli return %s, volume=%s",
      errCode ? "failure" : "success", blk->volume);

  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s",
      reply ? reply->out : "*Nil*");

  GB_FREE(errMsg);
  return reply;
}


bool_t
block_gen_config_cli_1_svc(blockGenConfigCli *blk, blockResponse *reply,
                      struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(gen_config_cli, blk, reply, rqstp, ret);
  return ret;
}
