#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include   <stdio.h>
#include   <netdb.h>
#include   <sys/socket.h>
# include  <uuid/uuid.h>

#include "rpc/block.h"
#include "utils.h"
#include "glfs-operations.h"
# define UUID_BUF_SIZE 256

# define  CFG_STRING_SIZE  256

# define  LIST             "list"
# define  CREATE           "create"
# define  DELETE           "delete"
# define  INFO             "info"
# define  MODIFY           "modify"
# define  BLOCKHOST        "block-host"
# define  HELP             "help"

# define  GLFS_PATH        "/backstores/user:glfs"
# define  TARGETCLI_GLFS   "targetcli "GLFS_PATH
# define  TARGETCLI_ISCSI  "targetcli /iscsi"
# define  TARGETCLI_SAVE   "targetcli / saveconfig"
# define  ATTRIBUTES       "generate_node_acls=1 demo_mode_write_protect=0"
# define  BACKEND_CFGSTR   "ls | grep ' %s ' | cut -d'[' -f2 | cut -d']' -f1"
# define  LUNS_LIST        "ls | grep -v user:glfs | cut -d'-' -f2 | cut -d' ' -f2"

# define  IQN_PREFIX       "iqn.2016-12.org.gluster-block:"

# define MSERVER_DELIMITER ","

typedef enum opterations {
  CREATE_SRV = 1,
  LIST_SRV   = 2,
  INFO_SRV   = 3,
  DELETE_SRV = 4,
  EXEC_SRV   = 5
} opterations;

static void
gluster_block_1(char *host, void *cobj, opterations opt, blockResponse  **reply)
{
  CLIENT *clnt;
  int sockfd;
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
      *reply = block_create_1((blockCreate *)cobj, clnt);
      if (*reply == NULL) {
        clnt_perror (clnt, "call failed gluster-block");
      }
      break;
    case DELETE_SRV:
      *reply = block_delete_1((blockDelete *)cobj, clnt);
      if (*reply == NULL) {
        clnt_perror (clnt, "call failed gluster-block");
      }
      break;
    case INFO_SRV:
    case LIST_SRV:
      break;
    case EXEC_SRV:
      *reply = block_exec_1((char **)&cobj, clnt);
      if (*reply == NULL) {
        clnt_perror (clnt, "call failed gluster-block");
      }
      break;
  }
  clnt_destroy (clnt);
}

static char *
getCfgstring(char* name, char *blkServer)
{
  char *cmd;
  char *exec;
  char *buf = NULL;
  blockResponse *reply = NULL;

  asprintf(&cmd, "%s %s", TARGETCLI_GLFS, BACKEND_CFGSTR);
  asprintf(&exec, cmd, name);

  gluster_block_1(blkServer, exec, EXEC_SRV, &reply);
  if (!reply || reply->exit) {
    ERROR("%s on host: %s",
          FAILED_GATHERING_CFGSTR, blkServer);
  }

  GB_FREE(cmd);
  GB_FREE(exec);

  if(reply->out)
    buf = strdup(reply->out);

  return buf;
}

blockResponse *
block_create_cli_1_svc(blockCreateCli *blk, struct svc_req *rqstp)
{
  int ret;
  uuid_t out;
  static blockCreate *cobj;
  static blockResponse *reply;
  char *gbid = CALLOC(UUID_BUF_SIZE);

  uuid_generate(out);
  uuid_unparse(out,gbid);

  ret = glusterBlockCreateEntry(blk, gbid);
  if (ret) {
    ERROR("%s volume: %s host: %s",
          FAILED_CREATING_FILE, blk->volume, blk->volfileserver);
    goto out;
  }

  if(GB_ALLOC(cobj) < 0)
    goto out;

  strcpy(cobj->volume, blk->volume);
  strcpy(cobj->volfileserver, blk->volfileserver);
  strcpy(cobj->block_name, blk->block_name);
  cobj->size = blk->size;
  strcpy(cobj->gbid, gbid);

  //for

  gluster_block_1(blk->block_hosts, cobj, CREATE_SRV, &reply);

out:
  return reply;
}

static size_t
glusterBlockCreateParseSize(char *value)
{
  char *postfix;
  char *tmp;
  size_t sizef;

  if (!value)
    return -1;

  sizef = strtod(value, &postfix);
  if (sizef < 0) {
    ERROR("%s", "size cannot be negative number\n");
    return -1;
  }

  tmp = postfix;
  if (*postfix == ' ')
    tmp = tmp + 1;

  switch (*tmp) {
  case 'Y':
    sizef *= 1024;
    /* fall through */
  case 'Z':
    sizef *= 1024;
    /* fall through */
  case 'E':
    sizef *= 1024;
    /* fall through */
  case 'P':
    sizef *= 1024;
    /* fall through */
  case 'T':
    sizef *= 1024;
    /* fall through */
  case 'G':
    sizef *= 1024;
    /* fall through */
  case 'M':
    sizef *= 1024;
    /* fall through */
  case 'K':
  case 'k':
    sizef *= 1024;
    /* fall through */
  case 'b':
  case '\0':
    return sizef;
    break;
  default:
    ERROR("%s", "You may use k/K, M, G or T suffixes for "
                "kilobytes, megabytes, gigabytes and terabytes.");
    return -1;
  }
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
  /*if (GB_STRDUP(blk->volume, p) < 0) {
    ret = -1;
    goto fail;
  }
  */
  strcpy(blk->volume, p);

  /* part between '@' and '/' is the server name */
  p = sep + 1;
  sep = strchr(p, '/');
  if (!sep) {
    ret = -1;
    goto fail;
  }

  *sep = '\0';
  /*if (GB_STRDUP(blk->volfileserver, p) < 0) {
    ret = -1;
    goto fail;
  }
  */
  strcpy(blk->volfileserver, p);

  /* part between '/' and '(' is the filename */
  p = sep + 1;
  sep = strchr(p, '(');
  if (!sep) {
    ret = -1;
    goto fail;
  }

  *(sep - 1) = '\0';  /* discard extra space at end of filename */
  /*if (GB_STRDUP(blk->gbid, p) < 0) {
    ret = -1;
    goto fail;
  }
  */
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


  /* part between ')' and '\n' is the status */
/*
  p = sep + 1;
  sep = strchr(p, '\n');
  if (!sep) {
    ret = -1;
    goto fail;
  }

  *sep = '\0';
  if (!strcmp(p, " activated"))
    blk->status = true;
*/
  return 0;

 fail:
//  glusterBlockDefFree(blk);

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
    return NULL;

  if (GB_ALLOC_N(obj->out, 4096) < 0) {
    GB_FREE(obj);
    return NULL;
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

  return obj;
}

blockResponse *
block_delete_cli_1_svc(blockDeleteCli *blk, struct svc_req *rqstp)
{
  char *cfgstring;
  static blockCreate *blkcfg;
  static blockDelete *cobj;
  static blockResponse *reply;

  if(GB_ALLOC(cobj) < 0)
    goto out;

  strcpy(cobj->block_name, blk->block_name);

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

  strcpy(cobj->gbid, blkcfg->gbid);
  gluster_block_1(blk->block_hosts, cobj, DELETE_SRV, &reply);

  if (glusterBlockDeleteEntry(blkcfg)) {
    ERROR("%s volume: %s host: %s",
          FAILED_DELETING_FILE, blkcfg->volume, blkcfg->volfileserver);
  }
out:
  return reply;
}


blockResponse *
block_delete_1_svc(blockDelete *blk, struct svc_req *rqstp)
{
  FILE *fp;
  char *backstore = NULL;
  char *iqn = NULL;
  char *exec = NULL;
  blockResponse *obj = NULL;

  asprintf(&iqn, "%s %s %s%s", TARGETCLI_ISCSI, DELETE, IQN_PREFIX, blk->gbid);

  asprintf(&backstore, "%s %s %s", TARGETCLI_GLFS,
           DELETE, blk->block_name);

  asprintf(&exec, "%s && %s", backstore, iqn);

  if(GB_ALLOC(obj) < 0)
    return NULL;

  if (GB_ALLOC_N(obj->out, 4096) < 0) {
    GB_FREE(obj);
    return NULL;
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
  blockResponse *reply;

  asprintf(&cmd, "%s %s", TARGETCLI_GLFS, LUNS_LIST);

  gluster_block_1(blk->block_hosts, cmd, EXEC_SRV, &reply);
  if (!reply || reply->exit) {
    ERROR("%s on host: %s",
        FAILED_GATHERING_CFGSTR, blk->block_hosts);
  }

  return reply;
}

blockResponse *
block_info_cli_1_svc(blockInfoCli *blk, struct svc_req *rqstp)
{
  char *cmd;
  blockResponse *reply;

  asprintf(&cmd, "%s/%s %s", TARGETCLI_GLFS, blk->block_name, INFO);

  //for
  gluster_block_1(blk->block_hosts, cmd, EXEC_SRV, &reply);
  if (!reply || reply->exit) {
    ERROR("%s on host: %s",
        FAILED_GATHERING_CFGSTR, blk->block_hosts);
  }

  return reply;
}
