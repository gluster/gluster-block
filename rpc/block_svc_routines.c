/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "common.h"
# include  "capabilities.h"
# include  "glfs-operations.h"

# include  <pthread.h>
# include  <netdb.h>
# include  <uuid/uuid.h>
# include  <json-c/json.h>


# define   UUID_BUF_SIZE        38
# define   GB_DEFAULT_ERRCODE   255

# define   GB_CREATE            "create"
# define   GB_DELETE            "delete"
# define   GB_MSERVER_DELIMITER ","

# define   GB_TGCLI_GLFS_PATH   "/backstores/user:glfs"
# define   GB_TGCLI_GLFS        "targetcli " GB_TGCLI_GLFS_PATH
# define   GB_TGCLI_CHECK       GB_TGCLI_GLFS " ls | grep ' %s ' | grep '/%s ' > " DEVNULLPATH
# define   GB_TGCLI_ISCSI_PATH  "/iscsi"
# define   GB_TGCLI_GLFS_SAVE   GB_TGCLI_GLFS_PATH "/%s saveconfig"
# define   GB_TGCLI_ATTRIBUTES  "generate_node_acls=1 demo_mode_write_protect=0"
# define   GB_TGCLI_IQN_PREFIX  "iqn.2016-12.org.gluster-block:"

# define   GB_JSON_OBJ_TO_STR(x) json_object_new_string(x?x:"")
# define   GB_DEFAULT_ERRMSG    "Operation failed, please check the log "\
                                "file to find the reason."
# define   GB_GET_PORTAL_TPG    "targetcli /iscsi/" GB_TGCLI_IQN_PREFIX \
                                "'%s' ls | grep -e tpg -e '%s' | grep -B1 '%s' | grep -o 'tpg\\w'"
# define   GB_CHECK_PORTAL      "targetcli /iscsi/" GB_TGCLI_IQN_PREFIX \
                                "'%s' ls | grep '%s' > " DEVNULLPATH
# define   GB_SAVECONFIG_CHECK  "grep -m 1 '\"name\": \"%s\",' " GB_SAVECONFIG " > " DEVNULLPATH

# define   GB_ALUA_AO_TPG_NAME          "glfs_tg_pt_gp_ao"
# define   GB_ALUA_ANO_TPG_NAME         "glfs_tg_pt_gp_ano"
# define   GB_RING_BUFFER_STR           "max_data_area_mb"

#define    GB_CMD_TIME_OUT      130

# define   GB_OLD_CAP_MAX       9

# define   GB_OP_SKIPPED        222
# define   GB_NODE_NOT_EXIST    223
# define   GB_NODE_IN_USE       224
# define   GB_BLOCK_NOT_LOADED  225
# define   GB_BLOCK_NOT_FOUND   226

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef enum operations {
  CREATE_SRV = 1,
  DELETE_SRV,
  MODIFY_SRV,
  MODIFY_TPGC_SRV,
  MODIFY_SIZE_SRV,
  REPLACE_SRV,
  REPLACE_GET_PORTAL_TPG_SRV,
  LIST_SRV,
  INFO_SRV,
  VERSION_SRV,
  GENCONFIG_SRV
} operations;


typedef struct blockRemoteObj {
    struct glfs *glfs;
    void *obj;
    char *volume;
    char *addr;
    char *reply;
    int  exit;
} blockRemoteObj;


typedef struct blockRemoteResp {
  char *attempt;
  char *success;
  char *skipped;
  int   status;
} blockRemoteResp;


typedef struct blockRemoteReplaceResp {
  blockRemoteResp *cop;
  blockRemoteResp *dop;
  blockRemoteResp *rop;
  bool force;
  int   status;
} blockRemoteReplaceResp;


typedef struct blockRemoteModifyResp {
  char *attempt;
  char *success;
  char *rb_attempt;
  char *rb_success;
} blockRemoteModifyResp;


typedef struct soTgObj {
   struct json_object *so_arr;
   struct json_object *tg_arr;
} soTgObj;


typedef struct blockRemoteDeleteResp {
  char *d_attempt;
  char *d_success;
} blockRemoteDeleteResp;


typedef struct blockRemoteCreateResp {
  char *errMsg;
  char *backend_size;
  char *iqn;
  size_t nportal;
  char **portal;
  blockRemoteDeleteResp *obj;
} blockRemoteCreateResp;


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


void
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


static int
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


static void
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


static int
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

  /* if block is loaded, skip the rest */
  if (!ret) {
    goto out;
  }
  GB_FREE(exec);

  if (GB_ASPRINTF(&exec, GB_SAVECONFIG_CHECK, block_name) == -1) {
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


struct addrinfo *
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

    if (cblk_v2->rb_size || cblk_v2->prio_path[0]) {
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


static blockServerDefPtr
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


void *
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
gbCapResp *
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
        GB_ASPRINTF(errMsg, "capability '%s' doesn't exit on %s",
                    gbCapabilitiesLookup[i], args[j].addr);
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


  /* skip if nhosts = 1 */
  if (!servers || (servers->nhosts <= 1)) {
    return 0;
  }

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
glusterBlockCreateRemoteAsync(blockServerDefPtr list,
                            size_t listindex, size_t mpath,
                            struct glfs *glfs,
                            blockCreate2 *cobj,
                            blockRemoteCreateResp **savereply)
{
  pthread_t  *tid = NULL;
  blockRemoteObj *args = NULL;
  int ret = -1;
  size_t i;


  if (GB_ALLOC_N(tid, mpath) < 0) {
    goto out;
  }

  if (GB_ALLOC_N(args, mpath) < 0) {
    goto out;
 }

  for (i = 0; i < mpath; i++) {
    args[i].glfs = glfs;
    args[i].obj = (void *)cobj;
    args[i].volume = cobj->volume;
    args[i].addr = list->hosts[i + listindex];
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
    GB_FREE(args[i].reply);
  }
  GB_FREE(args);
  GB_FREE(tid);

  return ret;
}


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
glusterBlockDeleteFillArgs(MetaInfo *info, bool deleteall, blockRemoteObj *args,
                           struct glfs *glfs, blockDelete *dobj)
{
  int i = 0;
  size_t count = 0;

  for (i = 0, count = 0; i < info->nhosts; i++) {
    switch (blockMetaStatusEnumParse(info->list[i]->status)) {
    case GB_CONFIG_SUCCESS:
    case GB_AUTH_ENFORCEING:
    case GB_AUTH_ENFORCED:
    case GB_AUTH_ENFORCE_FAIL:
    case GB_AUTH_CLEAR_ENFORCED:
    case GB_AUTH_CLEAR_ENFORCEING:
    case GB_AUTH_CLEAR_ENFORCE_FAIL:
    case GB_RP_SUCCESS:
    case GB_RP_FAIL:
    case GB_RP_INPROGRESS:
    case GB_RS_INPROGRESS:
    case GB_RS_SUCCESS:
    case GB_RS_FAIL:
      if (!deleteall)
        break;
 /* case GB_CONFIG_INPROGRESS: untouched may be due to connect failed */
    case GB_CONFIG_FAIL:
    case GB_CLEANUP_INPROGRESS:
    case GB_CLEANUP_FAIL:
      if (args) {
        args[count].glfs = glfs;
        args[count].obj = (void *)dobj;
        args[count].volume = info->volume;
        args[count].addr = info->list[i]->addr;
      }
      count++;
      break;
    }
  }
  return count;
}


static int
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


static int
glusterBlockDeleteRemoteAsync(char *blockname,
                              MetaInfo *info,
                              struct glfs *glfs,
                              blockDelete *dobj,
                              size_t count,
                              bool deleteall,
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


  if (GB_ALLOC_N(tid, count) < 0) {
    goto out;
  }

  if (GB_ALLOC_N(args, count) < 0) {
    goto out;
  }

  count = glusterBlockDeleteFillArgs(info, deleteall, args, glfs, dobj);

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
    if (args[i].exit){
      if (args[i].exit == GB_BLOCK_NOT_FOUND) {
        cleanupsuccess++;
      }
    }
  }

  /* get new MetaInfo and compare */
  if (GB_ALLOC(info_new) < 0) {
    goto out;
  }

  ret = blockGetMetaInfo(glfs, blockname, info_new, NULL);
  if (ret) {
    goto out;
  }
  ret = -1;

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


void *
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


void *
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

bool *
glusterBlockBuildMinCaps(void *data, operations opt)
{
  blockCreateCli *cblk = NULL;
  blockDeleteCli *dblk = NULL;
  blockModifyCli *mblk = NULL;
  blockModifySizeCli *msblk = NULL;
  blockReplaceCli *rblk = NULL;
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
    minCaps[GB_CREATE_LOAD_BALANCE_CAP] = true;
    if (cblk->json_resp) {
      minCaps[GB_JSON_CAP] = true;
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


static int
glusterBlockCheckCapabilities(void* blk, operations opt, blockServerDefPtr list,
                              bool *resultCaps, char **errMsg)
{
  int errCode = 0;
  bool *minCaps = NULL;
  char *localErrMsg = NULL;


  /* skip if nhosts = 1 */
  if (!list || (list->nhosts <= 1)) {
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


void *
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


void
blockRemoteReplaceRespFree(blockRemoteReplaceResp *resp)
{

  if (!resp)
    return;

  blockRemoteRespFree(resp->cop);
  blockRemoteRespFree(resp->dop);
  blockRemoteRespFree(resp->rop);
  GB_FREE(resp);
}


int
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

  cobj->xdata.xdata_len = strlen(gbConf->volServer);
  cobj->xdata.xdata_val = (char *) gbConf->volServer;
  GB_STRCPYSTATIC(cobj->ipaddr, blk->new_node);
  GB_STRCPYSTATIC(cobj->volume, info->volume);
  GB_STRCPYSTATIC(cobj->gbid, info->gbid);
  cobj->size = info->size;
  cobj->rb_size = info->rb_size;
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
  GB_FREE(cobj);
  GB_FREE(dobj);
  GB_FREE(robj);
  GB_FREE(args);
  blockRemoteReplaceRespFree(reply);

  return ret;
}


void
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


blockResponse *
block_replace_cli_1_svc_st(blockReplaceCli *blk, struct svc_req *rqstp)
{
  blockRemoteReplaceResp *savereply = NULL;
  blockResponse *reply = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  int errCode = 0;
  char *errMsg = NULL;
  int ret;
  blockServerDefPtr list = NULL;
  MetaInfo *info = NULL;


  LOG("mgmt", GB_LOG_DEBUG,
      "replace request, volume=%s, blockname=%s oldnode=%s newnode=%s force=%d",
      blk->volume, blk->block_name, blk->old_node, blk->new_node, blk->force);

  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }
  reply->exit = -1;

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
    goto out;
  }
  if (blockGetMetaInfo(glfs, blk->block_name, info, NULL)) {
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

  errCode = 0;

  LOG("mgmt", GB_LOG_DEBUG, "replace cli success, volume=%s", blk->volume);

 out:
  GB_METAUNLOCK(lkfd, blk->volume, errCode, errMsg);
  blockReplaceNodeCliFormatResponse(blk, errCode, errMsg, savereply, reply);
  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s", reply->out);
  blockServerDefFree(list);
  blockRemoteReplaceRespFree(savereply);
  blockFreeMetaInfo(info);

optfail:
  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): on volume %s for block %s failed[%s]",
        GB_TXLOCKFILE, blk->volume, blk->block_name, strerror(errno));
  }

  GB_FREE(errMsg);

  return reply;
}


struct json_object *
getTpgObj(char *block, MetaInfo *info, blockGenConfigCli *blk, char *portal, int tag)
{
  int auth = 0;
  uuid_t uuid;
  char alias[UUID_BUF_SIZE];
  char alias_10[11] = {'\0', };
  char lun_so[256] = {'\0', };

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
  snprintf(lun_so, 256, "/backstores/user/%s", block);
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


struct json_object *
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


struct json_object *
getSoObj(char *block, MetaInfo *info, blockGenConfigCli *blk)
{
  char cfgstr[1024] = {'\0', };
  char control[1024] = {'\0', };
  struct json_object *so_obj = json_object_new_object();
  struct json_object *so_obj_alua_ao_tpg;
  struct json_object *so_obj_alua_ano_tpg;
  struct json_object *so_obj_alua_tpgs_arr;
  struct json_object *so_obj_attr = json_object_new_object();


  // "alua_tpgs": [
  // {
  if (info->prio_path[0]) {
    so_obj_alua_ao_tpg = json_object_new_object();
    so_obj_alua_ano_tpg = json_object_new_object();
    so_obj_alua_tpgs_arr = json_object_new_array();

    json_object_object_add(so_obj_alua_ao_tpg, "alua_access_type", json_object_new_int(1));
    json_object_object_add(so_obj_alua_ao_tpg, "alua_access_state", json_object_new_int(0));
    json_object_object_add(so_obj_alua_ao_tpg, "name", GB_JSON_OBJ_TO_STR(GB_ALUA_AO_TPG_NAME));
    json_object_object_add(so_obj_alua_ao_tpg, "tg_pt_gp_id", json_object_new_int(1));

    json_object_object_add(so_obj_alua_ano_tpg, "alua_access_type", json_object_new_int(1));
    json_object_object_add(so_obj_alua_ano_tpg, "alua_access_state", json_object_new_int(1));
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

  if (!strcmp(gbConf->volServer, "localhost")) {
    snprintf(cfgstr, 1024, "glfs/%s@%s/block-store/%s", info->volume, blk->addr, info->gbid);
  } else {
    snprintf(cfgstr, 1024, "glfs/%s@%s/block-store/%s", info->volume, gbConf->volServer, info->gbid);
  }
  json_object_object_add(so_obj, "config", GB_JSON_OBJ_TO_STR(cfgstr[0]?cfgstr:NULL));
  if (info->rb_size) {
    snprintf(control, 1024, "%s=%zu", GB_RING_BUFFER_STR, info->rb_size);
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


  vols = getCharArrayFromDelimitedStr(blk->volume, GB_VOLS_DELIMITER);
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


blockResponse *
block_gen_config_cli_1_svc_st(blockGenConfigCli *blk, struct svc_req *rqstp)
{
  blockResponse *reply = NULL;
  int errCode = 0;
  char *errMsg = NULL;


  LOG("mgmt", GB_LOG_DEBUG,
      "genconfig request, volume[s]=%s addr=%s", blk->volume, blk->addr);

  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }
  reply->exit = -1;

  errCode = glusterBlockGenConfigSvc(blk, reply, &errMsg, &errCode);
  if (errCode) {
    LOG("mgmt", GB_LOG_ERROR, "glusterBlockGenConfigSvc(): on volume[s] %s failed with %s",
        blk->volume, errMsg?errMsg:"");
    goto out;
  }

  LOG("mgmt", GB_LOG_DEBUG, "genconfig cli success, volume[s]=%s", blk->volume);

 out:
  GB_FREE(errMsg);
  return reply;
}


static int
glusterBlockCleanUp(struct glfs *glfs, char *blockname,
                    bool deleteall, bool forcedel, bool unlink, blockRemoteDeleteResp *drobj)
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

  count = glusterBlockDeleteFillArgs(info, deleteall, NULL, NULL, NULL);
  asyncret = glusterBlockDeleteRemoteAsync(blockname, info, glfs, &dobj, count,
                                           deleteall, &drobj);
  if (asyncret) {
    LOG("mgmt", GB_LOG_WARNING,
        "glusterBlockDeleteRemoteAsync: return %d %s for block %s on volume %s",
        asyncret, FAILED_REMOTE_AYNC_DELETE, blockname, info->volume);
  }

  /* delete metafile and block file */
  if (deleteall) {
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
  size_t failcnt = 0;
  size_t spent;
  size_t spare;
  size_t morereq;
  MetaInfo *info;
  static bool needcleanup = FALSE;   /* partial failure on subset of nodes */


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
    case GB_CONFIG_INPROGRESS:
    case GB_CONFIG_FAIL:
      failcnt++;
    }
  }

  /* check if mpath is satisfied */
  if (blk->mpath == successcnt) {
    LOG("mgmt", GB_LOG_INFO, "Block create request satisfied for target:"
        " %s on volume %s with given hosts %s",
          blk->block_name, blk->volume, blk->block_hosts);
    ret = 0;
    goto out;
  }

  spent = successcnt + failcnt;  /* total spent */
  spare = list->nhosts  - spent;  /* spare after spent */
  morereq = blk->mpath  - successcnt;  /* needed nodes to complete req */

  if (spare == 0) {
    LOG("mgmt", GB_LOG_WARNING,
        "No Spare nodes to create (%s): rollingback creation of target"
        " on volume %s with given hosts %s",
        blk->block_name, blk->volume, blk->block_hosts);
    glusterBlockCleanUp(glfs, blk->block_name, TRUE, FALSE, TRUE, (*reply)->obj);
    needcleanup = FALSE;   /* already clean attempted */
    ret = -1;
    goto out;
  }

  if (spare < morereq) {
    LOG("mgmt", GB_LOG_WARNING,
        "Not enough Spare nodes for (%s): rollingback creation of target"
        " on volume %s with given hosts %s",
        blk->block_name, blk->volume, blk->block_hosts);
    glusterBlockCleanUp(glfs, blk->block_name, TRUE, FALSE, TRUE, (*reply)->obj);
    needcleanup = FALSE;   /* already clean attempted */
    ret = -1;
    goto out;
  }

  /* create on spare */
  LOG("mgmt", GB_LOG_INFO,
      "Trying to serve request for (%s)  on volume %s from spare machines",
      blk->block_name, blk->volume);
  ret = glusterBlockCreateRemoteAsync(list, spent, morereq,
                                      glfs, cobj, reply);
  if (ret) {
    LOG("mgmt", GB_LOG_WARNING, "glusterBlockCreateRemoteAsync: return %d"
        " %s for block %s on volume %s with hosts %s", ret,
        FAILED_REMOTE_AYNC_CREATE, blk->block_name,
        blk->volume, blk->block_hosts);
  }
  /* we could ideally moved this into #CreateRemoteAsync fail {} */
  needcleanup = TRUE;

  ret = glusterBlockAuditRequest(glfs, blk, cobj, list, reply);
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "glusterBlockAuditRequest: return %d"
        "volume: %s hosts: %s blockname %s", ret,
        blk->volume, blk->block_hosts, blk->block_name);
  }

 out:
  if (needcleanup) {
      glusterBlockCleanUp(glfs, blk->block_name, FALSE, FALSE, TRUE, (*reply)->obj);
  }

  blockFreeMetaInfo(info);
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


blockResponse *
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
  int errCode = 0;
  char *errMsg = NULL;
  blockServerDefPtr list = NULL;


  LOG("mgmt", GB_LOG_DEBUG,
      "modify cli request, volume=%s blockname=%s authmode=%d",
      blk->volume, blk->block_name, blk->auth_mode);

  if ((GB_ALLOC(reply) < 0) || (GB_ALLOC(savereply) < 0) ||
      (GB_ALLOC (info) < 0)) {
    GB_FREE (reply);
    GB_FREE (savereply);
    GB_FREE (info);
    return NULL;
  }

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

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, ret, errMsg, nolock);
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
                            ret, errMsg, out, "PASSWORD: %s\n", passwd);
      GB_STRCPYSTATIC(mobj.passwd, passwd);
    } else {
      GB_STRCPYSTATIC(mobj.passwd, info->passwd);
    }
    mobj.auth_mode = 1;
  } else {
    GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                          ret, errMsg, out, "PASSWORD: \n");
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
                          ret, errMsg, out, "PASSWORD: \n");
    }

    /* Collect new Meta status */
    blockFreeMetaInfo(info);
    if (GB_ALLOC(info) < 0) {
      goto out;
    }
    ret = blockGetMetaInfo(glfs, blk->block_name, info, NULL);
    if (ret) {
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

  errCode = 0;

  LOG("mgmt", GB_LOG_DEBUG,
      "modify auth cli success, volume=%s blockname=%s auth=%d",
      blk->volume, blk->block_name, blk->auth_mode);

 out:
  GB_METAUNLOCK(lkfd, blk->volume, ret, errMsg);
  blockServerDefFree(list);

 nolock:
  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): for block %s on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->block_name, blk->volume, strerror(errno));
  }

 initfail:
  blockModifyCliFormatResponse (blk, &mobj, asyncret?asyncret:errCode,
                                errMsg, savereply, info, reply, rollback);
  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s", reply->out);
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
  char         *hr_size    = NULL;           /* Human Readable size */

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
      GB_ASPRINTF(&tmp3, "%s\nSKIPPED ON:%s",tmp, savereply->skipped);
    }

    GB_ASPRINTF(&reply->out, "%sRESULT: %s\n", tmp3, errCode?"FAIL":"SUCCESS");
  }
  GB_FREE(tmp);
  GB_FREE(tmp2);
  GB_FREE(tmp3);

  /*catch all*/
  if (!reply->out) {
    blockFormatErrorResponse(MODIFY_SIZE_SRV, blk->json_resp, errCode,
                             GB_DEFAULT_ERRMSG, reply);
  }

  GB_FREE(hr_size);
}


blockResponse *
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
  int errCode = 0;
  char *errMsg = NULL;
  char *cSize = NULL;
  char *rSize = NULL;
  blockServerDefPtr list = NULL;


  LOG("mgmt", GB_LOG_DEBUG,
      "modify size cli request, volume=%s blockname=%s size=%zu",
      blk->volume, blk->block_name, blk->size);

  if ((GB_ALLOC(reply) < 0) || (GB_ALLOC(savereply) < 0) ||
      (GB_ALLOC (info) < 0)) {
    GB_FREE (reply);
    GB_FREE (savereply);
    GB_FREE (info);
    return NULL;
  }

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

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, ret, errMsg, nolock);
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
    goto out;
  }

  if ((info->size > blk->size && !blk->force) || info->size == blk->size) {
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
    LOG("mgmt", GB_LOG_ERROR, "%s block: %s volume: %s file: %s size: %zu",
        FAILED_MODIFY_SIZE, mobj.block_name, mobj.volume, mobj.gbid, mobj.size);
    goto out;
  }

  asyncret = glusterBlockModifySizeRemoteAsync(info, glfs, &mobj, &savereply);
  if (asyncret) {   /* asyncret decides result is success/fail */
    errCode = asyncret;
    LOG("mgmt", GB_LOG_WARNING,
        "glusterBlockModifySizeRemoteAsync(size=%zu): return %d %s for block %s on volume %s",
        blk->size, asyncret, FAILED_REMOTE_AYNC_MODIFY, blk->block_name, info->volume);
    goto out;
  } else {
    GB_METAUPDATE_OR_GOTO(lock, glfs, mobj.block_name, mobj.volume,
                          ret, errMsg, out, "SIZE: %zu\n",  mobj.size);
  }

  errCode = 0;

 out:
  GB_METAUNLOCK(lkfd, blk->volume, ret, errMsg);
  blockServerDefFree(list);

 nolock:
  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): for block %s on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->block_name, blk->volume, strerror(errno));
  }

 initfail:
  blockModifySizeCliFormatResponse(blk, &mobj, asyncret?asyncret:errCode,
                                   errMsg, savereply, info, reply);
  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s", reply->out);
  blockFreeMetaInfo(info);

  blockRemoteRespFree(savereply);
  GB_FREE(errMsg);

  return reply;
}


void
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


blockResponse *
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


  LOG("mgmt", GB_LOG_INFO,
      "create cli request, volume=%s blockname=%s mpath=%d blockhosts=%s "
      "authmode=%d size=%lu, rbsize=%d", blk->volume, blk->block_name, blk->mpath,
      blk->block_hosts, blk->auth_mode, blk->size, blk->rb_size);

  if (GB_ALLOC(reply) < 0) {
    return NULL;
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

  GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                        errCode, errMsg, exist,
                        "SIZE: %zu\nRINGBUFFER: %d\nENTRYCREATE: SUCCESS\n",
                        blk->size, blk->rb_size);

  GB_STRCPYSTATIC(cobj.volume, blk->volume);
  GB_STRCPYSTATIC(cobj.block_name, blk->block_name);
  cobj.size = blk->size;
  cobj.rb_size = blk->rb_size;
  GB_STRCPYSTATIC(cobj.gbid, gbid);
  GB_STRDUP(cobj.block_hosts,  blk->block_hosts);
  cobj.xdata.xdata_len = strlen(gbConf->volServer);
  cobj.xdata.xdata_val = (char *) gbConf->volServer;

  if (blk->auth_mode) {
    uuid_generate(uuid);
    uuid_unparse(uuid, passwd);

    GB_STRCPYSTATIC(cobj.passwd, passwd);
    cobj.auth_mode = 1;

    GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                          errCode, errMsg, exist, "PASSWORD: %s\n", passwd);
  }

  errCode = glusterBlockCreateRemoteAsync(list, 0, blk->mpath,
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
        "volume: %s hosts: %s blockname %s", errCode,
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
  blockCreateCliFormatResponse(glfs, blk, &cobj, errCode, errMsg, savereply, reply);
  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s", reply->out);
  GB_FREE(errMsg);
  blockServerDefFree(list);
  blockCreateParsedRespFree(savereply);
  GB_FREE (cobj.block_hosts);
  GB_FREE(resultCaps);

  return reply;
}


static int
blockValidateCommandOutput(const char *out, int opt, void *data)
{
  blockCreate *cblk = data;
  blockDelete *dblk = data;
  blockModify *mblk = data;
  blockModifySize *msblk = data;
  blockReplace *rblk = data;
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
  }

out:
  return ret;
}


blockResponse *
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
                  "targetcli <<EOF\n%s delete %s ip_port=3260\n%s create %s\n%s\nEOF",
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


blockResponse *
block_create_common(blockCreate *blk, char *rbsize, char *volServer, char *prio_path)
{
  char *tmp = NULL;
  char *backstore = NULL;
  char *backstore_attr = NULL;
  char *iqn = NULL;
  char *tpg = NULL;
  char *glfs_alua = NULL;
  char *glfs_alua_type = NULL;
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
      "filename=%s authmode=%d passwd=%s size=%lu", blk->volume,
      volServer?volServer:blk->ipaddr, blk->block_name, blk->block_hosts,
      blk->gbid, blk->auth_mode, blk->auth_mode?blk->passwd:"", blk->size);

  if (GB_ALLOC(reply) < 0) {
    goto out;
  }
  reply->exit = -1;

  if (prio_path && prio_path[0]) {
    prioCap = true;
  }

  if (GB_ASPRINTF(&backstore, "%s %s name=%s size=%zu cfgstring=%s@%s%s/%s%s wwn=%s",
                  GB_TGCLI_GLFS_PATH, GB_CREATE, blk->block_name, blk->size,
                  blk->volume, volServer?volServer:blk->ipaddr, GB_STOREDIR,
                  blk->gbid, rbsize ? rbsize: "", blk->gbid) == -1) {
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
        if (GB_ASPRINTF(&exec, "%s\n%s\n%s\n%s\n%s\n%s %s\n%s\n%s\n%s %s",
            backstore, backstore_attr, glfs_alua, glfs_alua_type, iqn,
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

  if (GB_ASPRINTF(&exec, "targetcli <<EOF\n%s\n%s\nEOF", tmp, save) == -1) {
    goto out;
  }
  GB_FREE(tmp);

  if (GB_ALLOC_N(reply->out, 8192) < 0) {
    GB_FREE(reply);
    goto out;
  }

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
  GB_FREE(rbsize);
  GB_FREE(backstore_attr);
  GB_FREE(volServer);
  blockServerDefFree(list);

  return reply;
}


blockResponse *
block_create_1_svc_st(blockCreate *blk, struct svc_req *rqstp)
{
  return block_create_common(blk, NULL, NULL, NULL);
}


blockResponse *
block_create_v2_1_svc_st(blockCreate2 *blk, struct svc_req *rqstp)
{
  char *rbsize= NULL;
  blockCreate blk_v1 = {{0},};
  char *volServer = NULL;
  size_t len = blk->xdata.xdata_len;


  if (blk->rb_size) {
    GB_ASPRINTF(&rbsize, " control='%s=%d'", GB_RING_BUFFER_STR, blk->rb_size);
  }

  convertTypeCreate2ToCreate(blk, &blk_v1);

  if (len > 0 && len <= HOST_NAME_MAX) {
    if (strncmp(blk->xdata.xdata_val, "localhost", 9)) {
      if (GB_ALLOC_N(volServer, len) < 0)
        goto err;
      strncpy(volServer, blk->xdata.xdata_val, len);
    }
  }
  return block_create_common(&blk_v1, rbsize, volServer, blk->prio_path);

err:
  return NULL;
}


void
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

blockResponse *
block_delete_cli_1_svc_st(blockDeleteCli *blk, struct svc_req *rqstp)
{
  blockRemoteDeleteResp *savereply = NULL;
  MetaInfo *info = NULL;
  blockResponse *reply = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  char *errMsg = NULL;
  int errCode = 0;
  int ret;
  blockServerDefPtr list = NULL;


  LOG("mgmt", GB_LOG_INFO, "delete cli request, volume=%s blockname=%s",
                           blk->volume, blk->block_name);

  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }

  if (GB_ALLOC(savereply) < 0) {
    GB_FREE(reply);
    return NULL;
  }

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
    goto out;
  }

  ret = blockGetMetaInfo(glfs, blk->block_name, info, NULL);
  if (ret) {
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

  errCode = glusterBlockCleanUp(glfs, blk->block_name, TRUE, blk->force, blk->unlink, savereply);
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
  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): for block %s on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->block_name, blk->volume, strerror(errno));
  }


  blockDeleteCliFormatResponse(blk, errCode, errMsg, savereply, reply);
  LOG("cmdlog", ((!!errCode) ? GB_LOG_ERROR : GB_LOG_INFO), "%s", reply->out);

  if (savereply) {
    GB_FREE(savereply->d_attempt);
    GB_FREE(savereply->d_success);
    GB_FREE(savereply);
  }
  GB_FREE(errMsg);

  return reply;
}


blockResponse *
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

  if (GB_ASPRINTF(&exec, "targetcli <<EOF\n%s\n%s\nEOF", backstore, iqn) == -1) {
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


blockResponse *
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


blockResponse *
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

  if (GB_ASPRINTF(&exec, "targetcli <<EOF\n%s\n%s\nEOF", tmp, save) == -1) {
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


blockResponse *
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

  if (GB_ASPRINTF(&exec, "targetcli <<EOF\n%s\n%s\nEOF", tmp, save) == -1) {
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


blockResponse *
block_list_cli_1_svc_st(blockListCli *blk, struct svc_req *rqstp)
{
  blockResponse *reply;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  struct glfs_fd *tgmdfd = NULL;
  struct dirent *entry;
  char *tmp = NULL;
  char *filelist = NULL;
  json_object *json_obj = NULL;
  json_object *json_array = NULL;
  int errCode = 0;
  char *errMsg = NULL;


  LOG("mgmt", GB_LOG_DEBUG, "list cli request, volume=%s", blk->volume);

  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }

  if (blk->json_resp) {
    json_obj = json_object_new_object();
    json_array = json_object_new_array();
  }

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

  LOG("mgmt", GB_LOG_DEBUG, "list cli success, volume=%s", blk->volume);

  if (blk->json_resp) {
    json_object_object_add(json_obj, "blocks", json_array);
  }

 out:
  GB_METAUNLOCK(lkfd, blk->volume, errCode, errMsg);

 optfail:
  if (tgmdfd && glfs_closedir (tgmdfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "glfs_closedir(%s): on volume %s failed[%s]",
        GB_METADIR, blk->volume, strerror(errno));
  }

  if (errCode < 0) {
    errCode = GB_DEFAULT_ERRCODE;
  }
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

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "glfs_close(%s): on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->volume, strerror(errno));
  }

  GB_FREE(errMsg);

  return reply;
}


void
blockInfoCliFormatResponse(blockInfoCli *blk, int errCode,
                           char *errMsg, MetaInfo *info,
                           blockResponse *reply)
{
  json_object  *json_obj    = NULL;
  json_object  *json_array1 = NULL;
  json_object  *json_array2 = NULL;
  char         *tmp         = NULL;
  char         *tmp2        = NULL;
  char         *tmp3        = NULL;
  char         *out         = NULL;
  int          i            = 0;
  char         *hr_size     = NULL;           /* Human Readable size */

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

  for (i = 0; i < info->nhosts; i++) {
    switch (blockMetaStatusEnumParse(info->list[i]->status)) {
    case GB_RS_INPROGRESS:
    case GB_RS_FAIL:
      GB_STRDUP(hr_size, "-");
      break;
    }
    if (hr_size) {
      break;
    }
  }
  if (!hr_size) {
    hr_size = glusterBlockFormatSize("mgmt", info->size);
    if (!hr_size) {
      GB_ASPRINTF (&errMsg, "failed in glusterBlockFormatSize");
      blockFormatErrorResponse(INFO_SRV, blk->json_resp, ENOMEM,
                               errMsg, reply);
      GB_FREE(errMsg);
      goto out;
    }
  }

  if (blk->json_resp) {
    json_obj = json_object_new_object();
    json_object_object_add(json_obj, "NAME", GB_JSON_OBJ_TO_STR(blk->block_name));
    json_object_object_add(json_obj, "VOLUME", GB_JSON_OBJ_TO_STR(info->volume));
    json_object_object_add(json_obj, "GBID", GB_JSON_OBJ_TO_STR(info->gbid));
    json_object_object_add(json_obj, "SIZE", GB_JSON_OBJ_TO_STR(hr_size));
    json_object_object_add(json_obj, "HA", json_object_new_int(info->mpath));
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
    }

    json_object_object_add(json_obj, "EXPORTED ON", json_array1);
    if (json_array2) {
      json_object_object_add(json_obj, "ENCOUNTERED FAILURES ON", json_array2);
    }

    GB_ASPRINTF(&reply->out, "%s\n",
                json_object_to_json_string_ext(json_obj,
                                mapJsonFlagToJsonCstring(blk->json_resp)));
    json_object_put(json_obj);
  } else {
    if (GB_ASPRINTF(&tmp, "NAME: %s\nVOLUME: %s\nGBID: %s\nSIZE: %s\n"
                    "HA: %zu\nPASSWORD: %s\nEXPORTED ON:",
                    blk->block_name, info->volume, info->gbid, hr_size,
                    info->mpath, info->passwd) == -1) {
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
    }

    if (tmp2) {
      tmp3 = tmp;
      if (GB_ASPRINTF(&tmp, "%s\nENCOUNTERED FAILURES ON:%s\n", tmp3, tmp2) == -1) {
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
  GB_FREE(hr_size);
  GB_FREE (tmp);
  GB_FREE (tmp2);
  GB_FREE (tmp3);
  return;
}

blockResponse *
block_info_cli_1_svc_st(blockInfoCli *blk, struct svc_req *rqstp)
{
  blockResponse *reply;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  MetaInfo *info = NULL;
  int ret = -1;
  int errCode = 0;
  char *errMsg = NULL;


  LOG("mgmt", GB_LOG_DEBUG,
      "info cli request, volume=%s blockname=%s", blk->volume, blk->block_name);

  if ((GB_ALLOC(reply) < 0) || (GB_ALLOC(info) < 0)) {
    GB_FREE (reply);
    GB_FREE (info);
    return NULL;
  }

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

  ret = blockGetMetaInfo(glfs, blk->block_name, info, &errCode);
  if (ret) {
    if (errCode == ENOENT) {
      GB_ASPRINTF (&errMsg, "block %s/%s doesn't exist", blk->volume,
                   blk->block_name);
    } else {
      GB_ASPRINTF (&errMsg, "Not able to get metadata information for %s/%s[%s]",
                   blk->volume, blk->block_name, strerror(errCode));
    }
    goto out;
  }

  LOG("mgmt", GB_LOG_DEBUG,
      "info cli success, volume=%s blockname=%s", blk->volume, blk->block_name);

 out:
  GB_METAUNLOCK(lkfd, blk->volume, ret, errMsg);

 optfail:
  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): on volume %s for block %s failed[%s]",
        GB_TXLOCKFILE, blk->volume, blk->block_name, strerror(errno));
  }


  blockInfoCliFormatResponse(blk, errCode, errMsg, info, reply);
  GB_FREE(errMsg);
  blockFreeMetaInfo(info);

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
block_delete_1_svc(blockDelete *blk, blockResponse *reply, struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(delete, blk, reply, rqstp, ret);
  return ret;
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
block_version_1_svc(void *data, blockResponse *reply, struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(version, data, reply, rqstp, ret);
  return ret;
}


bool_t
block_replace_1_svc(blockReplace *blk, blockResponse *reply, struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(replace, blk, reply, rqstp, ret);
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


bool_t
block_replace_cli_1_svc(blockReplaceCli *blk, blockResponse *reply,
                        struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(replace_cli, blk, reply, rqstp, ret);
  return ret;
}

bool_t
block_gen_config_cli_1_svc(blockGenConfigCli *blk, blockResponse *reply,
                      struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(gen_config_cli, blk, reply, rqstp, ret);
  return ret;
}

bool_t
block_list_cli_1_svc(blockListCli *blk, blockResponse *reply,
                     struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(list_cli, blk, reply, rqstp, ret);
  return ret;
}


bool_t
block_info_cli_1_svc(blockInfoCli *blk, blockResponse *reply,
                     struct svc_req *rqstp)
{
  int ret;

  GB_RPC_CALL(info_cli, blk, reply, rqstp, ret);
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
