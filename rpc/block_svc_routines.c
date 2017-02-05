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

# include  <netdb.h>
# include  <uuid/uuid.h>


# define   UUID_BUF_SIZE     38

# define   CREATE            "create"
# define   DELETE            "delete"

# define   GLFS_PATH         "/backstores/user:glfs"
# define   TARGETCLI_GLFS    "targetcli "GLFS_PATH
# define   TARGETCLI_ISCSI   "targetcli /iscsi"
# define   TARGETCLI_SAVE    "targetcli / saveconfig"
# define   ATTRIBUTES        "generate_node_acls=1 demo_mode_write_protect=0"
# define   IQN_PREFIX        "iqn.2016-12.org.gluster-block:"

# define   MSERVER_DELIMITER ","



int
glusterBlockCallRPC_1(char *host, void *cobj,
                      operations opt, char **out)
{
  CLIENT *clnt = NULL;
  int ret = -1;
  int sockfd;
  blockResponse *reply;
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
  sain.sin_port = htons(24006);

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

  if (GB_STRDUP(*out, reply->out) < 0){
    goto out;
  }
  ret = reply->exit;

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


static void
glusterBlockCreateRemote(struct glfs_fd *tgmfd, char *volume,
                         blockCreate *cobj, char *addr, char **reply)
{
  int ret;
  char *out = NULL;
  char *tmp = *reply;


  GB_METAUPDATE_OR_GOTO(tgmfd, cobj->gbid, volume, ret, out,
                        "%s: CONFIGINPROGRESS\n", addr);

  ret = glusterBlockCallRPC_1(addr, cobj, CREATE_SRV, &out);
  if (ret) {
    GB_METAUPDATE_OR_GOTO(tgmfd, cobj->gbid, volume, ret, out,
                          "%s: CONFIGFAIL\n", addr);
    LOG("mgmt", GB_LOG_ERROR, "%s on host: %s", FAILED_CREATE, addr);
    goto out;
  }

  GB_METAUPDATE_OR_GOTO(tgmfd, cobj->gbid, volume, ret, out,
                        "%s: CONFIGSUCCESS\n", addr);

 out:
  asprintf(reply, "%s%s\n", (tmp==NULL?"":tmp), out);
  GB_FREE(tmp);
  GB_FREE(out);
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
        glusterBlockCreateRemote(tgmfd, info->volume, cobj,
                                 list->hosts[i], reply);
      }
    }
  }

  ret = glusterBlockAuditRequest(glfs, tgmfd, blk, cobj, list, reply);

 out:
  blockFreeMetaInfo(info);
  return ret;
}


static void
glusterBlockDeleteRemote(struct glfs_fd *tgmfd, MetaInfo *info,
                         blockDelete *cobj, char *addr, char **reply)
{
  int ret = -1;
  char *out = NULL;
  char *tmp = *reply;


  GB_METAUPDATE_OR_GOTO(tgmfd, info->gbid, info->volume, ret, out,
                        "%s: CLEANUPINPROGRES\n", addr);
  ret = glusterBlockCallRPC_1(addr, cobj, DELETE_SRV, &out);
  if (ret) {
    GB_METAUPDATE_OR_GOTO(tgmfd, info->gbid, info->volume, ret, out,
                          "%s: CLEANUPFAIL\n", addr);
    LOG("mgmt", GB_LOG_ERROR, "%s on host: %s",
        FAILED_GATHERING_INFO, addr);
    goto out;
  }
  GB_METAUPDATE_OR_GOTO(tgmfd, info->gbid, info->volume, ret, out,
                        "%s: CLEANUPSUCCESS\n", addr);

 out:
  asprintf(reply, "%s%s\n", (tmp==NULL?"":tmp), out);
  GB_FREE(tmp);
  GB_FREE(out);
}


static int
glusterBlockCleanUp(struct glfs *glfs, char *blockname,
                    bool deleteall, char **reply)
{
  int ret = -1;
  size_t i;
  static blockDelete cobj;
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

  strcpy(cobj.block_name, blockname);
  strcpy(cobj.gbid, info->gbid);

  tgmfd = glfs_open(glfs, blockname, O_WRONLY|O_APPEND);
  if (!tgmfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glfs_open: failed");
    goto out;
  }

  for (i = 0; i < info->nhosts; i++) {
    switch (blockMetaStatusEnumParse(info->list[i]->status)) {
    case GB_CLEANUP_INPROGRES:
    case GB_CLEANUP_FAIL:
    case GB_CONFIG_FAIL:
    case GB_CONFIG_INPROGRESS:
      glusterBlockDeleteRemote(tgmfd, info, &cobj,
                               info->list[i]->addr, reply);
      break;
    }
    if (deleteall &&
        blockMetaStatusEnumParse(info->list[i]->status) == GB_CONFIG_SUCCESS) {
        glusterBlockDeleteRemote(tgmfd, info, &cobj,
                                 info->list[i]->addr, reply);
    }
  }
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
    if (glusterBlockDeleteEntry(info->volume, info->gbid)) {
      LOG("mgmt", GB_LOG_ERROR, "%s volume: %s host: %s",
          FAILED_DELETING_FILE, info->volume, "localhost");
    }
    ret = glfs_unlink(glfs, blockname);
    if (ret && errno != ENOENT) {
      LOG("mgmt", GB_LOG_ERROR, "%s", "glfs_unlink: failed");
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
  size_t i;
  uuid_t uuid;
  char *savereply = NULL;
  char gbid[UUID_BUF_SIZE];
  static blockCreate cobj;
  static blockResponse *reply;
  struct glfs *glfs;
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
    asprintf(&reply->out, "multipath req: %d > block-hosts: %s\n",
             blk->mpath, blk->block_hosts);
    reply->exit = ENODEV;
    goto optfail;
  }

  glfs = glusterBlockVolumeInit(blk->volume, blk->volfileserver);
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

  if (!glfs_access(glfs, blk->block_name, F_OK)) {
    asprintf(&reply->out, "BLOCK with name: '%s' already EXIST\n",
             blk->block_name);
    ret = EEXIST;
    goto exist;
  }

  tgmfd = glfs_creat(glfs, blk->block_name, O_RDWR, S_IRUSR | S_IWUSR);
  if (!tgmfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glfs_creat: failed");
    goto out;
  }

  uuid_generate(uuid);
  uuid_unparse(uuid, gbid);

  GB_METAUPDATE_OR_GOTO(tgmfd, blk->block_name, blk->volume, ret, exist,
                        "VOLUME: %s\nGBID: %s\nSIZE: %zu\nHA: %d\n"
                        "ENTRYCREATE: INPROGRESS\n",
                        blk->volume, gbid, blk->size, blk->mpath);

  ret = glusterBlockCreateEntry(blk, gbid);
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

  for (i = 0; i < blk->mpath; i++) {
    glusterBlockCreateRemote(tgmfd, blk->volume, &cobj,
                             list->hosts[i], &savereply);
  }

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
  char *backstore;
  char *iqn;
  char *lun;
  char *attr;
  char *exec;
  blockResponse *reply = NULL;


  asprintf(&backstore, "%s %s %s %zu %s@%s/%s %s", TARGETCLI_GLFS,
           CREATE, blk->block_name, blk->size, blk->volume,
           blk->volfileserver, blk->gbid, blk->gbid);

  asprintf(&iqn, "%s %s %s%s", TARGETCLI_ISCSI, CREATE,
           IQN_PREFIX, blk->gbid);


  asprintf(&lun, "%s/%s%s/tpg1/luns %s %s/%s",  TARGETCLI_ISCSI,
           IQN_PREFIX, blk->gbid, CREATE, GLFS_PATH, blk->block_name);

  asprintf(&attr, "%s/%s%s/tpg1 set attribute %s",
             TARGETCLI_ISCSI, IQN_PREFIX, blk->gbid, ATTRIBUTES);


  asprintf(&exec, "%s && %s && %s && %s && %s", backstore, iqn, lun,
           attr, TARGETCLI_SAVE);

  if (GB_ALLOC(reply) < 0) {
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
  struct glfs_fd *lkfd;


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
  char *iqn;
  char *backstore;
  char *exec;
  blockResponse *reply = NULL;


  asprintf(&iqn, "%s %s %s%s", TARGETCLI_ISCSI, DELETE,
           IQN_PREFIX, blk->gbid);

  asprintf(&backstore, "%s %s %s", TARGETCLI_GLFS,
           DELETE, blk->block_name);

  asprintf(&exec, "%s && %s && %s", backstore, iqn, TARGETCLI_SAVE);

  if (GB_ALLOC(reply) < 0) {
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

  tgmfd = glfs_opendir (glfs, "/block-meta");
  if (!tgmfd) {
    LOG("mgmt", GB_LOG_ERROR, "%s", "glusterBlockVolumeInit failed");
    goto out;
  }

  while ((entry = glfs_readdir (tgmfd))) {
    if (strcmp(entry->d_name, ".") &&
       strcmp(entry->d_name, "..") &&
       strcmp(entry->d_name, "meta.lock")) {
      asprintf(&filelist, "%s%s\n", (tmp==NULL?"":tmp), entry->d_name);
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

  asprintf(&tmp, "NAME: %s\nVOLUME: %s\nGBID: %s\nSIZE: %zu\n"
                 "MULTIPATH: %zu\nBLOCK CONFIG NODE(S):",
           blk->block_name, info->volume, info->gbid,
           info->size, info->mpath);
  for (i = 0; i < info->nhosts; i++) {
    if (blockMetaStatusEnumParse(info->list[i]->status) == GB_CONFIG_SUCCESS) {
      asprintf(&out, "%s %s", (tmp==NULL?"":tmp), info->list[i]->addr);
      GB_FREE(tmp);
      tmp = out;
    }
  }
  asprintf(&out, "%s\n", tmp);
  ret = 0;

 out:
  if (GB_ALLOC(reply) < 0) {
    return NULL;
  }

  if (!out) {
    asprintf(&out, "No Block with name %s", blk->block_name);
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
