/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "common.h"
# include  "glfs-operations.h"

# include  <pthread.h>
# include  <netdb.h>
# include  <uuid/uuid.h>
# include  <json-c/json.h>


# define   UUID_BUF_SIZE     38
# define   GB_DEFAULT_ERRCODE 255

# define   GB_CREATE            "create"
# define   GB_DELETE            "delete"
# define   GB_MSERVER_DELIMITER ","

# define   GB_TGCLI_GLFS_PATH   "/backstores/user:glfs"
# define   GB_TGCLI_GLFS        "targetcli " GB_TGCLI_GLFS_PATH
# define   GB_TGCLI_GLFS_CHECK  GB_TGCLI_GLFS " ls > " DEVNULLPATH
# define   GB_TGCLI_CHECK       GB_TGCLI_GLFS " ls | grep ' %s ' > " DEVNULLPATH
# define   GB_TGCLI_ISCSI       "targetcli /iscsi"
# define   GB_TGCLI_GLOBALS     "targetcli set "                        \
                                "global auto_add_default_portal=false " \
                                "auto_enable_tpgt=false "               \
                                "logfile=" CONFIGSHELL_LOG_FILE " > " DEVNULLPATH
# define   GB_TGCLI_SAVE        "targetcli / saveconfig > " DEVNULLPATH
# define   GB_TGCLI_ATTRIBUTES  "generate_node_acls=1 demo_mode_write_protect=0 > " DEVNULLPATH
# define   GB_TGCLI_IQN_PREFIX  "iqn.2016-12.org.gluster-block:"

# define   GB_JSON_OBJ_TO_STR(x) json_object_new_string(x?x:"")
# define   GB_DEFAULT_ERRMSG    "Operation failed, please check the log "\
                                "file to find the reason."

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef enum operations {
  CREATE_SRV = 1,
  DELETE_SRV,
  MODIFY_SRV,
  LIST_SRV,
  INFO_SRV
} operations;


typedef struct blockRemoteObj {
    struct glfs *glfs;
    void *obj;
    char *volume;
    char *addr;
    char *reply;
    int  exit;
} blockRemoteObj;


typedef struct blockRemoteModifyResp {
  char *attempt;
  char *success;
  char *rb_attempt;
  char *rb_success;
} blockRemoteModifyResp;


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


typedef struct blockServerDef {
  size_t nhosts;
  char   **hosts;
} blockServerDef;
typedef blockServerDef *blockServerDefPtr;


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


  if (!temp) {
    return;
  }

  /* Allocate size for out including trailing space and \0. */
  if (GB_ALLOC_N(out, strlen(temp) + strlen(" ") + 1) < 0) {
    return;
  }

  /* Split string into tokens */
  element = strtok(temp, " ");
  while (element) {
    if (!strstr(out, element)) {
      strcat(out, element);
      strcat(out, " ");
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
  char *errMsg = NULL;


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
      if (!local->nportal) {
        if (GB_ALLOC(local->portal) < 0) {
          goto out;
        }
        if (GB_STRDUP(local->portal[local->nportal],
                      getLastWordNoDot(line)) < 0) {
          goto out;
        }
        local->nportal++;
      } else {
        if (GB_REALLOC_N(local->portal, local->nportal + 1) < 0) {
          goto out;
        }
        if (GB_STRDUP(local->portal[local->nportal],
                      getLastWordNoDot(line)) < 0) {
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
    case GB_REMOTE_CREATE_RESP_MAX:
      goto out;
    }

    line = strtok(NULL, "\n");
  }

  *savereply = local;

  return 0;

 out:
  *savereply = local;

  return -1;
}


int
glusterBlockCallRPC_1(char *host, void *cobj,
                      operations opt, bool *rpc_sent, char **out)
{
  CLIENT *clnt = NULL;
  int ret = -1;
  int sockfd;
  int errsv = 0;
  blockResponse *reply =  NULL;
  struct hostent *server;
  struct sockaddr_in sain = {0, };


  *rpc_sent = FALSE;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "socket creation failed (%s)",
        strerror (errno));
    goto out;
  }

  server = gethostbyname(host);
  if (!server) {
    LOG("mgmt", GB_LOG_ERROR, "gethostbyname(%s) failed (%s)",
        host, strerror (errno));
    goto out;
  }

  sain.sin_family = AF_INET;
  bcopy((char *)server->h_addr, (char *)&sain.sin_addr.s_addr,
        server->h_length);
  sain.sin_port = htons(GB_TCP_PORT);

  if (connect(sockfd, (struct sockaddr *) &sain, sizeof(sain)) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "connect on %s failed (%s)", host,
        strerror (errno));
    errsv = errno;
    goto out;
  }

  clnt = clnttcp_create ((struct sockaddr_in *) &sain, GLUSTER_BLOCK,
                         GLUSTER_BLOCK_VERS, &sockfd, 0, 0);
  if (!clnt) {
    LOG("mgmt", GB_LOG_ERROR, "%son inet host %s",
        clnt_spcreateerror("client create failed"), host);
    goto out;
  }

  switch(opt) {
  case CREATE_SRV:
    strcpy(((blockCreate *)cobj)->ipaddr, host);
    *rpc_sent = TRUE;

    reply = block_create_1((blockCreate *)cobj, clnt);
    if (!reply) {
      LOG("mgmt", GB_LOG_ERROR, "%son host %s",
          clnt_sperror(clnt, "block remote create failed"), host);
      goto out;
    }
    break;
  case DELETE_SRV:
    *rpc_sent = TRUE;
    reply = block_delete_1((blockDelete *)cobj, clnt);
    if (!reply) {
      LOG("mgmt", GB_LOG_ERROR, "%son host %s",
          clnt_sperror(clnt, "block remote delete failed"), host);
      goto out;
    }
    break;
  case MODIFY_SRV:
    *rpc_sent = TRUE;
    reply = block_modify_1((blockModify *)cobj, clnt);
    if (!reply) {
      LOG("mgmt", GB_LOG_ERROR, "%son host %s",
          clnt_sperror(clnt, "block remote modify failed"), host);
      goto out;
    }
    break;
  case LIST_SRV:
  case INFO_SRV:
      goto out;
  }

  if (reply) {
    if (GB_STRDUP(*out, reply->out) < 0) {
      goto out;
    }
    ret = reply->exit;
  }

 out:
  if (clnt && reply) {
    if (!clnt_freeres(clnt, (xdrproc_t)xdr_blockResponse, (char *)reply)) {
      LOG("mgmt", GB_LOG_ERROR, "%s",
          clnt_sperror(clnt, "clnt_freeres failed"));

    }
    clnt_destroy (clnt);
  }

  if (sockfd != -1) {
    close(sockfd);
  }

  if (errsv) {
    errno = errsv;
  }

  return ret;
}


static void
blockServerDefFree(blockServerDefPtr blkServers)
{
  size_t i;


  if (!blkServers) {
    return;
  }

  for (i = 0; i < blkServers->nhosts; i++) {
     GB_FREE(blkServers->hosts[i]);
  }
  GB_FREE(blkServers->hosts);
  GB_FREE(blkServers);
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

  return list;

 out:
  GB_FREE(base);
  blockServerDefFree(list);
  return NULL;
}


static bool
isRetValueDependencyError(int ret)
{
    if (ret == EKEYEXPIRED || ret == ESRCH || ret == ENODEV) {
      return TRUE;
    }

    return FALSE;
}


void *
glusterBlockCreateRemote(void *data)
{
  int ret;
  int saveret;
  blockRemoteObj *args = (blockRemoteObj *)data;
  blockCreate cobj = *(blockCreate *)args->obj;
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
    }

    if (isRetValueDependencyError(ret)) {
      LOG("mgmt", GB_LOG_ERROR, "%s [%s] hence create block %s on "
          "host %s volume %s failed", FAILED_DEPENDENCY, strerror(ret),
          cobj.block_name, args->addr, args->volume);
      goto out;
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
                            blockCreate *cobj,
                            blockRemoteCreateResp **savereply)
{
  pthread_t  *tid = NULL;
  static blockRemoteObj *args = NULL;
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
    /* this means in oneof the nodes dependency package not installed*/
    if (isRetValueDependencyError(args[i].exit)) {
      ret = args[i].exit;
      goto out; /* important to catch */
    } else if (args[i].exit) {
      ret = -1;
    }
  }

 out:
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
    }

    if (isRetValueDependencyError(ret)) {
      LOG("mgmt", GB_LOG_ERROR, "%s [%s] hence delete block %s on "
          "host %s volume %s failed", FAILED_DEPENDENCY, strerror(ret),
          dobj.block_name, args->addr, args->volume);
      goto out;
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
glusterBlockCollectAttemptSuccess(struct glfs *glfs, char *blockname,
                                  blockRemoteObj *args, size_t count,
                                  char **attempt, char **success)
{
  char *a_tmp = NULL;
  char *s_tmp = NULL;
  int i = 0;

  for (i = 0; i < count; i++) {
    if (args[i].exit) {
      if (GB_ASPRINTF(attempt, "%s %s",
                      (a_tmp==NULL?"":a_tmp), args[i].addr) == -1) {
        goto fail;
      }
      GB_FREE(a_tmp);
      a_tmp = *attempt;
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
glusterBlockDeleteRemoteAsync(MetaInfo *info,
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

  ret = glusterBlockCollectAttemptSuccess (glfs, dobj->block_name, args,
                                           count, &d_attempt, &d_success);
  if (ret) {
    goto out;
  }

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

  ret = 0;
  for (i = 0; i < count; i++) {
    if (isRetValueDependencyError(args[i].exit)) {
      ret = args[i].exit;
      break; /* important to catch */
    } else if (args[i].exit) {
      ret = -1;
    }
  }

  *savereply = local;

 out:
  GB_FREE(d_attempt);
  GB_FREE(d_success);
  GB_FREE(args);
  GB_FREE(tid);

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
    }

    if (isRetValueDependencyError(ret)) {
      LOG("mgmt", GB_LOG_ERROR, "%s [%s] hence modify block %s on "
          "host %s volume %s failed", FAILED_DEPENDENCY, strerror(ret),
          cobj.block_name, args->addr, args->volume);
      goto out;
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
        /* case GB_AUTH_ENFORCED: this is not required to be configured */
      case GB_AUTH_ENFORCE_FAIL:
      case GB_AUTH_CLEAR_ENFORCED:
        if (mobj->auth_mode) {
          fill = TRUE;
        }
        break;
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
    ret = glusterBlockCollectAttemptSuccess(glfs, mobj->block_name,
                                            args, count, &local->attempt,
                                            &local->success);
    if (ret)
            goto out;
  } else {
    /* collect return */
    ret = glusterBlockCollectAttemptSuccess(glfs, mobj->block_name, args,
                                            count, &local->rb_attempt,
                                            &local->rb_success);
    if (ret)
            goto out;
  }
  for (i = 0; i < count; i++) {
    if (isRetValueDependencyError(args[i].exit)) {
      ret = args[i].exit;
      break; /* important to catch */
    } else if (args[i].exit) {
      ret = -1;
    }
  }

  *savereply = local;

 out:
  GB_FREE(args);
  GB_FREE(tid);

  return ret;
}


static int
glusterBlockCleanUp(struct glfs *glfs, char *blockname,
                    bool deleteall, blockRemoteDeleteResp *drobj)
{
  int ret = -1;
  size_t i;
  static blockDelete dobj;
  size_t cleanupsuccess = 0;
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

  strcpy(dobj.block_name, blockname);
  strcpy(dobj.gbid, info->gbid);

  count = glusterBlockDeleteFillArgs(info, deleteall, NULL, NULL, NULL);
  asyncret = glusterBlockDeleteRemoteAsync(info, glfs, &dobj, count,
                                           deleteall, &drobj);
  if (asyncret) {
    LOG("mgmt", GB_LOG_WARNING,
        "glusterBlockDeleteRemoteAsync: return %d %s for block %s on volume %s",
        asyncret, FAILED_REMOTE_AYNC_DELETE, blockname, info->volume);

    if (isRetValueDependencyError(asyncret)) {
      goto out;
    }
  }

  /* delete metafile and block file */
  if (deleteall) {
    blockFreeMetaInfo(info);

    if (GB_ALLOC(info) < 0) {
      goto out;
    }

    ret = blockGetMetaInfo(glfs, blockname, info, NULL);
    if (ret) {
      goto out;
    }

    for (i = 0; i < info->nhosts; i++) {
      switch (blockMetaStatusEnumParse(info->list[i]->status)) {
      case GB_CONFIG_INPROGRESS:  /* un touched */
      case GB_CLEANUP_SUCCESS:
        cleanupsuccess++;
        break;
      }
    }

    if (cleanupsuccess == info->nhosts) {
      GB_METAUPDATE_OR_GOTO(lock, glfs, blockname, info->volume,
                            ret, errMsg, out, "ENTRYDELETE: INPROGRESS\n");
      if (glusterBlockDeleteEntry(glfs, info->volume, info->gbid)) {
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

  return asyncret?asyncret:ret;
}


static int
glusterBlockAuditRequest(struct glfs *glfs,
                         blockCreateCli *blk,
                         blockCreate *cobj,
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
  } else {
    spent = successcnt + failcnt;  /* total spent */
    spare = list->nhosts  - spent;  /* spare after spent */
    morereq = blk->mpath  - successcnt;  /* needed nodes to complete req */
    if (spare == 0) {
      LOG("mgmt", GB_LOG_WARNING,
          "No Spare nodes to create (%s): rollingback creation of target"
          " on volume %s with given hosts %s",
          blk->block_name, blk->volume, blk->block_hosts);
      glusterBlockCleanUp(glfs, blk->block_name, TRUE, (*reply)->obj);
      needcleanup = FALSE;   /* already clean attempted */
      ret = -1;
      goto out;
    } else if (spare < morereq) {
      LOG("mgmt", GB_LOG_WARNING,
          "Not enough Spare nodes for (%s): rollingback creation of target"
          " on volume %s with given hosts %s",
          blk->block_name, blk->volume, blk->block_hosts);
      glusterBlockCleanUp(glfs, blk->block_name, TRUE, (*reply)->obj);
      needcleanup = FALSE;   /* already clean attempted */
      ret = -1;
      goto out;
    } else {
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
    }
  }

  ret = glusterBlockAuditRequest(glfs, blk, cobj, list, reply);
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "glusterBlockAuditRequest: return %d"
        "volume: %s hosts: %s blockname %s", ret,
        blk->volume, blk->block_hosts, blk->block_name);
  }

 out:
  if (needcleanup) {
      glusterBlockCleanUp(glfs, blk->block_name, FALSE, (*reply)->obj);
  }

  blockFreeMetaInfo(info);
  return ret;
}

void
blockFormatErrorResponse (operations op, int json_resp, int errCode,
                          char *errMsg, struct blockResponse *reply)
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
      GB_ASPRINTF (&reply->out, "%s\nRESULT:FAIL\n", errMsg);
    } else {
      GB_ASPRINTF (&reply->out, "%s\n", errMsg);
    }
  }
}

static void
blockStr2arrayAddToJsonObj (json_object *json_obj, char *string, char *label,
                            json_object **json_array)
{
  char *tmp = NULL;
  json_object *json_array1 = NULL;

  if (!string)
    return;

  json_array1 = json_object_new_array();
  tmp = strtok (string, " ");
  while (tmp != NULL)
  {
    json_object_array_add(json_array1, GB_JSON_OBJ_TO_STR(tmp));
    tmp = strtok (NULL, " ");
  }
  json_object_object_add(json_obj, label, json_array1);
  *json_array = json_array1;
}


static void
blockFormatDependencyErrors(int errCode, char **errMsg)
{
  if (errCode == ESRCH) {
    GB_ASPRINTF(errMsg, "tcmu-runner is not running in few nodes");
  } else if (errCode == ENODEV) {
    GB_ASPRINTF(errMsg, "tcmu-runner running, but targetcli doesn't list "
                        "user:glfs handler in few nodes");
  } else if (errCode == EKEYEXPIRED) {
    GB_ASPRINTF(errMsg, "targetcli is not installed on few nodes");
  }
}


static void
blockModifyCliFormatResponse (blockModifyCli *blk, struct blockModify *mobj,
                              int errCode, char *errMsg,
                              blockRemoteModifyResp *savereply,
                              MetaInfo *info, struct blockResponse *reply,
                              bool rollback)
{
  json_object *json_obj = NULL;
  json_object *json_array[4] = {0};
  char        *tmp2 = NULL;
  char        *tmp3 = NULL;
  char        *tmp = NULL;
  int          i = 0;

  if (!reply) {
    return;
  }

  if (errCode < 0) {
    errCode = GB_DEFAULT_ERRCODE;
  }

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
      blockStr2arrayAddToJsonObj(json_obj, savereply->attempt, "FAILED ON",
                                 &json_array[0]);
    }

    if (savereply->success) {
      blockStr2arrayAddToJsonObj(json_obj, savereply->success,
                                 "SUCCESSFUL ON", &json_array[1]);
    }

    if (rollback) {
      if (savereply->rb_attempt) {
        blockStr2arrayAddToJsonObj(json_obj, savereply->rb_attempt,
                                   "ROLLBACK FAILED ON", &json_array[2]);
      }

      if (savereply->rb_success) {
        blockStr2arrayAddToJsonObj(json_obj, savereply->rb_success,
                                   "ROLLBACK SUCCESS ON", &json_array[3]);
      }
    }

    json_object_object_add(json_obj, "RESULT",
      errCode?GB_JSON_OBJ_TO_STR("FAIL"):GB_JSON_OBJ_TO_STR("SUCCESS"));

    GB_ASPRINTF(&reply->out, "%s\n", json_object_to_json_string_ext(json_obj,
                                     mapJsonFlagToJsonCstring(blk->json_resp)));

    for (i = 0; i < 4; i++) {
      if (json_array[i]) {
        json_object_put(json_array[i]);
      }
    }
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
block_modify_cli_1_svc(blockModifyCli *blk, struct svc_req *rqstp)
{
  int ret = -1;
  static blockModify mobj = {0};
  static blockRemoteModifyResp *savereply = NULL;
  static blockResponse *reply = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  MetaInfo *info = NULL;
  uuid_t uuid;
  char passwd[UUID_BUF_SIZE];
  int asyncret = 0;
  bool rollback = false;
  int errCode = 0;
  char *errMsg = NULL;


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

  if (glfs_access(glfs, blk->block_name, F_OK)) {
    LOG("mgmt", GB_LOG_ERROR,
        "block with name %s doesn't exist in the volume %s",
        blk->block_name, blk->volume);
    GB_ASPRINTF(&errMsg, "block %s/%s doesn't exist", blk->volume,
                blk->block_name);
    errCode = ENOENT;
    goto out;
  }

  ret = blockGetMetaInfo(glfs, blk->block_name, info, NULL);
  if (ret) {
    goto out;
  }

  strcpy(mobj.block_name, blk->block_name);
  strcpy(mobj.volume, blk->volume);
  strcpy(mobj.gbid, info->gbid);

  if (blk->auth_mode) {
    if(info->passwd[0] == '\0') {
      uuid_generate(uuid);
      uuid_unparse(uuid, passwd);
      GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                            ret, errMsg, out, "PASSWORD: %s\n", passwd);
      strcpy(mobj.passwd, passwd);
    } else {
      strcpy(mobj.passwd, info->passwd);
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
    }
  }

  ret = 0;

 out:
  blockFormatDependencyErrors(errCode, &errMsg);

  GB_METAUNLOCK(lkfd, blk->volume, ret, errMsg);

 nolock:
  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): for block %s on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->block_name, blk->volume, strerror(errno));
  }

 initfail:
  blockModifyCliFormatResponse (blk, &mobj, asyncret?asyncret:errCode,
                                errMsg, savereply, info, reply, rollback);
  blockFreeMetaInfo(info);

  if (savereply) {
    GB_FREE(savereply->attempt);
    GB_FREE(savereply->success);
    GB_FREE(savereply->rb_attempt);
    GB_FREE(savereply->rb_success);
    GB_FREE(savereply);
  }

  return reply;
}

void
blockCreateCliFormatResponse(struct glfs *glfs, blockCreateCli *blk,
                             struct blockCreate *cobj, int errCode,
                             char *errMsg, blockRemoteCreateResp *savereply,
                             struct blockResponse *reply)
{
  MetaInfo *info = NULL;
  json_object *json_obj = NULL;
  json_object *json_array1 = NULL;
  json_object *json_array2 = NULL;
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
                               savereply->errMsg, reply);
    }
    goto out;
  }

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

    json_array1 = json_object_new_array();

    for (i = 0; i < savereply->nportal; i++) {
      json_object_array_add(json_array1,
                            GB_JSON_OBJ_TO_STR(savereply->portal[i]));
    }

    json_object_object_add(json_obj, "PORTAL(S)", json_array1);

    if (savereply->obj->d_attempt || savereply->obj->d_success) {
      json_array2 = json_object_new_array();

      if (savereply->obj->d_attempt) {
        tmp = strtok (savereply->obj->d_attempt, " ");
        while (tmp!= NULL)
        {
          json_object_array_add(json_array2, GB_JSON_OBJ_TO_STR(tmp));
          tmp = strtok (NULL, " ");
        }
      }

      if (savereply->obj->d_success) {
        tmp = strtok (savereply->obj->d_success, " ");
        while (tmp!= NULL) {
          json_object_array_add(json_array2, GB_JSON_OBJ_TO_STR(tmp));
          tmp = strtok (NULL, " ");
        }
      }
      tmp = NULL;
      json_object_object_add(json_obj, "ROLLBACK ON", json_array2);
    }

    json_object_object_add(json_obj, "RESULT",
      errCode?GB_JSON_OBJ_TO_STR("FAIL"):GB_JSON_OBJ_TO_STR("SUCCESS"));

    GB_ASPRINTF(&reply->out, "%s\n",
                json_object_to_json_string_ext(json_obj,
                                     mapJsonFlagToJsonCstring(blk->json_resp)));
    json_object_put(json_array1);
    json_object_put(json_array2);
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
    if (savereply->obj->d_attempt || savereply->obj->d_success) {
      if (GB_ASPRINTF(&tmp, "ROLLBACK ON: %s %s\n",
            savereply->obj->d_attempt?savereply->obj->d_attempt:"",
            savereply->obj->d_success?savereply->obj->d_success:"") == -1) {
        goto out;
      }
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
block_create_cli_1_svc(blockCreateCli *blk, struct svc_req *rqstp)
{
  int errCode = -1;
  uuid_t uuid;
  blockRemoteCreateResp *savereply = NULL;
  char gbid[UUID_BUF_SIZE];
  char passwd[UUID_BUF_SIZE];
  struct blockCreate cobj = {0};
  struct blockResponse *reply;
  struct glfs *glfs = NULL;
  struct glfs_fd *lkfd = NULL;
  blockServerDefPtr list = NULL;
  char *errMsg = NULL;


  LOG("mgmt", GB_LOG_INFO,
      "create cli request, volume=%s blockname=%s mpath=%d blockhosts=%s "
      "authmode=%d size=%lu", blk->volume, blk->block_name, blk->mpath,
      blk->block_hosts, blk->auth_mode, blk->size);

  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }

  list = blockServerParse(blk->block_hosts);

  /* Fail if mpath > list->nhosts */
  if (blk->mpath > list->nhosts) {
    LOG("mgmt", GB_LOG_ERROR, "for block %s multipath request:%d is greater "
                              "than provided block-hosts:%s on volume %s",
         blk->block_name, blk->mpath, blk->block_hosts, blk->volume);
    if (GB_ASPRINTF(&errMsg, "multipath req: %d > block-hosts: %s\n",
                    blk->mpath, blk->block_hosts) == -1) {
      reply->exit = -1;
      goto optfail;
    }
    reply->exit = ENODEV;
    goto optfail;
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

  uuid_generate(uuid);
  uuid_unparse(uuid, gbid);

  GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                        errCode, errMsg, exist,
                        "VOLUME: %s\nGBID: %s\nSIZE: %zu\n"
                        "HA: %d\nENTRYCREATE: INPROGRESS\n",
                        blk->volume, gbid, blk->size, blk->mpath);

  if (glusterBlockCreateEntry(glfs, blk, gbid, &errCode, &errMsg)) {
    GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                          errCode, errMsg, exist, "ENTRYCREATE: FAIL\n");
    LOG("mgmt", GB_LOG_ERROR, "%s volume: %s host: %s",
        FAILED_CREATING_FILE, blk->volume, blk->block_hosts);
    goto exist;
  }

  GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                        errCode, errMsg, exist, "ENTRYCREATE: SUCCESS\n");

  strcpy(cobj.volume, blk->volume);
  strcpy(cobj.block_name, blk->block_name);
  cobj.size = blk->size;
  strcpy(cobj.gbid, gbid);
  if (GB_STRDUP(cobj.block_hosts,  blk->block_hosts) < 0) {
    errCode = ENOMEM;
    goto exist;
  }

  if (blk->auth_mode) {
    uuid_generate(uuid);
    uuid_unparse(uuid, passwd);

    strcpy(cobj.passwd, passwd);
    cobj.auth_mode = 1;

    GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                          errCode, errMsg, exist, "PASSWORD: %s\n", passwd);
  }

  errCode = glusterBlockCreateRemoteAsync(list, 0, blk->mpath,
                                          glfs, &cobj, &savereply);
  if (errCode) {
    if (isRetValueDependencyError(errCode)) {
      LOG("mgmt", GB_LOG_ERROR, "glusterBlockCreateRemoteAsync: return %d"
          " rollingback the create request for block %s on volume %s with hosts %s",
          errCode, blk->block_name, blk->volume, blk->block_hosts);

      glusterBlockCleanUp(glfs, blk->block_name, TRUE, savereply->obj);

      goto exist;
    }
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
    reply->exit = GB_DEFAULT_ERRCODE;
  }

 exist:
  blockFormatDependencyErrors(errCode, &errMsg);
  GB_METAUNLOCK(lkfd, blk->volume, errCode, errMsg);

 out:

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "glfs_close(%s): on volume %s for "
        "block %s failed[%s]", GB_TXLOCKFILE, blk->volume,
        blk->block_name, strerror(errno));
  }

 optfail:
  blockCreateCliFormatResponse(glfs, blk, &cobj, errCode, errMsg, savereply, reply);
  GB_FREE(errMsg);
  blockServerDefFree(list);
  blockCreateParsedRespFree(savereply);
  GB_FREE (cobj.block_hosts);

  return reply;
}


static int
blockNodeSanityCheck(blockResponse *reply)
{
  int ret;


  /* Check if tcmu-runner is running */
  ret = WEXITSTATUS(system("ps aux ww | grep -w '[t]cmu-runner' > /dev/null"));
  if (ret == 1) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "tcmu-runner not running");
    reply->exit = ESRCH;
    if (GB_ASPRINTF(&reply->out, "tcmu-runner not running") == -1) {
      return -1;
    }
    return -1;
  }

  /* Check targetcli has user:glfs handler listed */
  ret = WEXITSTATUS(system(GB_TGCLI_GLFS_CHECK));
  if (ret == 1) {
    LOG("mgmt", GB_LOG_ERROR, "%s",
        "tcmu-runner running, but targetcli doesn't list user:glfs handler");
    reply->exit = ENODEV;
    if (GB_ASPRINTF(&reply->out,
                    "tcmu-runner running, but targetcli doesn't list "
                    "user:glfs handler") == -1) {
      return -1;
    }
    return -1;
  }

  if (ret == EKEYEXPIRED) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "targetcli not found");
    reply->exit = EKEYEXPIRED;
    if (GB_ASPRINTF(&reply->out, "targetcli not found") == -1) {
      return -1;
    }
    return -1;
  }

  return 0;
}


blockResponse *
block_create_1_svc(blockCreate *blk, struct svc_req *rqstp)
{
  FILE *fp;
  char *tmp = NULL;
  char *backstore = NULL;
  char *iqn = NULL;
  char *tpg = NULL;
  char *lun = NULL;
  char *portal = NULL;
  char *attr = NULL;
  char *authcred = NULL;
  char *exec = NULL;
  blockResponse *reply = NULL;
  blockServerDefPtr list = NULL;
  size_t i;


  LOG("mgmt", GB_LOG_INFO,
      "create request, volume=%s blockname=%s blockhosts=%s filename=%s authmode=%d "
      "passwd=%s size=%lu", blk->volume, blk->block_name, blk->block_hosts,
      blk->gbid, blk->auth_mode, blk->auth_mode?blk->passwd:"", blk->size);

  if (GB_ALLOC(reply) < 0) {
    goto out;
  }
  reply->exit = -1;

  if (blockNodeSanityCheck(reply)) {
    goto out;
  }

  if (GB_ASPRINTF(&backstore, "%s %s %s %zu %s@%s%s/%s %s", GB_TGCLI_GLFS,
                  GB_CREATE, blk->block_name, blk->size, blk->volume,
                  blk->ipaddr, GB_STOREDIR, blk->gbid, blk->gbid) == -1) {
    goto out;
  }

  if (GB_ASPRINTF(&iqn, "%s %s %s%s", GB_TGCLI_ISCSI, GB_CREATE,
                  GB_TGCLI_IQN_PREFIX, blk->gbid) == -1) {
    goto out;
  }

  list = blockServerParse(blk->block_hosts);

  /* i = 2; because tpg1 is created by default while iqn create */
  for (i = 2; i <= list->nhosts; i++) {
    if (!tmp) {
      if (GB_ASPRINTF(&tpg, "%s/%s%s create tpg%zu > %s && ",
                   GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid, i, DEVNULLPATH) == -1) {
        goto out;
      }
      tmp = tpg;
    } else {
      if (GB_ASPRINTF(&tpg, "%s %s/%s%s create tpg%zu > %s && ", tmp,
                   GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid, i, DEVNULLPATH) == -1) {
        goto out;
      }
      GB_FREE(tmp);
      tmp = tpg;
    }
  }
  tmp = NULL;

  for (i = 1; i <= list->nhosts; i++) {
    if (GB_ASPRINTF(&lun, "%s/%s%s/tpg%zu/luns %s %s/%s > %s",  GB_TGCLI_ISCSI,
                 GB_TGCLI_IQN_PREFIX, blk->gbid, i, GB_CREATE,
                 GB_TGCLI_GLFS_PATH, blk->block_name, DEVNULLPATH) == -1) {
      goto out;
    }

    if (!strcmp(blk->ipaddr, list->hosts[i-1])) {
      if (GB_ASPRINTF(&attr, "%s/%s%s/tpg%zu enable > %s && %s/%s%s/tpg%zu set attribute %s %s",
                   GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid, i, DEVNULLPATH,
                   GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
                   blk->auth_mode?"authentication=1":"", GB_TGCLI_ATTRIBUTES) == -1) {
        goto out;
      }
      if (GB_ASPRINTF(&portal, "%s/%s%s/tpg%zu/portals create %s ",
                   GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
                   blk->ipaddr) == -1) {
        goto out;
      }
    } else {
      if (GB_ASPRINTF(&attr, "%s/%s%s/tpg%zu set attribute tpg_enabled_sendtargets=0 %s %s > %s",
                   GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
                   blk->auth_mode?"authentication=1":"",
                   GB_TGCLI_ATTRIBUTES, DEVNULLPATH) == -1) {
        goto out;
      }
      if (GB_ASPRINTF(&portal, "%s/%s%s/tpg%zu/portals create %s > %s",
                   GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
                   list->hosts[i-1], DEVNULLPATH) == -1) {
        goto out;
      }
    }

    if (blk->auth_mode &&
        GB_ASPRINTF(&authcred, "&& %s/%s%s/tpg%zu set auth userid=%s password=%s > %s",
          GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
          blk->gbid, blk->passwd, DEVNULLPATH) == -1) {
      goto out;
    }
    if (!tmp) {
      if (GB_ASPRINTF(&exec, "%s && %s && %s && %s %s && %s && %s %s && %s",
          GB_TGCLI_GLOBALS, backstore, iqn, tpg?tpg:"", lun, portal, attr,
          blk->auth_mode?authcred:"", GB_TGCLI_SAVE) == -1) {
        goto out;
      }
      tmp = exec;
    } else {
      if (GB_ASPRINTF(&exec, "%s && %s && %s && %s %s && %s",
          tmp, lun, portal, attr,
          blk->auth_mode?authcred:"", GB_TGCLI_SAVE) == -1) {
        goto out;
      }
      GB_FREE(tmp);
      tmp = exec;
    }
    GB_FREE(authcred);
    GB_FREE(attr);
    GB_FREE(portal);
    GB_FREE(lun);
  }

  LOG("mgmt", GB_LOG_DEBUG, "command, %s", exec);

  if (GB_ALLOC_N(reply->out, 4096) < 0) {
    GB_FREE(reply);
    goto out;
  }

  fp = popen(exec, "r");
  if (fp) {
    size_t newLen = fread(reply->out, sizeof(char), 4096, fp);
    if (ferror( fp ) != 0) {
      LOG("mgmt", GB_LOG_ERROR,
          "reading command %s output for block %s on volume %s failed",
          exec, blk->block_name, blk->volume);
    } else {
      reply->out[newLen++] = '\0';
    }
    reply->exit = WEXITSTATUS(pclose(fp));
  } else {
      LOG("mgmt", GB_LOG_ERROR,
          "popen(): for block %s on volume %s executing command %s failed(%s)",
          blk->block_name, blk->volume, exec, strerror(errno));
  }

  LOG("mgmt", GB_LOG_DEBUG, "raw output, %s", reply->out);
  LOG("mgmt", GB_LOG_INFO, "command exit code, %d", reply->exit);

 out:
  GB_FREE(exec);
  GB_FREE(authcred);
  GB_FREE(attr);
  GB_FREE(portal);
  GB_FREE(lun);
  GB_FREE(tpg);
  GB_FREE(iqn);
  GB_FREE(backstore);
  blockServerDefFree(list);

  return reply;
}

void
blockDeleteCliFormatResponse(blockDeleteCli *blk, int errCode, char *errMsg,
                             blockRemoteDeleteResp *savereply,
                             struct blockResponse *reply)
{
  json_object *json_obj = NULL;
  json_object *json_array1 = NULL;
  json_object *json_array2 = NULL;
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

    blockStr2arrayAddToJsonObj (json_obj, savereply->d_attempt,
                                "FAILED ON", &json_array1);
    blockStr2arrayAddToJsonObj (json_obj, savereply->d_success,
                                "SUCCESSFUL ON", &json_array2);

    json_object_object_add(json_obj, "RESULT",
        errCode?GB_JSON_OBJ_TO_STR("FAIL"):GB_JSON_OBJ_TO_STR("SUCCESS"));

    GB_ASPRINTF(&reply->out, "%s\n",
                json_object_to_json_string_ext(json_obj,
                                     mapJsonFlagToJsonCstring(blk->json_resp)));

    if (json_array1)
      json_object_put(json_array1);
    if (json_array2)
      json_object_put(json_array2);
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
block_delete_cli_1_svc(blockDeleteCli *blk, struct svc_req *rqstp)
{
  blockRemoteDeleteResp *savereply = NULL;
  static blockResponse *reply = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  char *errMsg = NULL;
  int errCode = 0;


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

  if (glfs_access(glfs, blk->block_name, F_OK)) {
    LOG("mgmt", GB_LOG_ERROR,
        "block with name %s doesn't exist in the volume %s",
        blk->block_name, blk->volume);
    GB_ASPRINTF(&errMsg, "block %s/%s doesn't exist", blk->volume,
                blk->block_name);
    errCode = ENOENT;
    goto out;
  }

  errCode = glusterBlockCleanUp(glfs, blk->block_name, TRUE, savereply);
  if (errCode) {
    LOG("mgmt", GB_LOG_WARNING, "glusterBlockCleanUp: return %d "
        "on block %s for volume %s", errCode, blk->block_name, blk->volume);
  }

 out:
  blockFormatDependencyErrors(errCode, &errMsg);

  GB_METAUNLOCK(lkfd, blk->volume, errCode, errMsg);

 optfail:
  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): for block %s on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->block_name, blk->volume, strerror(errno));
  }


  blockDeleteCliFormatResponse(blk, errCode, errMsg, savereply, reply);

  if (savereply) {
    GB_FREE(savereply->d_attempt);
    GB_FREE(savereply->d_success);
    GB_FREE(savereply);
  }

  return reply;
}


blockResponse *
block_delete_1_svc(blockDelete *blk, struct svc_req *rqstp)
{
  FILE *fp;
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

  if (blockNodeSanityCheck(reply)) {
    goto out;
  }

  if (GB_ASPRINTF(&exec, GB_TGCLI_CHECK, blk->block_name) == -1) {
    goto out;
  }

  /* Check if block exist on this node ? */
  if (WEXITSTATUS(system(exec)) == 1) {
    reply->exit = 0;
    if (GB_ASPRINTF(&reply->out, "No %s.", blk->block_name) == -1) {
      goto out;
    }
    goto out;
  }
  GB_FREE(exec);

  if (GB_ASPRINTF(&iqn, "%s %s %s%s", GB_TGCLI_ISCSI, GB_DELETE,
                  GB_TGCLI_IQN_PREFIX, blk->gbid) == -1) {
    goto out;
  }

  if (GB_ASPRINTF(&backstore, "%s %s %s", GB_TGCLI_GLFS,
                  GB_DELETE, blk->block_name) == -1) {
    goto out;
  }

  if (GB_ASPRINTF(&exec, "%s && %s && %s", backstore, iqn,
                  GB_TGCLI_SAVE) == -1) {
    goto out;
  }

  LOG("mgmt", GB_LOG_DEBUG, "command, %s", exec);

  if (GB_ALLOC_N(reply->out, 4096) < 0) {
    GB_FREE(reply);
    goto out;
  }

  fp = popen(exec, "r");
  if (fp) {
    size_t newLen = fread(reply->out, sizeof(char), 4096, fp);
    if (ferror( fp ) != 0) {
      LOG("mgmt", GB_LOG_ERROR,
          "reading command %s output for block %s failed",
          exec, blk->block_name);
    } else {
      reply->out[newLen++] = '\0';
    }
    reply->exit = WEXITSTATUS(pclose(fp));
  } else {
      LOG("mgmt", GB_LOG_ERROR,
          "popen(): for block %s executing command %s failed(%s)",
          blk->block_name, exec, strerror(errno));
  }

  LOG("mgmt", GB_LOG_DEBUG, "raw output, %s", reply->out);
  LOG("mgmt", GB_LOG_INFO, "command exit code, %d", reply->exit);

 out:
  GB_FREE(exec);
  GB_FREE(backstore);
  GB_FREE(iqn);

  return reply;
}


blockResponse *
block_modify_1_svc(blockModify *blk, struct svc_req *rqstp)
{
  FILE *fp;
  int ret;
  char *authattr = NULL;
  char *authcred = NULL;
  char *exec = NULL;
  blockResponse *reply = NULL;
  char out[1024] = {0, };
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

  if (blockNodeSanityCheck(reply)) {
    goto out;
  }

  if (GB_ASPRINTF(&exec, GB_TGCLI_CHECK, blk->block_name) == -1) {
    goto out;
  }

  /* Check if block exist on this node ? */
  if (WEXITSTATUS(system(exec)) == 1) {
    reply->exit = 0;
    if (GB_ASPRINTF(&reply->out, "No %s.", blk->block_name) == -1) {
      goto out;
    }
    goto out;
  }
  GB_FREE(exec);

  if (GB_ASPRINTF(&exec, "%s/%s%s status", GB_TGCLI_ISCSI,
                      GB_TGCLI_IQN_PREFIX, blk->gbid) == -1) {
    goto out;
  }

  /* get number of tpg's for this target */
  fp = popen(exec, "r");
  if (fp) {
    size_t newLen = fread(out, sizeof(char), 1024, fp);
    if (ferror( fp ) != 0) {
      LOG("mgmt", GB_LOG_ERROR,
          "reading command %s output for block %s on volume %s failed",
          exec, blk->block_name, blk->volume);
    } else {
      out[newLen++] = '\0';
    }
    ret = WEXITSTATUS(pclose(fp));
    if (ret) {
      LOG("mgmt", GB_LOG_ERROR,
          "reading command %s output for block %s on volume %s failed",
          exec, blk->block_name, blk->volume);
      reply->exit = ret;
       goto out;
     }
   } else {
      LOG("mgmt", GB_LOG_ERROR,
          "popen(): for block %s on volume %s executing command %s failed(%s)",
          blk->block_name, blk->volume, exec, strerror(errno));
  }

  /* out looks like, "Status for /iscsi/iqn.abc:xyz: TPGs: 2" */
  tmp = strrchr(out, ':');
  if (tmp) {
    sscanf(tmp+1, "%zu", &tpgs);
    tmp = NULL;
  }

  for (i = 1; i <= tpgs; i++) {
    if (blk->auth_mode) {  /* set auth */
      if (GB_ASPRINTF(&authattr, "%s/%s%s/tpg%zu set attribute authentication=1",
                   GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid, i) == -1) {
        goto out;
      }

      if (GB_ASPRINTF(&authcred, "%s/%s%s/tpg%zu set auth userid=%s password=%s",
                   GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid, i,
                   blk->gbid, blk->passwd) == -1) {
        goto out;
      }

      if (!tmp) {
        if (GB_ASPRINTF(&exec, "%s && %s", authattr, authcred) == -1) {
          goto out;
        }
        tmp = exec;
      } else {   /* append next series of commands */
        if (GB_ASPRINTF(&exec, "%s && %s && %s", tmp, authattr, authcred) == -1) {
          goto out;
        }
        GB_FREE(tmp);
        tmp = exec;
      }
    } else {      /* unset auth */
      if (!tmp) {
        if (GB_ASPRINTF(&exec, "%s/%s%s/tpg%zu set attribute authentication=0",
                     GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid, i) == -1) {
          goto out;
        }
        tmp = exec;
      } else {   /* append next series of commands */
        if (GB_ASPRINTF(&exec, "%s && %s/%s%s/tpg%zu set attribute authentication=0",
                     tmp, GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid, i) == -1) {
          goto out;
        }
        GB_FREE(tmp);
        tmp = exec;
      }
    }
  }

  if (GB_ASPRINTF(&exec, "%s && %s", tmp, GB_TGCLI_SAVE) == -1) {
    goto out;
  }

  LOG("mgmt", GB_LOG_DEBUG, "command, %s", exec);


  if (GB_ALLOC_N(reply->out, 4096) < 0) {
    GB_FREE(reply);
    goto out;
  }

  ret = WEXITSTATUS(system(exec));
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR,
        "system(): for block %s executing command %s failed(%s)",
        blk->block_name, exec, strerror(errno));
    reply->exit = ret;
    if (GB_ASPRINTF(&reply->out,
                    "cannot execute auth commands.") == -1) {
      goto out;
    }
    goto out;
  }

  /* command execution success */
  reply->exit = 0;

  LOG("mgmt", GB_LOG_DEBUG, "raw output, %s", reply->out);
  LOG("mgmt", GB_LOG_INFO, "command exit code, %d", reply->exit);

 out:
  GB_FREE(tmp);
  GB_FREE(exec);
  GB_FREE(authattr);
  GB_FREE(authcred);

  return reply;
}


blockResponse *
block_list_cli_1_svc(blockListCli *blk, struct svc_req *rqstp)
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
    errCode = errno;
    GB_ASPRINTF (&errMsg, "Not able to open metadata directory for volume "
                 "%s[%s]", blk->volume, strerror(errCode));
    LOG("mgmt", GB_LOG_ERROR, "glfs_opendir(%s): on volume %s failed[%s]",
        GB_METADIR, blk->volume, strerror(errno));
    goto out;
  }

  while ((entry = glfs_readdir (tgmdfd))) {
    if (strcmp(entry->d_name, ".") &&
       strcmp(entry->d_name, "..") &&
       strcmp(entry->d_name, "meta.lock")) {
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

  errCode = reply->exit;
  if (blk->json_resp) {
    if (errCode) {
      json_object_object_add(json_obj, "RESULT",
                             GB_JSON_OBJ_TO_STR("FAIL"));
      json_object_object_add(json_obj, "errCode",
                             json_object_new_int(errCode));
      json_object_object_add(json_obj, "errMsg",
                             GB_JSON_OBJ_TO_STR(errMsg));
    } else {
            json_object_object_add(json_obj, "RESULT",
                                   GB_JSON_OBJ_TO_STR("SUCCESS"));
    }
    GB_ASPRINTF(&reply->out, "%s\n",
                json_object_to_json_string_ext(json_obj,
                                mapJsonFlagToJsonCstring(blk->json_resp)));
    json_object_put(json_array);
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

  return reply;
}

static bool
blockhostIsValid (char *status)
{
  switch (blockMetaStatusEnumParse(status)) {
  case GB_CONFIG_SUCCESS:
  case GB_CLEANUP_INPROGRESS:
  case GB_AUTH_ENFORCEING:
  case GB_AUTH_ENFORCED:
  case GB_AUTH_ENFORCE_FAIL:
  case GB_AUTH_CLEAR_ENFORCED:
  case GB_AUTH_CLEAR_ENFORCEING:
  case GB_AUTH_CLEAR_ENFORCE_FAIL:
    return TRUE;
  }

  return FALSE;
}

void
blockInfoCliFormatResponse(blockInfoCli *blk, int errCode,
                           char *errMsg, MetaInfo *info,
                           struct blockResponse *reply)
{
  json_object  *json_obj   = NULL;
  json_object  *json_array = NULL;
  char         *tmp        = NULL;
  char         *out        = NULL;
  int          i           = 0;

  if (!reply) {
    return;
  }

  if (errCode < 0) {
    errCode = GB_DEFAULT_ERRCODE;
  }
  if (errMsg) {
    blockFormatErrorResponse(INFO_SRV, blk->json_resp, errCode,
                             errMsg, reply);
    return;
  }

  if (!info) {
    return;
  }

  if (blk->json_resp) {
    json_obj = json_object_new_object();
    json_object_object_add(json_obj, "NAME", GB_JSON_OBJ_TO_STR(blk->block_name));
    json_object_object_add(json_obj, "VOLUME", GB_JSON_OBJ_TO_STR(info->volume));
    json_object_object_add(json_obj, "GBID", GB_JSON_OBJ_TO_STR(info->gbid));
    json_object_object_add(json_obj, "SIZE", json_object_new_int64(info->size));
    json_object_object_add(json_obj, "HA", json_object_new_int(info->mpath));
    json_object_object_add(json_obj, "USER", GB_JSON_OBJ_TO_STR(info->userid));
    json_object_object_add(json_obj, "PASSWORD", GB_JSON_OBJ_TO_STR(info->passwd));

    json_array = json_object_new_array();

    for (i = 0; i < info->nhosts; i++) {
      if (blockhostIsValid (info->list[i]->status)) {
        json_object_array_add(json_array, GB_JSON_OBJ_TO_STR(info->list[i]->addr));
      }
    }

    json_object_object_add(json_obj, "BLOCK CONFIG NODE(S)", json_array);

    GB_ASPRINTF(&reply->out, "%s\n",
                json_object_to_json_string_ext(json_obj,
                                mapJsonFlagToJsonCstring(blk->json_resp)));
    json_object_put(json_array);
    json_object_put(json_obj);
  } else {
    if (GB_ASPRINTF(&tmp, "NAME: %s\nVOLUME: %s\nGBID: %s\nSIZE: %zu\n"
                    "HA: %zu\nUSER: %s\nPASSWORD: %s\nBLOCK CONFIG NODE(S):",
          blk->block_name, info->volume, info->gbid, info->size, info->mpath,
          info->userid, info->passwd) == -1) {
      goto out;
    }
    for (i = 0; i < info->nhosts; i++) {
        if (blockhostIsValid (info->list[i]->status)) {
          if (GB_ASPRINTF(&out, "%s %s", tmp, info->list[i]->addr) == -1) {
            GB_FREE (tmp);
            goto out;
          }
          tmp = out;
      }
    }
    if (GB_ASPRINTF(&reply->out, "%s\n", tmp) == -1) {
      GB_FREE (tmp);
      goto out;
    }
    GB_FREE (tmp);
  }
 out:
  /*catch all*/
  if (!reply->out) {
    blockFormatErrorResponse(DELETE_SRV, blk->json_resp, errCode,
                             GB_DEFAULT_ERRMSG, reply);
  }
  return;
}

blockResponse *
block_info_cli_1_svc(blockInfoCli *blk, struct svc_req *rqstp)
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
