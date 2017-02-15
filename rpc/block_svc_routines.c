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

# define   CREATE            "create"
# define   DELETE            "delete"

# define   GLFS_PATH         "/backstores/user:glfs"
# define   TARGETCLI_GLFS    "targetcli "GLFS_PATH
# define   TARGETCLI_ISCSI   "targetcli /iscsi"
# define   TARGETCLI_GLOBALS "targetcli set global auto_add_default_portal=false > " DEVNULLPATH
# define   TARGETCLI_SAVE    "targetcli / saveconfig > " DEVNULLPATH
# define   ATTRIBUTES        "generate_node_acls=1 demo_mode_write_protect=0 > " DEVNULLPATH
# define   IQN_PREFIX        "iqn.2016-12.org.gluster-block:"

# define   MSERVER_DELIMITER ","



typedef struct blockRemoteObj {
    struct glfs_fd *tgmfd;
    void *obj;
    char *volume;
    char *addr;
    char *reply;
} blockRemoteObj;


int
glusterBlockCallRPC_1(char *host, void *cobj,
                      operations opt, char **out)
{
  CLIENT *clnt = NULL;
  int ret = -1;
  int sockfd;
  blockResponse *reply =  NULL;
  struct hostent *server;
  struct sockaddr_in sain;


  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "socket creation failed (%s)",
        strerror (errno));
    goto out;
  }

  server = gethostbyname(host);
  if (!server) {
    LOG("mgmt", GB_LOG_ERROR, "gethostbyname failed (%s)",
        strerror (errno));
    goto out;
  }

  bzero((char *) &sain, sizeof(sain));
  sain.sin_family = AF_INET;
  bcopy((char *)server->h_addr, (char *)&sain.sin_addr.s_addr,
        server->h_length);
  sain.sin_port = htons(GB_TCP_PORT);

  if (connect(sockfd, (struct sockaddr *) &sain, sizeof(sain)) < 0) {
    LOG("mgmt", GB_LOG_ERROR, "connect failed (%s)", strerror (errno));
    goto out;
  }

  clnt = clnttcp_create ((struct sockaddr_in *) &sain, GLUSTER_BLOCK,
                         GLUSTER_BLOCK_VERS, &sockfd, 0, 0);
  if (!clnt) {
    LOG("mgmt", GB_LOG_ERROR, "%s, inet host %s",
        clnt_spcreateerror("client create failed"), host);
    goto out;
  }

  switch(opt) {
  case CREATE_SRV:
    reply = block_create_1((blockCreate *)cobj, clnt);
    if (!reply) {
      LOG("mgmt", GB_LOG_ERROR, "%s",
          clnt_sperror(clnt, "block create failed"));
      goto out;
    }
    break;
  case DELETE_SRV:
    reply = block_delete_1((blockDelete *)cobj, clnt);
    if (!reply) {
      LOG("mgmt", GB_LOG_ERROR, "%s",
          clnt_sperror(clnt, "block delete failed"));
      goto out;
    }
    break;
  }

  if(reply) {
    if (GB_STRDUP(*out, reply->out) < 0) {
      goto out;
    }
    ret = reply->exit;
  }

 out:
  if (clnt) {
    if (!reply ||
        !clnt_freeres(clnt, (xdrproc_t)xdr_blockResponse, (char *)reply)) {
      LOG("mgmt", GB_LOG_ERROR, "%s",
          clnt_sperror(clnt, "clnt_freeres failed"));

      clnt_destroy (clnt);
    }
  }

  close(sockfd);

  return ret;
}


void
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


void
blockRemoteObjFree(pthread_t **tid, blockRemoteObj **args, int count)
{
  size_t i;


  for (i = 0; i < count; i++) {
    GB_FREE(args[i]);
    GB_FREE(tid[i]);
  }

  GB_FREE(args);
  GB_FREE(tid);
}


static blockServerDefPtr
blockServerParse(char *blkServers)
{
  blockServerDefPtr list;
  char *tmp = blkServers;
  size_t i = 0;


  if (GB_ALLOC(list) < 0) {
    return NULL;
  }

  if (!blkServers) {
    blkServers = "localhost";
  }

  /* count number of servers */
  while (*tmp) {
    if (*tmp == ',') {
      list->nhosts++;
    }
    tmp++;
  }
  list->nhosts++;
  tmp = blkServers; /* reset addr */


  if (GB_ALLOC_N(list->hosts, list->nhosts) < 0) {
    goto fail;
  }

  for (i = 0; tmp != NULL; i++) {
    if (GB_STRDUP(list->hosts[i], strsep(&tmp, MSERVER_DELIMITER)) < 0) {
      goto fail;
    }
  }

  return list;

 fail:
  blockServerDefFree(list);
  return NULL;
}


void *
glusterBlockCreateRemote(void *data)
{
  int ret;
  blockRemoteObj *args = *(blockRemoteObj**)data;
  blockCreate cobj = *(blockCreate *)args->obj;


  GB_METAUPDATE_OR_GOTO(args->tgmfd, cobj.gbid, cobj.volume, ret, out,
                        "%s: CONFIGINPROGRESS\n", args->addr);

  ret = glusterBlockCallRPC_1(args->addr, &cobj, CREATE_SRV, &args->reply);
  if (ret) {
    GB_METAUPDATE_OR_GOTO(args->tgmfd, cobj.gbid, cobj.volume, ret, out,
                          "%s: CONFIGFAIL\n", args->addr);
    LOG("mgmt", GB_LOG_ERROR, "%s on host: %s", FAILED_CREATE, args->addr);
    goto out;
  }

  GB_METAUPDATE_OR_GOTO(args->tgmfd, cobj.gbid, cobj.volume, ret, out,
                        "%s: CONFIGSUCCESS\n", args->addr);

 out:
  pthread_exit(&ret);   /* collect ret in pthread_join 2nd arg */
}


void
glusterBlockCreateRemoteAsync(blockServerDefPtr list,
                            size_t listindex, size_t mpath,
                            struct glfs_fd *tgmfd,
                            blockCreate *cobj,
                            char **savereply)
{
  pthread_t  **tid = NULL;
  static blockRemoteObj **args = NULL;
  char *tmp = *savereply;
  size_t i;


  if (GB_ALLOC_N(tid, mpath) < 0) {
    goto out;
  }

  if (GB_ALLOC_N(args, mpath) < 0) {
    goto out;
  }

  for (i = 0; i < mpath; i++) {
    if (GB_ALLOC(tid[i])< 0) {
      goto out;
    }

    if (GB_ALLOC(args[i])< 0) {
      goto out;
    }
  }

  for (i = 0; i < mpath; i++) {
    args[i]->tgmfd = tgmfd;
    args[i]->obj = (void *)cobj;
    args[i]->addr = list->hosts[i + listindex];
  }

  for (i = 0; i < mpath; i++) {
    pthread_create(tid[i], NULL, glusterBlockCreateRemote, &args[i]);
  }

  for (i = 0; i < mpath; i++) {
    pthread_join(*tid[i], NULL);
  }

  for (i = 0; i < mpath; i++) {
    if (asprintf(savereply, "%s%s\n", (tmp==NULL?"":tmp), args[i]->reply) == -1) {
      *savereply = tmp;
      goto out;
    } else {
      GB_FREE(tmp);
      tmp = *savereply;
    }
  }

 out:
  blockRemoteObjFree(tid, args, mpath);

  return;
}


static int
glusterBlockAuditRequest(struct glfs *glfs,
                         struct glfs_fd *tgmfd,
                         blockCreateCli *blk,
                         blockCreate *cobj,
                         blockServerDefPtr list,
                         char **reply)
{
  int ret = -1;
  size_t i;
  size_t successcnt = 0;
  size_t failcnt = 0;
  size_t spent;
  size_t spare;
  size_t morereq;
  MetaInfo *info;


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
    ret = 0;
    goto out;
  } else {
    spent = successcnt + failcnt;  /* total spent */
    spare = list->nhosts  - spent;  /* spare after spent */
    morereq = blk->mpath  - successcnt;  /* needed nodes to complete req */
    if (spare == 0) {
      LOG("mgmt", GB_LOG_WARNING, "%s",
          "No Spare nodes: rewining the creation of target");
      ret = -1;
      goto out;
    } else if (spare < morereq) {
      LOG("mgmt", GB_LOG_WARNING, "%s",
          "Not enough Spare nodes: rewining the creation of target");
      ret = -1;
      goto out;
    } else {
      /* create on spare */
      LOG("mgmt", GB_LOG_INFO, "%s",
          "trying to serve the mpath from spare machines");
      for (i = spent; i < list->nhosts; i++) {
        glusterBlockCreateRemoteAsync(list, spent, morereq,
                                    tgmfd, cobj, reply);
      }
    }
  }

  ret = glusterBlockAuditRequest(glfs, tgmfd, blk, cobj, list, reply);

 out:
  blockFreeMetaInfo(info);
  return ret;
}


void *
glusterBlockDeleteRemote(void *data)
{
  int ret;
  blockRemoteObj *args = *(blockRemoteObj**)data;
  blockDelete dobj = *(blockDelete *)args->obj;


  GB_METAUPDATE_OR_GOTO(args->tgmfd, dobj.gbid, args->volume, ret, out,
                        "%s: CLEANUPINPROGRES\n", args->addr);
  ret = glusterBlockCallRPC_1(args->addr, &dobj, DELETE_SRV, &args->reply);
  if (ret) {
    GB_METAUPDATE_OR_GOTO(args->tgmfd, dobj.gbid, args->volume, ret, out,
                          "%s: CLEANUPFAIL\n", args->addr);
    LOG("mgmt", GB_LOG_ERROR, "%s on host: %s",
        FAILED_GATHERING_INFO, args->addr);
    goto out;
  }
  GB_METAUPDATE_OR_GOTO(args->tgmfd, dobj.gbid, args->volume, ret, out,
                        "%s: CLEANUPSUCCESS\n", args->addr);

 out:
  pthread_exit(&ret);   /* collect ret in pthread_join 2nd arg */
}


void
glusterBlockDeleteRemoteAsync(MetaInfo *info,
                              struct glfs_fd *tgmfd,
                              blockDelete *dobj,
                              bool deleteall,
                              char **savereply)
{
  pthread_t  **tid = NULL;
  static blockRemoteObj **args = NULL;
  char *tmp = *savereply;
  size_t i;
  size_t count = 0;

  for (i = 0; i < info->nhosts; i++) {
    switch (blockMetaStatusEnumParse(info->list[i]->status)) {
    case GB_CLEANUP_INPROGRES:
    case GB_CLEANUP_FAIL:
    case GB_CONFIG_FAIL:
    case GB_CONFIG_INPROGRESS:
      count++;
      break;
    }
    if (deleteall &&
        blockMetaStatusEnumParse(info->list[i]->status) == GB_CONFIG_SUCCESS) {
      count++;
    }
  }

  if (GB_ALLOC_N(tid, count) < 0) {
    goto out;
  }

  if (GB_ALLOC_N(args, count) < 0) {
    goto out;
  }

  for (i = 0; i < count; i++) {
    if (GB_ALLOC(tid[i])< 0) {
      goto out;
    }

    if (GB_ALLOC(args[i])< 0) {
      goto out;
    }
  }

  for (i = 0, count = 0; i < info->nhosts; i++) {
    switch (blockMetaStatusEnumParse(info->list[i]->status)) {
    case GB_CLEANUP_INPROGRES:
    case GB_CLEANUP_FAIL:
    case GB_CONFIG_FAIL:
    case GB_CONFIG_INPROGRESS:
      args[count]->tgmfd = tgmfd;
      args[count]->obj = (void *)dobj;
      args[count]->volume = info->volume;
      args[count]->addr = info->list[i]->addr;
      count++;
      break;
    }
    if (deleteall &&
        blockMetaStatusEnumParse(info->list[i]->status) == GB_CONFIG_SUCCESS) {
      args[count]->tgmfd = tgmfd;
      args[count]->obj = (void *)dobj;
      args[count]->volume = info->volume;
      args[count]->addr = info->list[i]->addr;
      count++;
    }
  }

  for (i = 0; i < count; i++) {
    pthread_create(tid[i], NULL, glusterBlockDeleteRemote, &args[i]);
  }

  for (i = 0; i < count; i++) {
    pthread_join(*tid[i], NULL);
  }

  for (i = 0; i < count; i++) {
    if (asprintf(savereply, "%s%s\n", (tmp==NULL?"":tmp), args[i]->reply) == -1) {
      *savereply = tmp;
      goto out;
    } else {
      GB_FREE(tmp);
      tmp = *savereply;
    }
  }

 out:
  blockRemoteObjFree(tid, args, count);

  return;
}


static int
glusterBlockCleanUp(struct glfs *glfs, char *blockname,
                    bool deleteall, char **reply)
{
  int ret = -1;
  size_t i;
  static blockDelete dobj;
  struct glfs_fd *tgmfd = NULL;
  size_t cleanupsuccess = 0;
  MetaInfo *info;


  if (GB_ALLOC(info) < 0) {
    goto out;
  }

  ret = blockGetMetaInfo(glfs, blockname, info);
  if (ret) {
    goto out;
  }

  strcpy(dobj.block_name, blockname);
  strcpy(dobj.gbid, info->gbid);

  tgmfd = glfs_open(glfs, blockname, O_WRONLY|O_APPEND);
  if (!tgmfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glfs_open: failed");
    goto out;
  }

  glusterBlockDeleteRemoteAsync(info, tgmfd, &dobj, deleteall, reply);

  blockFreeMetaInfo(info);

  if (GB_ALLOC(info) < 0)
    goto out;

  ret = blockGetMetaInfo(glfs, blockname, info);
  if (ret)
    goto out;

  for (i = 0; i < info->nhosts; i++) {
    if (blockMetaStatusEnumParse(info->list[i]->status) == GB_CLEANUP_SUCCESS) {
      cleanupsuccess++;
    }
  }

  if (cleanupsuccess == info->nhosts) {
    if (glusterBlockDeleteEntry(glfs, info->volume, info->gbid)) {
      LOG("mgmt", GB_LOG_ERROR, "%s volume: %s host: %s",
          FAILED_DELETING_FILE, info->volume, "localhost");
    }
    ret = glusterBlockDeleteMetaLockFile(glfs, info->volume, blockname);
    if (ret) {
      LOG("mgmt", GB_LOG_ERROR, "%s", "glusterBlockDeleteMetaLockFile: failed");
      goto out;
    }
  }

 out:
  blockFreeMetaInfo(info);

  if (glfs_close(tgmfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glfs_close: failed");
  }

  return ret;
}


blockResponse *
block_create_cli_1_svc(blockCreateCli *blk, struct svc_req *rqstp)
{
  int ret = -1;
  uuid_t uuid;
  char *savereply = NULL;
  char gbid[UUID_BUF_SIZE];
  static blockCreate cobj;
  static blockResponse *reply;
  struct glfs *glfs = NULL;
  struct glfs_fd *lkfd = NULL;
  struct glfs_fd *tgmfd = NULL;
  blockServerDefPtr list = NULL;


  if (GB_ALLOC(reply) < 0) {
    goto out;
  }

  list = blockServerParse(blk->block_hosts);

  /* Fail if mpath > list->nhosts */
  if (blk->mpath > list->nhosts) {
    LOG("mgmt", GB_LOG_ERROR, "block multipath request:%d is greater "
                              "than provided block-hosts:%s",
         blk->mpath, blk->block_hosts);
    if (asprintf(&reply->out, "multipath req: %d > block-hosts: %s\n",
                 blk->mpath, blk->block_hosts) == -1) {
      reply->exit = -1;
      goto optfail;
    }
    reply->exit = ENODEV;
    goto optfail;
  }

  glfs = glusterBlockVolumeInit(blk->volume, blk->volfileserver);
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glusterBlockVolumeInit failed");
    goto optfail;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s",
        "glusterBlockCreateMetaLockFile failed");
    goto optfail;
  }

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, ret, out);

  if (!glfs_access(glfs, blk->block_name, F_OK)) {
    if (asprintf(&reply->out, "BLOCK with name: '%s' already EXIST\n",
                 blk->block_name) == -1) {
      ret = -1;
      goto exist;
    }
    ret = EEXIST;
    goto exist;
  }

  tgmfd = glfs_creat(glfs, blk->block_name, O_RDWR, S_IRUSR | S_IWUSR);
  if (!tgmfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glfs_creat: failed");
    goto exist;
  }

  uuid_generate(uuid);
  uuid_unparse(uuid, gbid);

  GB_METAUPDATE_OR_GOTO(tgmfd, blk->block_name, blk->volume, ret, exist,
                        "VOLUME: %s\nGBID: %s\nSIZE: %zu\nHA: %d\n"
                        "ENTRYCREATE: INPROGRESS\n",
                        blk->volume, gbid, blk->size, blk->mpath);

  ret = glusterBlockCreateEntry(glfs, blk, gbid);
  if (ret) {
    GB_METAUPDATE_OR_GOTO(tgmfd, blk->block_name, blk->volume, ret,
                          exist, "ENTRYCREATE: FAIL\n");
    LOG("mgmt", GB_LOG_ERROR, "%s volume: %s host: %s",
        FAILED_CREATING_FILE, blk->volume, blk->volfileserver);
    goto out;
  }

  GB_METAUPDATE_OR_GOTO(tgmfd, blk->block_name, blk->volume, ret, exist,
                        "ENTRYCREATE: SUCCESS\n");

  strcpy(cobj.volume, blk->volume);
  strcpy(cobj.volfileserver, blk->volfileserver);
  strcpy(cobj.block_name, blk->block_name);
  cobj.size = blk->size;
  strcpy(cobj.gbid, gbid);

  glusterBlockCreateRemoteAsync(list, 0, blk->mpath,
                              tgmfd, &cobj, &savereply);

  /* Check Point */
  ret = glusterBlockAuditRequest(glfs, tgmfd, blk,
                                 &cobj, list, &savereply);
  if (ret) {
      LOG("mgmt", GB_LOG_ERROR, "%s",
          "even spare nodes have exhausted rewinding");
      ret = glusterBlockCleanUp(glfs,
                                blk->block_name, FALSE, &savereply);
  }

 out:
  reply->out = savereply;

  if (glfs_close(tgmfd) != 0)
    LOG("mgmt", GB_LOG_ERROR, "%s", "glfs_close: failed");

 exist:
  GB_METAUNLOCK(lkfd, blk->volume, ret);

  reply->exit = ret;

  if (glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glfs_close: failed");
  }

  glfs_fini(glfs);

 optfail:
  blockServerDefFree(list);

  return reply;
}


blockResponse *
block_create_1_svc(blockCreate *blk, struct svc_req *rqstp)
{
  FILE *fp;
  char hostname[255];
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

  if (gethostname(hostname, 255)) {
    LOG("mgmt", GB_LOG_ERROR, "gethostname failed (%s)", strerror(errno));
    reply->exit = -1;
    goto out;
  }

  if (asprintf(&backstore, "%s %s %s %zu %s@%s%s/%s %s", TARGETCLI_GLFS,
               CREATE, blk->block_name, blk->size, blk->volume,
               blk->volfileserver, GB_STOREDIR, blk->gbid, blk->gbid) == -1) {
    reply->exit = -1;
    goto out;
  }

  if (asprintf(&iqn, "%s %s %s%s", TARGETCLI_ISCSI, CREATE,
               IQN_PREFIX, blk->gbid) == -1) {
    reply->exit = -1;
    goto out;
  }


  if (asprintf(&lun, "%s/%s%s/tpg1/luns %s %s/%s",  TARGETCLI_ISCSI,
               IQN_PREFIX, blk->gbid, CREATE, GLFS_PATH,
               blk->block_name) == -1) {
    reply->exit = -1;
    goto out;
  }

  if (asprintf(&portal, "%s/%s%s/tpg1/portals create %s",
               TARGETCLI_ISCSI, IQN_PREFIX, blk->gbid,
               hostname) == -1) {
    reply->exit = -1;
    goto out;
  }

  if (asprintf(&attr, "%s/%s%s/tpg1 set attribute %s",
               TARGETCLI_ISCSI, IQN_PREFIX, blk->gbid,
               ATTRIBUTES) == -1) {
    reply->exit = -1;
    goto out;
  }


  if (asprintf(&exec, "%s && %s && %s && %s && %s && %s && %s", TARGETCLI_GLOBALS,
               backstore, iqn, lun, portal, attr, TARGETCLI_SAVE) == -1) {
    reply->exit = -1;
    goto out;
  }

  if (GB_ALLOC_N(reply->out, 4096) < 0) {
    GB_FREE(reply);
    goto out;
  }

  fp = popen(exec, "r");
  if (fp != NULL) {
    size_t newLen = fread(reply->out, sizeof(char), 4096, fp);
    if (ferror( fp ) != 0) {
      LOG("mgmt", GB_LOG_ERROR, "Reading command %s output", exec);
    } else {
      reply->out[newLen++] = '\0';
    }
    reply->exit = WEXITSTATUS(pclose(fp));
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
  char *savereply = NULL;
  static blockResponse *reply = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd = NULL;


  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }

  glfs = glusterBlockVolumeInit(blk->volume, "localhost");
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glusterBlockVolumeInit failed");
    goto out;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s",
        "glusterBlockCreateMetaLockFile failed");
    goto out;
  }

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, ret, out);

  if (glfs_access(glfs, blk->block_name, F_OK)) {
    GB_STRDUP(reply->out, "BLOCK Doesn't EXIST");
    reply->exit = ENOENT;
    goto out;
  }

  ret = glusterBlockCleanUp(glfs, blk->block_name, TRUE, &savereply);

 out:
  reply->out = savereply;

  GB_METAUNLOCK(lkfd, blk->volume, ret);

  reply->exit = ret;

  if (glfs_close(lkfd) != 0)
    LOG("mgmt", GB_LOG_ERROR, "%s", "glfs_close: failed");

  glfs_fini(glfs);

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


  if (GB_ALLOC(reply) < 0) {
    goto out;
  }

  if (asprintf(&iqn, "%s %s %s%s", TARGETCLI_ISCSI, DELETE,
               IQN_PREFIX, blk->gbid) == -1) {
    reply->exit = -1;
    goto out;
  }

  if (asprintf(&backstore, "%s %s %s", TARGETCLI_GLFS,
               DELETE, blk->block_name) == -1) {
    reply->exit = -1;
    goto out;
  }

  if (asprintf(&exec, "%s && %s && %s", backstore, iqn,
               TARGETCLI_SAVE) == -1) {
    reply->exit = -1;
    goto out;
  }

  if (GB_ALLOC_N(reply->out, 4096) < 0) {
    GB_FREE(reply);
    goto out;
  }

  fp = popen(exec, "r");
  if (fp != NULL) {
    size_t newLen = fread(reply->out, sizeof(char), 4096, fp);
    if (ferror( fp ) != 0) {
      LOG("mgmt", GB_LOG_ERROR, "reading command %s output", exec);
    } else {
      reply->out[newLen++] = '\0';
    }
    reply->exit = WEXITSTATUS(pclose(fp));
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
  struct glfs_fd *tgmfd = NULL;
  struct dirent *entry;
  char *tmp = NULL;
  char *filelist = NULL;
  int ret = -1;


  glfs = glusterBlockVolumeInit(blk->volume, "localhost");
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glusterBlockVolumeInit failed");
    goto out;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s",
        "glusterBlockCreateMetaLockFile failed");
    goto out;
  }

  GB_METALOCK_OR_GOTO(lkfd, blk->volume, ret, out);

  tgmfd = glfs_opendir (glfs, GB_METADIR);
  if (!tgmfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glusterBlockVolumeInit failed");
    goto out;
  }

  while ((entry = glfs_readdir (tgmfd))) {
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

  glfs_closedir (tgmfd);

  GB_METAUNLOCK(lkfd, blk->volume, ret);

  reply->exit = ret;

  if (glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glfs_close: failed");
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


  glfs = glusterBlockVolumeInit(blk->volume, "localhost");
  if (!glfs) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glusterBlockVolumeInit failed");
    goto out;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs, blk->volume);
  if (!lkfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s",
        "glusterBlockCreateMetaLockFile failed");
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
                     "MULTIPATH: %zu\nBLOCK CONFIG NODE(S):",
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

  if (glfs_close(lkfd) != 0) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glfs_close: failed");
  }

  glfs_fini(glfs);

  blockFreeMetaInfo(info);

  return reply;
}
