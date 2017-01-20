#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include   <stdio.h>
#include <netdb.h>
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

blockResponse *
block_create_cli_1_svc(blockCreateCli *blk, struct svc_req *rqstp)
{
  //FILE *fp;
  CLIENT *clnt;
  int sockfd;
  struct hostent *server;
  struct sockaddr_in sain;
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

  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    perror("gluster-blockd: socket");
    exit(1);
  }
  server = gethostbyname(blk->block_hosts);
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

  reply = block_create_1(cobj, clnt);
  if (reply == NULL) {
    clnt_perror (clnt, "call failed gluster-blockd");
  }

  clnt_destroy (clnt);

out:
  return reply;
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

