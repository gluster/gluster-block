/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# define   _GNU_SOURCE         /* See feature_test_macros(7) */

# include  <string.h>
# include  <unistd.h>
# include  <getopt.h>
# include  <uuid/uuid.h>

# include "utils.h"
#include  "rpc/block.h"
# include "glfs-operations.h"


# define  UUID_BUF_SIZE    50
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
  CREATE_CLI = 1,
  LIST_CLI   = 2,
  INFO_CLI   = 3,
  DELETE_CLI = 4
} opterations;

typedef struct blockServerDef {
  size_t nhosts;
  char   **hosts;
} blockServerDef;
typedef blockServerDef *blockServerDefPtr;


static void
gluster_block_cli_1(void *cobj, opterations opt, blockResponse  **reply)
{
  CLIENT *clnt;
  int sockfd, len;
  struct sockaddr_un saun;

  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    perror("client: socket");
    exit(1);
  }

  saun.sun_family = AF_UNIX;
  strcpy(saun.sun_path, ADDRESS);

  len = sizeof(saun.sun_family) + strlen(saun.sun_path);

  if (connect(sockfd, (struct sockaddr *) &saun, len) < 0) {
    perror("client: connect");
    exit(1);
  }

  clnt = clntunix_create ((struct sockaddr_un *) &saun, GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS, &sockfd, 0, 0);
  if (clnt == NULL) {
    clnt_pcreateerror ("localhost");
    exit (1);
  }
switch(opt) {
  case CREATE_CLI:
    *reply = block_create_cli_1((blockCreateCli *)cobj, clnt);
    if (*reply == NULL) {
      clnt_perror (clnt, "call failed gluster-block");
    }
    break;
  case DELETE_CLI:
    *reply = block_delete_cli_1((blockDeleteCli *)cobj, clnt);
    if (*reply == NULL) {
      clnt_perror (clnt, "call failed gluster-block");
    }
    break;
  case INFO_CLI:
    *reply = block_info_cli_1((blockInfoCli *)cobj, clnt);
    if (*reply == NULL) {
      clnt_perror (clnt, "call failed gluster-block");
    }
    break;
  case LIST_CLI:
    *reply = block_list_cli_1((blockListCli *)cobj, clnt);
    if (*reply == NULL) {
      clnt_perror (clnt, "call failed gluster-block");
    }
    break;
}
  printf("%s\n", (*reply)->out);

  clnt_destroy (clnt);
}


static void
glusterBlockHelp(void)
{
  MSG("%s",
      "gluster-block (Version 0.1) \n"
      " -c, --create      <name>          Create the gluster block\n"
      "     -v, --volume       <vol>            gluster volume name\n"
      "     -h, --host         <gluster-node>   node addr from gluster pool\n"
      "     -s, --size         <size>           block storage size in KiB|MiB|GiB|TiB..\n"
      "\n"
      " -l, --list                        List available gluster blocks\n"
      "\n"
      " -i, --info        <name>          Details about gluster block\n"
      "\n"
      " -m, --modify      <resize|auth>   Modify the metadata\n"
      "\n"
      " -d, --delete      <name>          Delete the gluster block\n"
      "\n"
      "     [-b, --block-host <IP1,IP2,IP3...>]  block servers, clubbed with any option\n");
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

/*
static void
glusterBlockDefFree(glusterBlockDefPtr blk)
{
  if (!blk)
    return;

  GB_FREE(blk->volume);
  GB_FREE(blk->host);
  GB_FREE(blk->filename);
  GB_FREE(blk);
}
*/

static blockServerDefPtr
blockServerParse(char *blkServers)
{
  blockServerDefPtr list;
  char *tmp = blkServers;
  size_t i = 0;

  if (!blkServers)
    return NULL;

  if (GB_ALLOC(list) < 0)
    return NULL;

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

/*
static char *
glusterBlockListGetHumanReadableSize(size_t bytes)
{
  char *size;
  char *types[] = {"Byte(s)", "KiB", "MiB", "GiB",
                   "TiB", "PiB", "EiB", "ZiB", "YiB"};
  size_t i;

  if (bytes < 1024) {
    asprintf(&size, "%zu %s", bytes, *(types));
    return size;
  }

  for (i = 1; i < 8; i++) {
    bytes /= 1024;
    if (bytes < 1024) {
      asprintf(&size, "%zu %s    ", bytes, *(types + i));
      return size;
    }
  }

  return NULL;
}
*/

static int
glusterBlockCreate(int count, char **options, char *name)
{
  int c;
  int ret = 0;
// char *cmd = NULL;
//  char *exec = NULL;
//  char *iqn = NULL;
//  char *blkServers = NULL;
//  blockServerDefPtr list;
//  uuid_t out;
//  glusterBlockDefPtr blk;
  blockResponse *reply;
  static blockCreateCli cobj;
//  size_t i;

/*
  if (GB_ALLOC(&cobj) < 0)
    return -1;
  if (GB_ALLOC(blk) < 0)
    return -1;

  blk->filename = CALLOC(UUID_BUF_SIZE);

  uuid_generate(out);
  uuid_unparse(out, blk->filename);
*/

  if (!name) {
    ERROR("%s", "Insufficient arguments supplied for"
                "'gluster-block create'");
    ret = -1;
    goto out;
  }

  strcpy(cobj.block_name, name);

  while (1) {
    static const struct option long_options[] = {
      {"volume",     required_argument, 0, 'v'},
      {"host",       required_argument, 0, 'h'},
      {"size",       required_argument, 0, 's'},
      {"block-host", required_argument, 0, 'b'},
      {0, 0, 0, 0}
    };

    /* getopt_long stores the option index here. */
    int option_index = 0;

    c = getopt_long(count, options, "b:v:h:s:",
                    long_options, &option_index);

    if (c == -1)
      break;

    switch (c) {
    case 'b':
      //blkServers = optarg;
      GB_STRDUP(cobj.block_hosts, optarg);
      break;

    case 'v':
      strcpy(cobj.volume, optarg);
      ret++;
      break;

    case 'h':
      strcpy(cobj.volfileserver, optarg);
      ret++;
      break;

    case 's':
      cobj.size = glusterBlockCreateParseSize(optarg);
      if (cobj.size < 0) {
        ERROR("%s", "failed while parsing size");
        ret = -1;
        goto out;
      }
      ret++;
      break;

    case '?':
      MSG("unrecognized option '%s'", options[optind-1]);
      MSG("%s", "Hint: gluster-block --help");
      goto out;

    default:
      break;
    }
  }
/*
  if (blkServers) {
    list = blockServerParse(blkServers);
  } else {
    list = blockServerParse("localhost");
  }
*/
  /* Print any remaining command line arguments (not options). */
  if (optind < count) {
    ERROR("%s", "non-option ARGV-elements: ");
    while (optind < count)
      printf("%s ", options[optind++]);
    putchar('\n');

    ret = -1;
    goto out;
  }

  if (ret != 3) {
    ERROR("%s", "Insufficient arguments supplied for"
                "'gluster-block create'\n");
    ret = -1;
    goto out;
  }

  gluster_block_cli_1(&cobj, CREATE_CLI, &reply);





/*
  ret = glusterBlockCreateEntry(blk);
  if (ret) {
    ERROR("%s volume: %s host: %s",
          FAILED_CREATING_FILE, blk->volume, blk->host);
    goto out;
  }

  if (asprintf(&cmd, "%s %s %s %zu %s@%s/%s %s", TARGETCLI_GLFS,
               CREATE, name, blk->size, blk->volume, blk->host,
               blk->filename, blk->filename) < 0)
    goto out;

  for (i = 0; i < list->nhosts; i++) {
    MSG("[OnHost: %s]", list->hosts[i]);
    gluster_block_1(list->hosts[i], cmd, &reply);
    if (!reply || reply->exit) {
      ERROR("%s on host: %s",
            FAILED_CREATING_BACKEND, list->hosts[i]);
      ret = -1;
      goto out;
    }
    MSG("%s", reply->out);

    asprintf(&iqn, "%s%s", IQN_PREFIX, blk->filename);
    asprintf(&exec, "%s %s %s", TARGETCLI_ISCSI, CREATE, iqn);
    gluster_block_1(list->hosts[i], exec, &reply);
    if (!reply || reply->exit) {
      ERROR("%s on host: %s",
            FAILED_CREATING_IQN, list->hosts[i]);
      ret = -1;
      goto out;
    }
    MSG("%s", reply->out);
    GB_FREE(exec);

    asprintf(&exec, "%s/%s/tpg1/luns %s %s/%s",
             TARGETCLI_ISCSI, iqn, CREATE, GLFS_PATH, name);
    gluster_block_1(list->hosts[i], exec, &reply);
    if (!reply || reply->exit) {
      ERROR("%s on host: %s",
            FAILED_CREATING_LUN, list->hosts[i]);
      ret = -1;
      goto out;
    }
    MSG("%s", reply->out);
    GB_FREE(exec);

    asprintf(&exec, "%s/%s/tpg1 set attribute %s",
             TARGETCLI_ISCSI, iqn, ATTRIBUTES);
    gluster_block_1(list->hosts[i], exec, &reply);
    if (!reply || reply->exit) {
      ERROR("%s on host: %s",
            FAILED_SETTING_ATTRIBUTES, list->hosts[i]);
      ret = -1;
      goto out;
    }
    MSG("%s", reply->out);
    GB_FREE(exec);
    GB_FREE(iqn);

    gluster_block_1(list->hosts[i], TARGETCLI_SAVE, &reply);
    if (!reply || reply->exit) {
      ERROR("%s on host: %s",
            FAILED_SAVEING_CONFIG, list->hosts[i]);
      ret = -1;
      goto out;
    }
    MSG("%s", reply->out);
    putchar('\n');
  }

 out:
  GB_FREE(cmd);
  GB_FREE(exec);
  GB_FREE(iqn);
  glusterBlockDefFree(blk);
  blockServerDefFree(list);
*/

  out:
  return reply->exit;
}

/*
static int
glusterBlockParseCfgStringToDef(char* cfgstring,
                                glusterBlockDefPtr blk)
{
  int ret = 0;
  char *p, *sep;

 // part before '@' is the volume name
  p = cfgstring;
  sep = strchr(p, '@');
  if (!sep) {
    ret = -1;
    goto fail;
  }

  *sep = '\0';
  if (GB_STRDUP(blk->volume, p) < 0) {
    ret = -1;
    goto fail;
  }

  // part between '@' and '/' is the server name
  p = sep + 1;
  sep = strchr(p, '/');
  if (!sep) {
    ret = -1;
    goto fail;
  }

  *sep = '\0';
  if (GB_STRDUP(blk->host, p) < 0) {
    ret = -1;
    goto fail;
  }

  // part between '/' and '(' is the filename
  p = sep + 1;
  sep = strchr(p, '(');
  if (!sep) {
    ret = -1;
    goto fail;
  }

  *(sep - 1) = '\0';  // discard extra space at end of filename
  if (GB_STRDUP(blk->filename, p) < 0) {
    ret = -1;
    goto fail;
  }

  // part between '(' and ')' is the size
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


  // part between ')' and '\n' is the status
  p = sep + 1;
  sep = strchr(p, '\n');
  if (!sep) {
    ret = -1;
    goto fail;
  }

  *sep = '\0';
  if (!strcmp(p, " activated"))
    blk->status = true;

  return 0;

 fail:
  glusterBlockDefFree(blk);

  return ret;
}
*/

/*
static char *
getCfgstring(char* name, char *blkServer)
{
  char *cmd;
  char *exec;
  char *buf = NULL;
  blockResponse *reply = NULL;

  asprintf(&cmd, "%s %s", TARGETCLI_GLFS, BACKEND_CFGSTR);
  asprintf(&exec, cmd, name);

  //gluster_block_1(blkServer, exec, &reply);
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
*/

/* TODO: need to implement sessions [ list | detail ] [sid] */
static int
glusterBlockList(char *blkServers)
{
  /*size_t i;
  int ret;
  char *cmd;
  char *pos;
  char *size;
  char *cfgstring = NULL;
  glusterBlockDefPtr blk = NULL;*/

  static blockListCli cobj;
  blockResponse *reply = NULL;

  GB_STRDUP(cobj.block_hosts, blkServers);

  gluster_block_cli_1(&cobj, LIST_CLI, &reply);

  /*
  MSG("%s", "BlockName      Volname      Host      Size      Status");

  asprintf(&cmd, "%s %s", TARGETCLI_GLFS, LUNS_LIST);

  for (i = 0; i < blkServers->nhosts; i++) {
    MSG("[OnHost: %s]", blkServers->hosts[i]);
    //gluster_block_1(blkServers->hosts[i], cmd, &reply);
    if (!reply || reply->exit) {
      ERROR("%s on host: %s",
            FAILED_LIST_BACKEND, blkServers->hosts[i]);
      ret = -1;
      goto fail;
    }

    pos = strtok (reply->out, "\n");
    while (pos != NULL) {

      cfgstring = getCfgstring(pos, blkServers->hosts[i]);
      if (!cfgstring) {
        ERROR("%s", "failed while gathering CfgString");
        ret = -1;
        goto fail;
      }

      if (GB_ALLOC(blk) < 0)
        goto fail;

      ret = glusterBlockParseCfgStringToDef(cfgstring, blk);
      if (ret) {
        ERROR("%s", "failed while parsing CfgString to glusterBlockDef");
        goto fail;
      }

      fprintf(stdout, " %s   %s   %s   %s   %s\n",
              pos, blk->volume, blk->host,
              size = glusterBlockListGetHumanReadableSize(blk->size),
              blk->status ? "Online" : "Offline");

      GB_FREE(cfgstring);
      glusterBlockDefFree(blk);
      pos = strtok (NULL, "\n");
    }

    putchar('\n');

    GB_FREE(size);
  }

  GB_FREE(cmd);
  return 0;

 fail:
  glusterBlockDefFree(blk);
  GB_FREE(cfgstring);

  GB_FREE(cmd);

  return -1;
  */
  return 0;
}


static int
glusterBlockDelete(char* name, char *blkServers)
{
  static blockDeleteCli cobj;
  blockResponse *reply;

  strcpy(cobj.block_name, name);
  GB_STRDUP(cobj.block_hosts, blkServers);

  gluster_block_cli_1(&cobj, DELETE_CLI, &reply);

  /*

  asprintf(&cmd, "%s %s %s", TARGETCLI_GLFS, DELETE, name);

  for (i = 0; i < blkServers->nhosts; i++) {
    MSG("[OnHost: %s]", blkServers->hosts[i]);
    if (cfgstring)
      GB_FREE(cfgstring);
    cfgstring = getCfgstring(name, blkServers->hosts[i]);
    if (!cfgstring) {
      ERROR("%s", "failed while gathering CfgString");
      ret = -1;
      goto fail;
    }

    //gluster_block_1(blkServers->hosts[i], cmd, &reply);
    if (!reply || reply->exit) {
      ERROR("%s on host: %s",
            FAILED_DELETING_BACKEND, blkServers->hosts[i]);
      ret = -1;
      goto fail;
    }
    MSG("%s", reply->out);
    if (!blk) {
      if (GB_ALLOC(blk) < 0)
        goto fail;

      ret = glusterBlockParseCfgStringToDef(cfgstring, blk);
      if (ret) {
        ERROR("%s", "failed while parsing CfgString to glusterBlockDef");
        goto fail;
      }
    }

    asprintf(&iqn, "%s%s", IQN_PREFIX, blk->filename);
    asprintf(&exec, "%s %s %s", TARGETCLI_ISCSI, DELETE, iqn);
    //gluster_block_1(blkServers->hosts[i], exec, &reply);
    if (!reply || reply->exit) {
      ERROR("%s on host: %s",
            FAILED_DELETING_IQN, blkServers->hosts[i]);
      ret = -1;
      goto fail;
    }
    MSG("%s", reply->out);
    GB_FREE(exec);
    GB_FREE(iqn);

    //gluster_block_1(blkServers->hosts[i], TARGETCLI_SAVE, &reply);
    if (!reply || reply->exit) {
      ERROR("%s on host: %s",
            FAILED_SAVEING_CONFIG, blkServers->hosts[i]);
      ret = -1;
      goto fail;
    }
    MSG("%s", reply->out);
    putchar('\n');
  }

  //ret = glusterBlockDeleteEntry(blk);
  if (ret) {
    ERROR("%s volume: %s host: %s",
          FAILED_DELETING_FILE, blk->volume, blk->host);
  }

 fail:
  glusterBlockDefFree(blk);
  GB_FREE(cfgstring);
  GB_FREE(exec);
  GB_FREE(cmd);
*/
  return reply->exit;
}


static int
glusterBlockInfo(char* name, char *blkServers)
{
  static blockInfoCli cobj;
  blockResponse *reply;

  strcpy(cobj.block_name, name);
  GB_STRDUP(cobj.block_hosts, blkServers);

  gluster_block_cli_1(&cobj, INFO_CLI, &reply);

  /*asprintf(&cmd, "%s/%s %s", TARGETCLI_GLFS, name, INFO);

  for (i = 0; i < blkServers->nhosts; i++) {
    MSG("[OnHost: %s]", blkServers->hosts[i]);
  gluster_block_1(blkServers->hosts[i], cmd, &reply);
    if (!reply || reply->exit) {
      ret = -1;
      ERROR("%s on host: %s",
            FAILED_GATHERING_INFO, blkServers->hosts[i]);
      goto fail;
    }
    MSG("%s", reply->out);
    putchar('\n');
  }

 fail:
  GB_FREE(cmd);
*/
  return 0;
}


static int
glusterBlockParseArgs(int count, char **options)
{
  int c;
  int ret = 0;
  int optFlag = 0;
  char *block = NULL;
  char *blkServers = NULL;
  blockServerDefPtr list = NULL;
  char *liststr;

  while (1) {
    static const struct option long_options[] = {
      {HELP,      no_argument,       0, 'h'},
      {CREATE,    required_argument, 0, 'c'},
      {DELETE,    required_argument, 0, 'd'},
      {LIST,      no_argument,       0, 'l'},
      {INFO,      required_argument, 0, 'i'},
      {MODIFY,    required_argument, 0, 'm'},
      {BLOCKHOST, required_argument, 0, 'b'},
      {0, 0, 0, 0}
    };

    /* getopt_long stores the option index here. */
    int option_index = 0;

    c = getopt_long(count, options, "hc:b:d:lim:",
                    long_options, &option_index);

    /* Detect the end of the options. */
    if (c == -1)
      break;

    switch (c) {
    case 'b':
      blkServers = optarg;
      if (optFlag)
        goto opt;
      break;

    case 'c':
      ret = glusterBlockCreate(count, options, optarg);
      if (ret) {
        ERROR("%s", FAILED_CREATE);
        goto out;
      }
      break;

    case 'l':
    case 'd':
    case 'i':
      if (optFlag) /* more than one main opterations ?*/
        goto out;
      optFlag = c;
      block = optarg;
      if (blkServers)
        goto opt;
      break;

    case 'm':
      MSG("option --modify yet TODO '%s'", optarg);
      break;

    case 'h':
      glusterBlockHelp();
      break;

    case '?':
      /* getopt_long already printed an error message. */
      break;
    }
  }

 opt:
  if (blkServers) {
    list = blockServerParse(blkServers);
    liststr = blkServers;
  } else {
    list = blockServerParse("localhost");
    liststr = "localhost";
  }

  switch (optFlag) {
  case 'l':
    ret = glusterBlockList(liststr);
    if (ret)
        ERROR("%s", FAILED_LIST);
    break;
  case 'i':
    ret = glusterBlockInfo(block, liststr);
    if (ret)
        ERROR("%s", FAILED_INFO);
    break;
  case 'd':
    ret = glusterBlockDelete(block, liststr);
    if (ret)
        ERROR("%s", FAILED_DELETE);
    break;
  }

 out:
  if (ret == 0 && optind < count) {
    ERROR("%s", "Unable to parse elements: ");
    while (optind < count)
      printf("%s ", options[optind++]);
    putchar('\n');
    MSG("Hint: %s --help", options[0]);
  }

  blockServerDefFree(list);
  return ret;
}


int
main(int argc, char *argv[])
{
  int ret;
  if (argc <= 1)
    glusterBlockHelp();

  ret = glusterBlockParseArgs(argc, argv);

  return ret;
}
