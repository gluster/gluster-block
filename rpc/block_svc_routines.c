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


# define   UUID_BUF_SIZE     38

# define   GB_CREATE            "create"
# define   GB_DELETE            "delete"
# define   GB_MSERVER_DELIMITER ","

# define   GB_TGCLI_GLFS_PATH   "/backstores/user:glfs"
# define   GB_TGCLI_GLFS        "targetcli " GB_TGCLI_GLFS_PATH
# define   GB_TGCLI_GLFS_CHECK  GB_TGCLI_GLFS " ls > " DEVNULLPATH
# define   GB_TGCLI_CHECK       GB_TGCLI_GLFS " ls | grep ' %s ' > " DEVNULLPATH
# define   GB_TGCLI_ISCSI       "targetcli /iscsi"
# define   GB_TGCLI_GLOBALS     "targetcli set global auto_add_default_portal=false > " DEVNULLPATH
# define   GB_TGCLI_SAVE        "targetcli / saveconfig > " DEVNULLPATH
# define   GB_TGCLI_ATTRIBUTES  "generate_node_acls=1 demo_mode_write_protect=0 > " DEVNULLPATH
# define   GB_TGCLI_IQN_PREFIX  "iqn.2016-12.org.gluster-block:"



pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct blockRemoteObj {
    struct glfs *glfs;
    void *obj;
    char *volume;
    char *addr;
    char *reply;
    int  exit;
} blockRemoteObj;


typedef struct blockRemoteDeleteResp {
  char *d_attempt;
  char *d_success;
} blockRemoteDeleteResp;


typedef struct blockRemoteCreateResp {
  char *backend_size;
  char *iqn;
  char *tpg_no;
  char *lun_no;
  char *port;
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


static void
blockCreateParsedRespFree(blockRemoteCreateResp *savereply)
{
  size_t i;


  if (!savereply) {
    return;
  }

  GB_FREE(savereply->backend_size);
  GB_FREE(savereply->iqn);
  GB_FREE(savereply->tpg_no);
  GB_FREE(savereply->lun_no);
  GB_FREE(savereply->port);

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
blockRemoteCreateRespParse(char *output /* create output on one node */,
                           blockRemoteCreateResp **savereply)
{
  char *line;
  blockRemoteCreateResp *local = *savereply;


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
      if (!local->tpg_no) {
        if (GB_STRDUP(local->tpg_no, getLastWordNoDot(line)) < 0) {
          goto out;
        }
      }
      break;
    case GB_LUN_NO_RESP:
      if (!local->lun_no) {
        if (GB_STRDUP(local->lun_no, getLastWordNoDot(line)) < 0) {
          goto out;
        }
      }
      break;
    case GB_IP_PORT_RESP:
      if (!local->port) {
        if (GB_STRDUP(local->port, getLastWordNoDot(line)) < 0) {
          goto out;
        }
      }
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
    case GB_FAILED_DEPEND:
      /* do nothing, this node will be mentioned in FAILED ON: while delete */
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
                      operations opt, char **out)
{
  CLIENT *clnt = NULL;
  int ret = -1;
  int sockfd;
  int errsv = 0;
  blockResponse *reply =  NULL;
  struct hostent *server;
  struct sockaddr_in sain = {0, };


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

    reply = block_create_1((blockCreate *)cobj, clnt);
    if (!reply) {
      LOG("mgmt", GB_LOG_ERROR, "%son host %s",
          clnt_sperror(clnt, "block remote create failed"), host);
      goto out;
    }
    break;
  case DELETE_SRV:
    reply = block_delete_1((blockDelete *)cobj, clnt);
    if (!reply) {
      LOG("mgmt", GB_LOG_ERROR, "%son host %s",
          clnt_sperror(clnt, "block remote delete failed"), host);
      goto out;
    }
    break;
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


void *
glusterBlockCreateRemote(void *data)
{
  int ret;
  blockRemoteObj *args = (blockRemoteObj *)data;
  blockCreate cobj = *(blockCreate *)args->obj;


  GB_METAUPDATE_OR_GOTO(lock, args->glfs, cobj.block_name, cobj.volume,
                        ret, out, "%s: CONFIGINPROGRESS\n", args->addr);

  ret = glusterBlockCallRPC_1(args->addr, &cobj, CREATE_SRV, &args->reply);
  if (ret) {
    if (errno == ENETUNREACH || errno == ECONNREFUSED  || errno == ETIMEDOUT) {
      LOG("mgmt", GB_LOG_ERROR, "%s hence %s for block %s on"
          "host %s volume %s", strerror(errno), FAILED_REMOTE_CREATE,
          cobj.block_name, args->addr, args->volume);
      goto out;
    }

    if (ret == EKEYEXPIRED) {
      LOG("mgmt", GB_LOG_ERROR, "%s [%s] hence create block %s on"
          "host %s volume %s failed", FAILED_DEPENDENCY, strerror(errno),
          cobj.block_name, args->addr, args->volume);
      goto out;
    }

    GB_METAUPDATE_OR_GOTO(lock, args->glfs, cobj.block_name, cobj.volume,
                          ret, out, "%s: CONFIGFAIL\n", args->addr);
    LOG("mgmt", GB_LOG_ERROR, "%s for block %s on host %s volume %s",
        FAILED_REMOTE_CREATE, cobj.block_name, args->addr, args->volume);
    goto out;
  }

  GB_METAUPDATE_OR_GOTO(lock, args->glfs, cobj.block_name, cobj.volume,
                        ret, out, "%s: CONFIGSUCCESS\n", args->addr);

 out:
  if (!args->reply) {
    if (asprintf(&args->reply, "failed to config on %s", args->addr) == -1) {
      ret = -1;
    }
  }
  args->exit = ret;

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
    ret = blockRemoteCreateRespParse(args[i].reply, savereply);
    if (ret) {
      goto out;
    }
  }

  ret = 0;
  for (i = 0; i < mpath; i++) {
    if (args[i].exit == EKEYEXPIRED) {
      ret = EKEYEXPIRED;
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
  blockRemoteObj *args = (blockRemoteObj *)data;
  blockDelete dobj = *(blockDelete *)args->obj;


  GB_METAUPDATE_OR_GOTO(lock, args->glfs, dobj.block_name, args->volume,
                        ret, out, "%s: CLEANUPINPROGRESS\n", args->addr);
  ret = glusterBlockCallRPC_1(args->addr, &dobj, DELETE_SRV, &args->reply);
  if (ret) {
    if (errno == ENETUNREACH || errno == ECONNREFUSED  || errno == ETIMEDOUT) {
      LOG("mgmt", GB_LOG_ERROR, "%s hence %s for block %s on"
          "host %s volume %s", strerror(errno), FAILED_REMOTE_DELETE,
          dobj.block_name, args->addr, args->volume);
      goto out;
    }

    if (ret == EKEYEXPIRED) {
      LOG("mgmt", GB_LOG_ERROR, "%s [%s] hence delete block %s on"
          "host %s volume %s failed", FAILED_DEPENDENCY, strerror(errno),
          dobj.block_name, args->addr, args->volume);
      goto out;
    }

    GB_METAUPDATE_OR_GOTO(lock, args->glfs, dobj.block_name, args->volume,
                          ret, out, "%s: CLEANUPFAIL\n", args->addr);
    LOG("mgmt", GB_LOG_ERROR, "%s for block %s on host %s volume %s",
        FAILED_REMOTE_DELETE, dobj.block_name, args->addr, args->volume);
    goto out;
  }
  GB_METAUPDATE_OR_GOTO(lock, args->glfs, dobj.block_name, args->volume,
                        ret, out, "%s: CLEANUPSUCCESS\n", args->addr);

 out:
  if (!args->reply) {
    if (asprintf(&args->reply, "failed to delete config on %s",
                 args->addr) == -1) {
      ret = -1;
    }
  }
  args->exit = ret;

  return NULL;
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
  static blockRemoteObj *args = NULL;
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

  for (i = 0, count = 0; i < info->nhosts; i++) {
    switch (blockMetaStatusEnumParse(info->list[i]->status)) {
    case GB_CLEANUP_INPROGRESS:
    case GB_CLEANUP_FAIL:
    case GB_CONFIG_FAIL:
      args[count].glfs = glfs;
      args[count].obj = (void *)dobj;
      args[count].volume = info->volume;
      args[count].addr = info->list[i]->addr;
      count++;
      break;
    }
    if (deleteall &&
        blockMetaStatusEnumParse(info->list[i]->status) == GB_CONFIG_SUCCESS) {
      args[count].glfs = glfs;
      args[count].obj = (void *)dobj;
      args[count].volume = info->volume;
      args[count].addr = info->list[i]->addr;
      count++;
    }
  }

  for (i = 0; i < count; i++) {
    pthread_create(&tid[i], NULL, glusterBlockDeleteRemote, &args[i]);
  }

  for (i = 0; i < count; i++) {
    pthread_join(tid[i], NULL);
  }

  /* collect return */
  for (i = 0; i < count; i++) {
    if (args[i].exit) {
      if (asprintf(&d_attempt, "%s %s",
                   (a_tmp==NULL?"":a_tmp), args[i].addr) == -1) {
        goto out;
      }
      GB_FREE(a_tmp);
      a_tmp = d_attempt;
    } else {
      if (asprintf(&d_success, "%s %s",
                   (s_tmp==NULL?"":s_tmp), args[i].addr) == -1) {
        goto out;
      }
      GB_FREE(s_tmp);
      s_tmp = d_success;
    }
  }

  if (d_attempt) {
    a_tmp = local->d_attempt;
    if (asprintf(&local->d_attempt, "%s %s",
                 (a_tmp==NULL?"":a_tmp), d_attempt) == -1) {
      goto out;
    }
    GB_FREE(a_tmp);
    a_tmp = local->d_attempt;
  }

  if (d_success) {
    s_tmp = local->d_success;
    if (asprintf(&local->d_success, "%s %s",
                 (s_tmp==NULL?"":s_tmp), d_success) == -1) {
      goto out;
    }
    GB_FREE(s_tmp);
    s_tmp = local->d_success;
  }

  ret = 0;
  for (i = 0; i < count; i++) {
    if (args[i].exit == EKEYEXPIRED) {
      ret = EKEYEXPIRED;
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


static int
glusterBlockCleanUp(operations opt, struct glfs *glfs, char *blockname,
                    bool deleteall, void *reply)
{
  int ret = -1;
  size_t i;
  static blockDelete dobj;
  size_t cleanupsuccess = 0;
  size_t count = 0;
  MetaInfo *info = NULL;
  blockRemoteDeleteResp *drobj;
  blockRemoteCreateResp *crobj;
  int asyncret = 0;

  switch(opt) {
  case CREATE_SRV:
    crobj = *(blockRemoteCreateResp **)reply;
    drobj = crobj->obj;
  break;
  case DELETE_SRV:
    drobj = *(blockRemoteDeleteResp **)reply;
  break;
  default:
    goto out;
  }


  if (GB_ALLOC(info) < 0) {
    goto out;
  }

  ret = blockGetMetaInfo(glfs, blockname, info);
  if (ret) {
    goto out;
  }

  strcpy(dobj.block_name, blockname);
  strcpy(dobj.gbid, info->gbid);

  for (i = 0; i < info->nhosts; i++) {
    switch (blockMetaStatusEnumParse(info->list[i]->status)) {
    case GB_CLEANUP_INPROGRESS:
    case GB_CLEANUP_FAIL:
    case GB_CONFIG_FAIL:
      count++;
      break;
    }
    if (deleteall &&
        blockMetaStatusEnumParse(info->list[i]->status) == GB_CONFIG_SUCCESS) {
      count++;
    }
  }

  asyncret = glusterBlockDeleteRemoteAsync(info, glfs, &dobj, count,
                                      deleteall, &drobj);
  if (asyncret) {
    LOG("mgmt", GB_LOG_WARNING,
        "glusterBlockDeleteRemoteAsync: return %d %s for block %s on volume %s",
        asyncret, FAILED_REMOTE_AYNC_DELETE, blockname, info->volume);
    /* No action ? */
  }

  /* delete metafile and block file */
  if (deleteall) {
    blockFreeMetaInfo(info);

    if (GB_ALLOC(info) < 0) {
      goto out;
    }

    ret = blockGetMetaInfo(glfs, blockname, info);
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
                            ret, out, "ENTRYDELETE: INPROGRESS\n");
      if (glusterBlockDeleteEntry(glfs, info->volume, info->gbid)) {
        GB_METAUPDATE_OR_GOTO(lock, glfs, blockname, info->volume,
                              ret, out, "ENTRYDELETE: FAIL\n");
        LOG("mgmt", GB_LOG_ERROR, "%s %s for block %s", FAILED_DELETING_FILE,
            info->volume, blockname);
        ret = -1;
        goto out;
      }
      GB_METAUPDATE_OR_GOTO(lock, glfs, blockname, info->volume,
                            ret, out, "ENTRYDELETE: SUCCESS\n");
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

  ret = blockGetMetaInfo(glfs, blk->block_name, info);
  if (ret) {
    goto out;
  }

  for (i = 0; i < info->nhosts; i++) {
    switch (blockMetaStatusEnumParse(info->list[i]->status)) {
    case GB_CONFIG_SUCCESS:
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
          "No Spare nodes to create (%s): rewinding creation of target"
          " on volume %s with given hosts %s",
          blk->block_name, blk->volume, blk->block_hosts);
      glusterBlockCleanUp(CREATE_SRV, glfs,
                          blk->block_name, TRUE, reply);
      needcleanup = FALSE;   /* already clean attempted */
      ret = -1;
      goto out;
    } else if (spare < morereq) {
      LOG("mgmt", GB_LOG_WARNING,
          "Not enough Spare nodes for (%s): rewinding creation of target"
          " on volume %s with given hosts %s",
          blk->block_name, blk->volume, blk->block_hosts);
      glusterBlockCleanUp(CREATE_SRV, glfs,
                          blk->block_name, TRUE, reply);
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
      glusterBlockCleanUp(CREATE_SRV, glfs,
                          blk->block_name, FALSE, reply);
  }

  blockFreeMetaInfo(info);
  return ret;
}


blockResponse *
block_create_cli_1_svc(blockCreateCli *blk, struct svc_req *rqstp)
{
  int ret = -1;
  size_t i;
  uuid_t uuid;
  blockRemoteCreateResp *savereply = NULL;
  char gbid[UUID_BUF_SIZE];
  static blockCreate cobj;
  static blockResponse *reply;
  struct glfs *glfs = NULL;
  struct glfs_fd *lkfd = NULL;
  blockServerDefPtr list = NULL;
  char *tmp = NULL;
  char *portals = NULL;


  if (GB_ALLOC(reply) < 0) {
    goto out;
  }

  list = blockServerParse(blk->block_hosts);

  /* Fail if mpath > list->nhosts */
  if (blk->mpath > list->nhosts) {
    LOG("mgmt", GB_LOG_ERROR, "for block %s multipath request:%d is greater "
                              "than provided block-hosts:%s on volume %s",
         blk->block_name, blk->mpath, blk->block_hosts, blk->volume);
    if (asprintf(&reply->out, "multipath req: %d > block-hosts: %s\n",
                 blk->mpath, blk->block_hosts) == -1) {
      reply->exit = -1;
      goto optfail;
    }
    reply->exit = ENODEV;
    goto optfail;
  }

  glfs = glusterBlockVolumeInit(blk->volume);
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockVolumeInit(%s) for block %s with hosts %s failed",
        blk->volume, blk->block_name, blk->block_hosts);
    goto optfail;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s %s for block %s with hosts %s",
        FAILED_CREATING_META, blk->volume, blk->block_name, blk->block_hosts);
    goto optfail;
  }

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, ret, out);

  if (!glfs_access(glfs, blk->block_name, F_OK)) {
    LOG("mgmt", GB_LOG_ERROR,
        "block with name %s already exist in the volume %s",
        blk->block_name, blk->volume);
    if (asprintf(&reply->out, "BLOCK with name: '%s' already EXIST\n",
                 blk->block_name) == -1) {
      ret = -1;
      goto exist;
    }
    ret = EEXIST;
    goto exist;
  }

  uuid_generate(uuid);
  uuid_unparse(uuid, gbid);

  GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                        ret, exist, "VOLUME: %s\nGBID: %s\nSIZE: %zu\n"
                        "HA: %d\nENTRYCREATE: INPROGRESS\n",
                        blk->volume, gbid, blk->size, blk->mpath);

  ret = glusterBlockCreateEntry(glfs, blk, gbid);
  if (ret) {
    GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                          ret, exist, "ENTRYCREATE: FAIL\n");
    LOG("mgmt", GB_LOG_ERROR, "%s volume: %s host: %s",
        FAILED_CREATING_FILE, blk->volume, blk->block_hosts);
    goto exist;
  }

  GB_METAUPDATE_OR_GOTO(lock, glfs, blk->block_name, blk->volume,
                        ret, exist, "ENTRYCREATE: SUCCESS\n");

  strcpy(cobj.volume, blk->volume);
  strcpy(cobj.block_name, blk->block_name);
  cobj.size = blk->size;
  strcpy(cobj.gbid, gbid);

  ret = glusterBlockCreateRemoteAsync(list, 0, blk->mpath,
                                      glfs, &cobj, &savereply);
  if (ret) {
    if (ret == EKEYEXPIRED) {
      LOG("mgmt", GB_LOG_ERROR, "glusterBlockCreateRemoteAsync: return %d"
          " rewinding the create request for block %s on volume %s with hosts %s",
          ret, blk->block_name, blk->volume, blk->block_hosts);

      glusterBlockCleanUp(CREATE_SRV, glfs, blk->block_name, TRUE, &savereply);

      goto exist;
    }
    LOG("mgmt", GB_LOG_WARNING, "glusterBlockCreateRemoteAsync: return %d"
        " %s for block %s on volume %s with hosts %s", ret,
        FAILED_REMOTE_AYNC_CREATE, blk->block_name,
        blk->volume, blk->block_hosts);
  }

  /* Check Point */
  ret = glusterBlockAuditRequest(glfs, blk, &cobj, list, &savereply);
  if (ret) {
    LOG("mgmt", GB_LOG_ERROR, "glusterBlockAuditRequest: return %d"
        "volume: %s hosts: %s blockname %s", ret,
        blk->volume, blk->block_hosts, blk->block_name);
  }

  for (i = 0; i < savereply->nportal; i++) {
    if (asprintf(&portals, "%s %s",
                 tmp!=NULL?tmp:"", savereply->portal[i]) == -1) {
      portals = tmp;
      goto exist;
    }
    GB_FREE(tmp);
    tmp = portals;
  }

  /* save 'failed on'*/
  tmp = NULL;
  if (savereply->obj->d_attempt || savereply->obj->d_success) {
    if (asprintf(&tmp, "REWIND ON: %s %s\n",
                 savereply->obj->d_attempt?savereply->obj->d_attempt:"",
                 savereply->obj->d_success?savereply->obj->d_success:""
                ) == -1) {
      ret = -1;
      goto exist;
    }
  }

  if (asprintf(&reply->out,
               "IQN: %s\nPORTAL(S): %s\n%sRESULT: %s\n",
               savereply->iqn, portals, tmp?tmp:"",
               ret?"FAIL":"SUCCESS") == -1) {
    ret = -1;
    goto exist;
  }

 exist:
  if (ret == EKEYEXPIRED) {
    if (asprintf(&reply->out,
                 "Looks like targetcli and tcmu-runner are not installed on few nodes.\n"
                 "RESULT:FAIL\n") == -1) {
      ret = -1;
    }
  }
  GB_METAUNLOCK(lkfd, blk->volume, ret);

 out:
  reply->exit = ret;

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "glfs_close(%s): on volume %s for "
        "block %s failed[%s]", GB_TXLOCKFILE, blk->volume,
        blk->block_name, strerror(errno));
  }

  glfs_fini(glfs);
  blockCreateParsedRespFree(savereply);
  GB_FREE(tmp);

 optfail:
  blockServerDefFree(list);

  return reply;
}


blockResponse *
block_create_1_svc(blockCreate *blk, struct svc_req *rqstp)
{
  FILE *fp;
  int  ret;
  char *backstore = NULL;
  char *iqn = NULL;
  char *lun = NULL;
  char *portal = NULL;
  char *attr = NULL;
  char *exec = NULL;
  blockResponse *reply = NULL;


  if (GB_ALLOC(reply) < 0) {
    goto out;
  }
  reply->exit = -1;

  /* Check if targetcli and tcmu-runner installed ? */
  ret = WEXITSTATUS(system(GB_TGCLI_GLFS_CHECK));
  if (ret == EKEYEXPIRED || ret == 1) {
    reply->exit = EKEYEXPIRED;
    if (asprintf(&reply->out,
                 "check if targetcli and tcmu-runner are installed.") == -1) {
      goto out;
    }
    goto out;
  }

  if (asprintf(&backstore, "%s %s %s %zu %s@%s%s/%s %s", GB_TGCLI_GLFS,
               GB_CREATE, blk->block_name, blk->size, blk->volume,
               blk->ipaddr, GB_STOREDIR, blk->gbid, blk->gbid) == -1) {
    goto out;
  }

  if (asprintf(&iqn, "%s %s %s%s", GB_TGCLI_ISCSI, GB_CREATE,
               GB_TGCLI_IQN_PREFIX, blk->gbid) == -1) {
    goto out;
  }


  if (asprintf(&lun, "%s/%s%s/tpg1/luns %s %s/%s",  GB_TGCLI_ISCSI,
               GB_TGCLI_IQN_PREFIX, blk->gbid, GB_CREATE,
               GB_TGCLI_GLFS_PATH, blk->block_name) == -1) {
    goto out;
  }

  if (asprintf(&portal, "%s/%s%s/tpg1/portals create %s",
               GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid,
               blk->ipaddr) == -1) {
    goto out;
  }

  if (asprintf(&attr, "%s/%s%s/tpg1 set attribute %s",
               GB_TGCLI_ISCSI, GB_TGCLI_IQN_PREFIX, blk->gbid,
               GB_TGCLI_ATTRIBUTES) == -1) {
    goto out;
  }


  if (asprintf(&exec, "%s && %s && %s && %s && %s && %s && %s",
               GB_TGCLI_GLOBALS, backstore, iqn, lun, portal, attr,
               GB_TGCLI_SAVE) == -1) {
    goto out;
  }

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

 out:
  GB_FREE(exec);
  GB_FREE(attr);
  GB_FREE(portal);
  GB_FREE(lun);
  GB_FREE(iqn);
  GB_FREE(backstore);

  return reply;
}


blockResponse *
block_delete_cli_1_svc(blockDeleteCli *blk, struct svc_req *rqstp)
{
  int ret = -1;
  char *tmp = NULL;
  blockRemoteDeleteResp *savereply = NULL;
  static blockResponse *reply = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;


  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }

  if (GB_ALLOC(savereply) < 0) {
    GB_FREE(reply);
    return NULL;
  }

  glfs = glusterBlockVolumeInit(blk->volume);
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockVolumeInit(%s) for block %s failed",
        blk->volume, blk->block_name);
    goto out;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s %s for block %s",
        FAILED_CREATING_META, blk->volume, blk->block_name);
    goto out;
  }

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, ret, out);

  if (glfs_access(glfs, blk->block_name, F_OK)) {
    LOG("mgmt", GB_LOG_ERROR,
        "block with name %s doesn't exist in the volume %s",
        blk->block_name, blk->volume);
    GB_STRDUP(reply->out, "BLOCK doesn't EXIST");
    reply->exit = ENOENT;
    goto out;
  }

  ret = glusterBlockCleanUp(DELETE_SRV, glfs, blk->block_name, TRUE, &savereply);
  if (ret) {
    LOG("mgmt", GB_LOG_WARNING, "glusterBlockCleanUp: return %d "
        "on block %s for volume %s", ret, blk->block_name, blk->volume);
  }

 out:
  if (ret == EKEYEXPIRED) {
    if (asprintf(&reply->out,
                 "Looks like targetcli and tcmu-runner are not installed on few nodes.\n"
                 "RESULT:FAIL\n") == -1) {
      ret = -1;
    }
  } else {
     /* save 'failed on'*/
    if (savereply->d_attempt) {
      if (asprintf(&tmp, "FAILED ON: %s\n", savereply->d_attempt) == -1) {
        ret = -1;
      }
    }

    if (asprintf(&reply->out,
                 "%sSUCCESSFUL ON: %s\nRESULT: %s\n", tmp?tmp:"",
                 savereply->d_success?savereply->d_success:"None",
                 ret?"FAIL":"SUCCESS") == -1) {
      ret = -1;
    }
  }

  GB_METAUNLOCK(lkfd, blk->volume, ret);

  reply->exit = ret;

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): for block %s on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->block_name, blk->volume, strerror(errno));
  }

  glfs_fini(glfs);

  if (savereply) {
    GB_FREE(savereply->d_attempt);
    GB_FREE(savereply->d_success);
    GB_FREE(savereply);
  }
  GB_FREE(tmp);

  return reply;
}


blockResponse *
block_delete_1_svc(blockDelete *blk, struct svc_req *rqstp)
{
  FILE *fp;
  char *iqn = NULL;
  char *backstore = NULL;
  char *exec = NULL;
  int ret;
  blockResponse *reply = NULL;


  if (GB_ALLOC(reply) < 0) {
    goto out;
  }
  reply->exit = -1;

  /* Check if targetcli and tcmu-runner installed ? */
  ret = WEXITSTATUS(system(GB_TGCLI_GLFS_CHECK));
  if (ret == EKEYEXPIRED || ret == 1) {
    reply->exit = EKEYEXPIRED;
    if (asprintf(&reply->out,
                 "check if targetcli and tcmu-runner are installed.") == -1) {
      goto out;
    }
    goto out;
  }

  if (asprintf(&exec, GB_TGCLI_CHECK, blk->block_name) == -1) {
    goto out;
  }

  /* Check if block exist on this node ? */
  if (WEXITSTATUS(system(exec)) == 1) {
    reply->exit = 0;
    if (asprintf(&reply->out, "No %s.", blk->block_name) == -1) {
      goto out;
    }
    goto out;
  }
  GB_FREE(exec);

  if (asprintf(&iqn, "%s %s %s%s", GB_TGCLI_ISCSI, GB_DELETE,
               GB_TGCLI_IQN_PREFIX, blk->gbid) == -1) {
    goto out;
  }

  if (asprintf(&backstore, "%s %s %s", GB_TGCLI_GLFS,
               GB_DELETE, blk->block_name) == -1) {
    goto out;
  }

  if (asprintf(&exec, "%s && %s && %s", backstore, iqn,
               GB_TGCLI_SAVE) == -1) {
    goto out;
  }

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

 out:
  GB_FREE(exec);
  GB_FREE(backstore);
  GB_FREE(iqn);

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
  int ret = -1;


  glfs = glusterBlockVolumeInit(blk->volume);
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockVolumeInit(%s) failed", blk->volume);
    goto out;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s %s",
        FAILED_CREATING_META, blk->volume);
    goto out;
  }

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, ret, out);

  tgmdfd = glfs_opendir (glfs, GB_METADIR);
  if (!tgmdfd) {
    LOG("mgmt", GB_LOG_ERROR, "glfs_opendir(%s): on volume %s failed[%s]",
        GB_METADIR, blk->volume, strerror(errno));
    goto out;
  }

  while ((entry = glfs_readdir (tgmdfd))) {
    if (strcmp(entry->d_name, ".") &&
       strcmp(entry->d_name, "..") &&
       strcmp(entry->d_name, "meta.lock")) {
      if (asprintf(&filelist, "%s%s\n", (tmp==NULL?"":tmp),
                   entry->d_name)  == -1) {
        filelist = NULL;
        GB_FREE(tmp);
        ret = -1;
        goto out;
      }
      GB_FREE(tmp);
      tmp = filelist;
    }
  }

  ret = 0;

 out:
  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }

  reply->out = filelist? filelist:strdup("*Nil*\n");

  if (tgmdfd && glfs_closedir (tgmdfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "glfs_closedir(%s): on volume %s failed[%s]",
        GB_METADIR, blk->volume, strerror(errno));
  }

  GB_METAUNLOCK(lkfd, blk->volume, ret);

  reply->exit = ret;

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "glfs_close(%s): on volume %s failed[%s]",
        GB_TXLOCKFILE, blk->volume, strerror(errno));
  }

  glfs_fini(glfs);

  return reply;
}


blockResponse *
block_info_cli_1_svc(blockInfoCli *blk, struct svc_req *rqstp)
{
  blockResponse *reply;
  char *out = NULL;
  char *tmp = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;
  MetaInfo *info = NULL;
  int ret = -1;
  size_t i;


  glfs = glusterBlockVolumeInit(blk->volume);
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR,
        "glusterBlockVolumeInit(%s) for block %s failed",
        blk->volume, blk->block_name);
    goto out;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s %s for block %s",
        FAILED_CREATING_META, blk->volume, blk->block_name);
    goto out;
  }

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, ret, out);

  if (GB_ALLOC(info) < 0) {
    goto out;
  }

  ret = blockGetMetaInfo(glfs, blk->block_name, info);
  if (ret) {
    goto out;
  }

  if (asprintf(&tmp, "NAME: %s\nVOLUME: %s\nGBID: %s\nSIZE: %zu\n"
                     "HA: %zu\nBLOCK CONFIG NODE(S):",
               blk->block_name, info->volume, info->gbid,
               info->size, info->mpath) == -1) {
    ret = -1;
    goto out;
  }
  for (i = 0; i < info->nhosts; i++) {
    if (blockMetaStatusEnumParse(info->list[i]->status) == GB_CONFIG_SUCCESS) {
      if (asprintf(&out, "%s %s", (tmp==NULL?"":tmp),
                   info->list[i]->addr) == -1) {
        out = NULL;
        GB_FREE(tmp);
        ret = -1;
        goto out;
      }
      GB_FREE(tmp);
      tmp = out;
    }
  }
  if (asprintf(&out, "%s\n", tmp) == -1) {
    ret = -1;
    goto out;
  }
  ret = 0;

 out:
  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }

  if (!out) {
    if (asprintf(&out, "No Block with name %s", blk->block_name) == -1) {
      ret = -1;
    }
  }

  reply->out = out;

  GB_METAUNLOCK(lkfd, blk->volume, ret);

  reply->exit = ret;

  if (lkfd && glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR,
        "glfs_close(%s): on volume %s for block %s failed[%s]",
        GB_TXLOCKFILE, blk->volume, blk->block_name, strerror(errno));
  }

  glfs_fini(glfs);

  blockFreeMetaInfo(info);

  return reply;
}
