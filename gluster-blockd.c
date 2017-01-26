/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

# define _GNU_SOURCE         /* See feature_test_macros(7) */

# include  <stdio.h>
# include  <netdb.h>
# include  <sys/socket.h>
# include  <uuid/uuid.h>

# include  "rpc/block.h"
# include  "common.h"
# include  "glfs-operations.h"


# define   UUID_BUF_SIZE     38

# define   CREATE            "create"
# define   LIST              "list"
# define   INFO              "info"
# define   DELETE            "delete"

# define   GLFS_PATH         "/backstores/user:glfs"
# define   TARGETCLI_GLFS    "targetcli "GLFS_PATH
# define   TARGETCLI_ISCSI   "targetcli /iscsi"
# define   TARGETCLI_SAVE    "targetcli / saveconfig"
# define   ATTRIBUTES        "generate_node_acls=1 demo_mode_write_protect=0"
# define   BACKEND_CFGSTR    "ls | grep ' %s ' | cut -d'[' -f2 | cut -d']' -f1"
# define   LUNS_LIST         "ls | grep -v user:glfs | cut -d'-' -f2 | cut -d' ' -f2"
# define   IQN_PREFIX        "iqn.2016-12.org.gluster-block:"

# define   MSERVER_DELIMITER ","



typedef struct blockServerDef {
  size_t nhosts;
  char   **hosts;
} blockServerDef;
typedef blockServerDef *blockServerDefPtr;

typedef enum opterations {
  CREATE_SRV = 1,
  LIST_SRV   = 2,
  INFO_SRV   = 3,
  DELETE_SRV = 4,
  EXEC_SRV   = 5
} opterations;


static int
gluster_block_1(char *host, void *cobj, opterations opt, char **out)
{
  CLIENT *clnt;
  int sockfd;
  int ret = -1;
  blockResponse *reply = NULL;
  struct hostent *server;
  struct sockaddr_in sain;

  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    perror("gluster-blockd: socket");
    exit(1);
  }

  server = gethostbyname(host);
  if (server == NULL) {
    fprintf(stderr,"ERROR, no such host\n");
    exit(0);
  }

  bzero((char *) &sain, sizeof(sain));
  sain.sin_family = AF_INET;
  bcopy((char *)server->h_addr, (char *)&sain.sin_addr.s_addr, server->h_length);
  sain.sin_port = htons(24006);

  if (connect(sockfd, (struct sockaddr *) &sain, sizeof(sain)) < 0) {
    perror("gluster-blockd: connect");
    exit(1);
  }

  clnt = clnttcp_create ((struct sockaddr_in *) &sain, GLUSTER_BLOCK, GLUSTER_BLOCK_VERS, &sockfd, 0, 0);
  if (clnt == NULL) {
    clnt_pcreateerror ("localhost");
    exit (1);
  }

  switch(opt) {
  case CREATE_SRV:
    reply = block_create_1((blockCreate *)cobj, clnt);
    if (reply == NULL) {
      clnt_perror (clnt, "call failed gluster-block");
    }
    break;
  case DELETE_SRV:
    reply = block_delete_1((blockDelete *)cobj, clnt);
    if (reply == NULL) {
      clnt_perror (clnt, "call failed gluster-block");
    }
    break;
  case INFO_SRV:
  case LIST_SRV:
    break;
  case EXEC_SRV:
    reply = block_exec_1((char **)&cobj, clnt);
    if (reply == NULL) {
      clnt_perror (clnt, "call failed gluster-block");
    }
    break;
  }

  if (GB_STRDUP(*out, reply->out) < 0) {
    ret = -1;
    goto out;
  }
  ret = reply->exit;

 out:
  if (!clnt_freeres(clnt, (xdrproc_t) xdr_blockResponse, (char *) reply))
    clnt_perror (clnt, "clnt_freeres failed");

  clnt_destroy (clnt);

  return ret;
}


void
blockServerDefFree(blockServerDefPtr blkServers)
{
  size_t i;

  if (!blkServers)
    return;

  for (i = 0; i < blkServers->nhosts; i++)
     GB_FREE(blkServers->hosts[i]);
   GB_FREE(blkServers->hosts);
   GB_FREE(blkServers);
}


static blockServerDefPtr
blockServerParse(char *blkServers)
{
  blockServerDefPtr list;
  char *tmp = blkServers;
  size_t i = 0;

  if (GB_ALLOC(list) < 0)
    return NULL;

  if (!blkServers)
    blkServers = "localhost";

  /* count number of servers */
  while (*tmp) {
    if (*tmp == ',')
      list->nhosts++;
    tmp++;
  }
  list->nhosts++;
  tmp = blkServers; /* reset addr */


  if (GB_ALLOC_N(list->hosts, list->nhosts) < 0)
    goto fail;

  for (i = 0; tmp != NULL; i++) {
    if (GB_STRDUP(list->hosts[i], strsep(&tmp, MSERVER_DELIMITER)) < 0)
      goto fail;
  }

  return list;

fail:
  blockServerDefFree(list);
  return NULL;
}


static char *
getCfgstring(char* name, char *blkServer)
{
  char *cmd;
  char *exec;
  char *out = NULL;
  int ret = -1;

  asprintf(&cmd, "%s %s", TARGETCLI_GLFS, BACKEND_CFGSTR);
  asprintf(&exec, cmd, name);

  ret = gluster_block_1(blkServer, exec, EXEC_SRV, &out);
  if (ret) {
    ERROR("%s on host: %s",
          FAILED_GATHERING_CFGSTR, blkServer);
  }

  GB_FREE(cmd);
  GB_FREE(exec);

  return out;
}


blockResponse *
block_create_cli_1_svc(blockCreateCli *blk, struct svc_req *rqstp)
{
  int ret = -1;
  size_t i = 0;
  char *out = NULL;
  char savereply[8096] = {0,};
  uuid_t uuid;
  static blockCreate *cobj;
  static blockResponse *reply = NULL;
  blockServerDefPtr list = NULL;
  char *gbid = CALLOC(UUID_BUF_SIZE);
  struct glfs *glfs = NULL;
  struct glfs_fd *lkfd;
  struct glfs_fd *tgfd = NULL;
  struct flock lock = {0, };
  char *write = NULL;

  glfs = glusterBlockVolumeInit(blk->volume, blk->volfileserver);
  if (!glfs) {
    ERROR("%s", "glusterBlockVolumeInit failed");
    goto out;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs);
  if (!lkfd) {
    ERROR("%s", "glusterBlockCreateMetaLockFile failed");
    goto out;
  }

  METALOCK(lock, lkfd);

  uuid_generate(uuid);
  uuid_unparse(uuid, gbid);

  if(GB_ALLOC(reply) < 0)
    goto out;


  if (!glfs_access(glfs, blk->block_name, F_OK)) {
    GB_STRDUP(reply->out, "BLOCK Already EXIST");
    reply->exit = EEXIST;
    goto out;
  }

  tgfd = glfs_creat(glfs, blk->block_name, O_RDWR, S_IRUSR | S_IWUSR);
  if (!tgfd) {
    ERROR("%s", "glfs_creat: failed");
    goto out;
  }

  METAUPDATE(tgfd, write,
             "GBID: %s\nSIZE: %zu\nHA: %d\nENTRYCREATE: INPROGRESS\n",
             gbid, blk->size, 1);

  ret = glusterBlockCreateEntry(blk, gbid);
  if (ret) {
    ERROR("%s volume: %s host: %s",
          FAILED_CREATING_FILE, blk->volume, blk->volfileserver);
    goto out;
  }

  METAUPDATE(tgfd, write, "ENTRYCREATE: SUCCESS\n");

  if(GB_ALLOC(cobj) < 0)
    goto out;

  strcpy(cobj->volume, blk->volume);
  strcpy(cobj->volfileserver, blk->volfileserver);
  strcpy(cobj->block_name, blk->block_name);
  cobj->size = blk->size;
  strcpy(cobj->gbid, gbid);

  list = blockServerParse(blk->block_hosts);

  for (i = 0; i < list->nhosts; i++) {
    METAUPDATE(tgfd, write, "%s: INPROGRESS\n", list->hosts[i]);

    ret = gluster_block_1(list->hosts[i], cobj, CREATE_SRV, &out);
    if (ret) {
      METAUPDATE(tgfd, write, "%s: FAIL\n", list->hosts[i]);
      ERROR("%s on host: %s",
            FAILED_CREATE, list->hosts[i]);
    }

    METAUPDATE(tgfd, write, "%s: SUCCESS\n", list->hosts[i]);

    strcpy(savereply, out);
    GB_FREE(out);
  }

  if (GB_STRDUP(reply->out, savereply) < 0)
    goto out;
  reply->exit = ret;

out:
  if (glfs_close(tgfd) != 0)
    ERROR("%s", "glfs_close: failed");

  METAUNLOCK(lock, lkfd);

  if (glfs_close(lkfd) != 0)
    ERROR("%s", "glfs_close: failed");

  glfs_fini(glfs);
  blockServerDefFree(list);
  GB_FREE(cobj);

  return reply;
}


static int
glusterBlockParseCfgStringToDef(char* cfgstring,
                                blockCreate *blk)
{
  int ret = 0;
  char *p, *sep;

  /* part before '@' is the volume name */
  p = cfgstring;
  sep = strchr(p, '@');
  if (!sep) {
    ret = -1;
    goto fail;
  }

  *sep = '\0';
  strcpy(blk->volume, p);

  /* part between '@' and '/' is the server name */
  p = sep + 1;
  sep = strchr(p, '/');
  if (!sep) {
    ret = -1;
    goto fail;
  }

  *sep = '\0';
  strcpy(blk->volfileserver, p);

  /* part between '/' and '(' is the filename */
  p = sep + 1;
  sep = strchr(p, '(');
  if (!sep) {
    ret = -1;
    goto fail;
  }

  *(sep - 1) = '\0';  /* discard extra space at end of filename */
  strcpy(blk->gbid, p);

  /* part between '(' and ')' is the size */
  p = sep + 1;
  sep = strchr(p, ')');
  if (!sep) {
    ret = -1;
    goto fail;
  }

  *sep = '\0';
  blk->size = glusterBlockCreateParseSize(p);
  if (blk->size < 0) {
    ERROR("%s", "failed while parsing size");
    ret = -1;
    goto fail;
  }

 fail:
  return ret;
}


blockResponse *
block_create_1_svc(blockCreate *blk, struct svc_req *rqstp)
{
  FILE *fp;
  char *backstore = NULL;
  char *iqn = NULL;
  char *lun = NULL;
  char *attr = NULL;
  char *exec = NULL;
  blockResponse *obj = NULL;

  asprintf(&backstore, "%s %s %s %zu %s@%s/%s %s", TARGETCLI_GLFS,
           CREATE, blk->block_name, blk->size, blk->volume, blk->volfileserver,
           blk->gbid, blk->gbid);

  asprintf(&iqn, "%s %s %s%s", TARGETCLI_ISCSI, CREATE, IQN_PREFIX, blk->gbid);


  asprintf(&lun, "%s/%s%s/tpg1/luns %s %s/%s",
             TARGETCLI_ISCSI, IQN_PREFIX, blk->gbid, CREATE, GLFS_PATH, blk->block_name);

  asprintf(&attr, "%s/%s%s/tpg1 set attribute %s",
             TARGETCLI_ISCSI, IQN_PREFIX, blk->gbid, ATTRIBUTES);


  asprintf(&exec, "%s && %s && %s && %s && %s", backstore, iqn, lun, attr, TARGETCLI_SAVE);

  if(GB_ALLOC(obj) < 0)
    goto out;

  if (GB_ALLOC_N(obj->out, 4096) < 0) {
    GB_FREE(obj);
    goto out;
  }

  fp = popen(exec, "r");
  if (fp != NULL) {
    size_t newLen = fread(obj->out, sizeof(char), 4096, fp);
    if (ferror( fp ) != 0) {
      ERROR("%s", "Error reading command output\n");
    } else {
      obj->out[newLen++] = '\0';
    }
    obj->exit = WEXITSTATUS(pclose(fp));
  }

 out:
  GB_FREE(exec);
  GB_FREE(attr);
  GB_FREE(lun);
  GB_FREE(iqn);
  GB_FREE(backstore);

  return obj;

}


blockResponse *
block_delete_cli_1_svc(blockDeleteCli *blk, struct svc_req *rqstp)
{
  size_t i = 0;
  int ret = -1;
  char *out = NULL;
  char savereply[8096] = {0,};
  blockServerDefPtr list = NULL;
  char *cfgstring;
  static blockCreate *blkcfg;
  static blockDelete *cobj;
  static blockResponse *reply = NULL;
  struct glfs *glfs = NULL;
  struct glfs_fd *lkfd;
  struct glfs_fd *tgfd;
  struct flock lock = {0, };
  char *write;

  glfs = glusterBlockVolumeInit(blk->volume, "localhost");
  if (!glfs) {
    ERROR("%s", "glusterBlockVolumeInit failed");
    goto out;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs);
  if (!lkfd) {
    ERROR("%s", "glusterBlockCreateMetaLockFile failed");
    goto out;
  }

  METALOCK(lock, lkfd);

  if (glfs_access(glfs, blk->block_name, F_OK)) {
    GB_STRDUP(reply->out, "BLOCK Doesn't EXIST");
    reply->exit = ENOENT;
    goto out;
  }

  tgfd = glfs_open(glfs, blk->block_name, O_RDWR);
  if (!tgfd) {
    ERROR("%s", "glfs_open: failed");
    goto out;
  }
  glfs_lseek (tgfd, 0, SEEK_END);

  //for
  cfgstring = getCfgstring(blk->block_name, blk->block_hosts);
  if (!cfgstring) {
    ERROR("%s", "failed while gathering CfgString");
    goto out;
  }

  if (GB_ALLOC(blkcfg) < 0)
    goto out;

  if(glusterBlockParseCfgStringToDef(cfgstring, blkcfg)) {
    ERROR("%s", "failed while parsing CfgString to glusterBlockDef");
    goto out;
  }

  if(GB_ALLOC(cobj) < 0)
    goto out;

  strcpy(cobj->block_name, blk->block_name);


  strcpy(cobj->gbid, blkcfg->gbid);

  list = blockServerParse(blk->block_hosts);
  for (i = 0; i < list->nhosts; i++) {
    METAUPDATE(tgfd, write, "%s: CLEANUPINPROGRES\n", list->hosts[i]);
    ret = gluster_block_1(list->hosts[i], cobj, DELETE_SRV, &out);
    if (ret) {
      METAUPDATE(tgfd, write, "%s: CLEANUPFAIL\n", list->hosts[i]);
      ERROR("%s on host: %s",
            FAILED_GATHERING_INFO, list->hosts[i]);
      goto out;
    }
    METAUPDATE(tgfd, write, "%s: CLEANUPSUCCESS\n", list->hosts[i]);
    /* TODO: aggrigate the result */
    strcpy(savereply, out);
    GB_FREE(out);
  }

  if (GB_ALLOC(reply) < 0)
    goto out;

  if (GB_STRDUP(reply->out, savereply) < 0)
    goto out;
  reply->exit = ret;

  if (glusterBlockDeleteEntry(blkcfg)) {
    ERROR("%s volume: %s host: %s",
          FAILED_DELETING_FILE, blkcfg->volume, blkcfg->volfileserver);
  }

out:
  if (glfs_close(tgfd) != 0)
    ERROR("%s", "glfs_close: failed");

  ret = glfs_unlink(glfs, blk->block_name);
  if (ret && errno != ENOENT) {
    ERROR("%s", "glfs_unlink: failed");
    goto out;
  }

  METAUNLOCK(lock, lkfd);

  if (glfs_close(lkfd) != 0)
    ERROR("%s", "glfs_close: failed");

  glfs_fini(glfs);

  blockServerDefFree(list);
  GB_FREE(cfgstring);
  GB_FREE(blkcfg);
  GB_FREE(cobj);

  return reply;
}


blockResponse *
block_delete_1_svc(blockDelete *blk, struct svc_req *rqstp)
{
  FILE *fp;
  char *iqn = NULL;
  char *backstore = NULL;
  char *exec = NULL;
  blockResponse *obj = NULL;

  asprintf(&iqn, "%s %s %s%s", TARGETCLI_ISCSI, DELETE, IQN_PREFIX, blk->gbid);

  asprintf(&backstore, "%s %s %s", TARGETCLI_GLFS,
           DELETE, blk->block_name);

  asprintf(&exec, "%s && %s && %s", backstore, iqn, TARGETCLI_SAVE);

  if(GB_ALLOC(obj) < 0)
    goto out;

  if (GB_ALLOC_N(obj->out, 4096) < 0) {
    GB_FREE(obj);
    goto out;
  }

  fp = popen(exec, "r");
  if (fp != NULL) {
    size_t newLen = fread(obj->out, sizeof(char), 4096, fp);
    if (ferror( fp ) != 0) {
      ERROR("%s", "Error reading command output\n");
    } else {
      obj->out[newLen++] = '\0';
    }
    obj->exit = WEXITSTATUS(pclose(fp));
  }

out:
  GB_FREE(exec);
  GB_FREE(backstore);
  GB_FREE(iqn);

  return obj;
}


blockResponse *
block_exec_1_svc(char **cmd, struct svc_req *rqstp)
{
  FILE *fp;
  static blockResponse *obj;

  if(GB_ALLOC(obj) < 0)
    return NULL;

  if (GB_ALLOC_N(obj->out, 4096) < 0) {
    GB_FREE(obj);
    return NULL;
  }

  fp = popen(*cmd, "r");
  if (fp != NULL) {
    size_t newLen = fread(obj->out, sizeof(char), 4996, fp);
    if (ferror( fp ) != 0) {
      ERROR("%s", "Error reading command output\n");
    } else {
      obj->out[newLen++] = '\0';
    }
    obj->exit = WEXITSTATUS(pclose(fp));
  }

  return obj;
}


blockResponse *
block_list_cli_1_svc(blockListCli *blk, struct svc_req *rqstp)
{
  char *cmd;
  char *out = NULL;
  blockResponse *reply = NULL;
  size_t i = 0;
  int ret = -1;
  char savereply[8096] = {0,};
  blockServerDefPtr list = NULL;
  struct glfs *glfs;
  struct glfs_fd *lkfd;
  struct flock lock = {0, };

  glfs = glusterBlockVolumeInit(blk->volume, "localhost");
  if (!glfs) {
    ERROR("%s", "glusterBlockVolumeInit failed");
    goto out;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs);
  if (!lkfd) {
    ERROR("%s", "glusterBlockCreateMetaLockFile failed");
    goto out;
  }

  METALOCK(lock, lkfd);

  asprintf(&cmd, "%s %s", TARGETCLI_GLFS, LUNS_LIST);

  list = blockServerParse(blk->block_hosts);

  for (i = 0; i < list->nhosts; i++) {
    ret = gluster_block_1(list->hosts[i], cmd, EXEC_SRV, &out);
    if (ret) {
      ERROR("%s on host: %s",
            FAILED_LIST, list->hosts[i]);
      goto out;
    }
    /* TODO: aggrigate the result */
    strcpy(savereply, out);
    GB_FREE(out);
  }

  if (GB_ALLOC(reply) < 0)
    goto out;

  if (GB_STRDUP(reply->out, savereply) < 0)
    goto out;
  reply->exit = ret;

 out:

  METAUNLOCK(lock, lkfd);

  if (glfs_close(lkfd) != 0)
    ERROR("%s", "glfs_close: failed");

  glfs_fini(glfs);

  blockServerDefFree(list);
  GB_FREE(cmd);

  return reply;
}


blockResponse *
block_info_cli_1_svc(blockInfoCli *blk, struct svc_req *rqstp)
{
  blockResponse *reply = NULL;
  static struct glfs *glfs;
  static struct glfs_fd *lkfd;
  static struct flock lock = {0, };
  MetaInfo *info = NULL;

  glfs = glusterBlockVolumeInit(blk->volume, "localhost");
  if (!glfs) {
    ERROR("%s", "glusterBlockVolumeInit failed");
    goto out;
  }

  lkfd = glusterBlockCreateMetaLockFile(glfs);
  if (!lkfd) {
    ERROR("%s", "glusterBlockCreateMetaLockFile failed");
    goto out;
  }

  METALOCK(lock, lkfd);

  if (GB_ALLOC(info) < 0)
    goto out;

  blockGetMetaInfo(glfs, blk->block_name, info);

  if (GB_ALLOC(reply) < 0)
    goto out;

  asprintf(&reply->out, "NAME: %s\nVOLUME: %s\nGBID: %s\nSIZE: %zu\nMULTIPATH: %zu",
           blk->block_name, blk->volume, info->gbid, info->size, info->mpath);

  reply->exit = 0;

 out:
  METAUNLOCK(lock, lkfd);

  if (glfs_close(lkfd) != 0)
    ERROR("%s", "glfs_close: failed");

  glfs_fini(glfs);

  blockFreeMetaInfo(info);

  return reply;
}
