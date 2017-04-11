/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# include  "common.h"
# include  "block.h"
# include  "config.h"
# include  <ctype.h>



typedef enum clioperations {
  CREATE_CLI = 1,
  LIST_CLI   = 2,
  INFO_CLI   = 3,
  DELETE_CLI = 4,
  MODIFY_CLI = 5
} clioperations;

const char *argp_program_version = ""                                 \
  PACKAGE_NAME" ("PACKAGE_VERSION")"                                  \
  "\nRepository rev: https://github.com/gluster/gluster-block.git\n"  \
  "Copyright (c) 2016 Red Hat, Inc. <https://redhat.com/>\n"          \
  "gluster-block comes with ABSOLUTELY NO WARRANTY.\n"                \
  "It is licensed to you under your choice of the GNU Lesser\n"       \
  "General Public License, version 3 or any later version (LGPLv3\n"  \
  "or later), or the GNU General Public License, version 2 (GPLv2),\n"\
  "in all cases as published by the Free Software Foundation.";

#define GB_CREATE_HELP_STR "gluster-block create <volname/blockname> "\
                           "[ha <count>] [auth enable|disable] "\
                           "<HOST1[,HOST2,...]> <size> [--json*]"

#define GB_DELETE_HELP_STR "gluster-block delete <volname/blockname> [--json*]"
#define GB_MODIFY_HELP_STR "gluster-block modify <volname/blockname> "\
                           "<auth enable|disable> [--json*]"
#define GB_INFO_HELP_STR  "gluster-block info <volname/blockname> [--json*]"
#define GB_LIST_HELP_STR  "gluster-block list <volname> [--json*]"

static int
glusterBlockCliRPC_1(void *cobj, clioperations opt, char **out)
{
  CLIENT *clnt = NULL;
  int ret = -1;
  int sockfd = -1;
  struct sockaddr_un saun = {0,};
  blockCreateCli *create_obj;
  blockDeleteCli *delete_obj;
  blockInfoCli *info_obj;
  blockListCli *list_obj;
  blockModifyCli *modify_obj;
  blockResponse *reply = NULL;


  if (strlen(GB_UNIX_ADDRESS) > SUN_PATH_MAX) {
    LOG("cli", GB_LOG_ERROR,
        "%s: path length is more than SUN_PATH_MAX: (%zu > %zu chars)",
        GB_UNIX_ADDRESS, strlen(GB_UNIX_ADDRESS), SUN_PATH_MAX);
    goto out;
  }

  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    LOG("cli", GB_LOG_ERROR, "%s: socket creation failed (%s)", GB_UNIX_ADDRESS,
        strerror (errno));
    goto out;
  }

  saun.sun_family = AF_UNIX;
  strcpy(saun.sun_path, GB_UNIX_ADDRESS);

  if (connect(sockfd, (struct sockaddr *) &saun,
              sizeof(struct sockaddr_un)) < 0) {
    if (errno == ENOENT || errno == ECONNREFUSED) {
      MSG("%s\n", "Connection failed. Please check if gluster-block daemon is operational.");
      if (sockfd != -1) {
        close (sockfd);
      }
      return -1;
    }
    LOG("cli", GB_LOG_ERROR, "%s: connect failed (%s)", GB_UNIX_ADDRESS,
        strerror (errno));
    goto out;
  }

  clnt = clntunix_create((struct sockaddr_un *) &saun,
                         GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS,
                         &sockfd, 0, 0);
  if (!clnt) {
    LOG("cli", GB_LOG_ERROR, "%s, unix addr %s",
        clnt_spcreateerror("client create failed"), GB_UNIX_ADDRESS);
    goto out;
  }

  switch(opt) {
  case CREATE_CLI:
    create_obj = cobj;
    reply = block_create_cli_1(create_obj, clnt);
    if (!reply) {
      LOG("cli", GB_LOG_ERROR,
          "%sblock %s create on volume %s with hosts %s failed\n",
          clnt_sperror(clnt, "block_create_cli_1"), create_obj->block_name,
          create_obj->volume, create_obj->block_hosts);
      goto out;
    }
    break;
  case DELETE_CLI:
    delete_obj = cobj;
    reply = block_delete_cli_1(delete_obj, clnt);
    if (!reply) {
      LOG("cli", GB_LOG_ERROR, "%sblock %s delete on volume %s failed",
          clnt_sperror(clnt, "block_delete_cli_1"),
          delete_obj->block_name, delete_obj->volume);
      goto out;
    }
    break;
  case INFO_CLI:
    info_obj = cobj;
    reply = block_info_cli_1(info_obj, clnt);
    if (!reply) {
      LOG("cli", GB_LOG_ERROR, "%sblock %s info on volume %s failed",
          clnt_sperror(clnt, "block_info_cli_1"),
          info_obj->block_name, info_obj->volume);
      goto out;
    }
    break;
  case LIST_CLI:
    list_obj = cobj;
    reply = block_list_cli_1(list_obj, clnt);
    if (!reply) {
      LOG("cli", GB_LOG_ERROR, "%sblock list on volume %s failed",
          clnt_sperror(clnt, "block_list_cli_1"), list_obj->volume);
      goto out;
    }
    break;
  case MODIFY_CLI:
    modify_obj = cobj;
    reply = block_modify_cli_1(modify_obj, clnt);
    if (!reply) {
      LOG("cli", GB_LOG_ERROR, "%sblock modify on volume %s failed",
          clnt_sperror(clnt, "block_modify_cli_1"), modify_obj->volume);
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
      LOG("cli", GB_LOG_ERROR, "%s",
          clnt_sperror(clnt, "clnt_freeres failed"));
    }
    clnt_destroy (clnt);
  }

  if (sockfd != -1) {
    close (sockfd);
  }

  return ret;
}


static void
glusterBlockHelp(void)
{
  MSG("%s",
      PACKAGE_NAME" ("PACKAGE_VERSION")\n"
      "usage:\n"
      "  gluster-block <command> <volname[/blockname]> [<args>] [--json*]\n"
      "\n"
      "commands:\n"
      "  create  <volname/blockname> [ha <count>] [auth enable|disable] <host1[,host2,...]> <size>\n"
      "        create block device.\n"
      "\n"
      "  list    <volname>\n"
      "        list available block devices.\n"
      "\n"
      "  info    <volname/blockname>\n"
      "        details about block device.\n"
      "\n"
      "  delete  <volname/blockname>\n"
      "        delete block device.\n"
      "\n"
      "  modify  <volname/blockname> <auth enable|disable>\n"
      "        modify block device.\n"
      "\n"
      "  help\n"
      "        show this message and exit.\n"
      "\n"
      "  version\n"
      "        show version info and exit.\n"
      "\n"
      "supported JSON formats:\n"
      "  --json|--json-plain|--json-spaced|--json-pretty\n"
      );
}

static bool
glusterBlockIsNameAcceptable (char *name)
{
  int i = 0;
  if (!name || strlen(name) == 0)
    return FALSE;
  for (i = 0; i < strlen(name); i++) {
    if (!isalnum (name[i]) && (name[i] != '_') && (name[i] != '-'))
      return FALSE;
  }
  return TRUE;
}

static int
glusterBlockParseVolumeBlock(char *volumeblock, char *volume, char *block,
                             char *helpstr, char *op)
{
  int ret = -1;
  size_t len = 0;
  char *sep = NULL;

  /* part before '/' is the volume name */
  sep = strchr(volumeblock, '/');
  if (!sep) {
    MSG("argument '<volname/blockname>'(%s) doesn't seems to be right",
        volumeblock);
    MSG("%s\n", helpstr);
    LOG("cli", GB_LOG_ERROR, "%s failed while parsing <volname/blockname>", op);
    goto out;
  }
  len = sep - volumeblock;
  if (len >= 255 || strlen(sep+1) >= 255) {
    MSG("%s\n", "Both volname and blockname should be less than 255 "
        "characters long");
    MSG("%s\n", helpstr);
    LOG("cli", GB_LOG_ERROR, "%s failed while parsing <volname/blockname>", op);
    goto out;
  }
  strncpy(volume, volumeblock, len);
  /* part after / is blockname */
  strncpy(block, sep+1, strlen(sep+1));
  if (!glusterBlockIsNameAcceptable (volume)) {
    MSG("volume name(%s) should contain only aplhanumeric,'-' "
        "and '_' characters", volume);
    goto out;
  }
  if (!glusterBlockIsNameAcceptable (block)) {
    MSG("block name(%s) should contain only aplhanumeric,'-' "
        "and '_' characters", block);
    goto out;
  }
  ret = 0;
 out:
  return ret;
}

static int
glusterBlockModify(int argcount, char **options, int json)
{
  size_t optind = 2;
  blockModifyCli mobj = {0, };
  int ret = -1;
  char *out = NULL;

  mobj.json_resp = json;
  if (argcount != 5) {
    MSG("%s\n", "Insufficient arguments for modify:");
    MSG("%s\n", GB_MODIFY_HELP_STR);
    return -1;
  }

  if (glusterBlockParseVolumeBlock (options[optind++], mobj.volume,
                                    mobj.block_name, GB_MODIFY_HELP_STR,
                                    "modify")) {
    goto out;
  }

  /* if auth given then collect status which is next by 'auth' arg */
  if (!strcmp(options[optind], "auth")) {
    optind++;
    if(strcmp (options[optind], "enable") == 0) {
       mobj.auth_mode = 1;
    } else if (strcmp (options[optind], "disable") == 0) {
       mobj.auth_mode = 0;
    } else {
      MSG("%s\n", "argument to 'auth' doesn't seems to be right");
      MSG("%s\n", GB_MODIFY_HELP_STR);
      LOG("cli", GB_LOG_ERROR, "Modify failed while parsing argument "
                               "to auth  for <%s/%s>",
                               mobj.volume, mobj.block_name);
      goto out;
    }
  }

  ret = glusterBlockCliRPC_1(&mobj, MODIFY_CLI, &out);
  if (ret) {
    LOG("cli", GB_LOG_ERROR,
        "failed getting info of block %s on volume %s",
        mobj.block_name, mobj.volume);
  }

  if (out) {
    MSG("%s", out);
  }

 out:
  GB_FREE(out);

  return ret;
}

static int
glusterBlockCreate(int argcount, char **options, int json)
{
  size_t optind = 2;
  int ret = -1;
  ssize_t sparse_ret;
  char *out = NULL;
  blockCreateCli cobj = {0, };


  cobj.json_resp = json;
  if (argcount <= optind) {
    MSG("%s\n", "Insufficient arguments for create:");
    MSG("%s\n", GB_CREATE_HELP_STR);
    return -1;
  }

  /* default mpath */
  cobj.mpath = 1;

  if (glusterBlockParseVolumeBlock (options[optind++], cobj.volume,
                                    cobj.block_name, GB_CREATE_HELP_STR,
                                    "create")) {
    goto out;
  }

  if (argcount - optind >= 2) {  /* atleast 2 needed */
    /* if ha given then collect count which is next by 'ha' arg */
    if (!strcmp(options[optind], "ha")) {
      optind++;
      sscanf(options[optind++], "%u", &cobj.mpath);
    }
  }

  if (argcount - optind >= 2) {  /* atleast 2 needed */
    /* if auth given then collect boolean which is next by 'auth' arg */
    if (!strcmp(options[optind], "auth")) {
      optind++;
      if(strcmp (options[optind], "enable") == 0) {
         cobj.auth_mode = 1;
      } else if (strcmp (options[optind], "disable") == 0) {
         cobj.auth_mode = 0;
      } else {
        MSG("%s\n", "argument to 'auth' doesn't seems to be right");
        MSG("%s\n", GB_CREATE_HELP_STR);
        LOG("cli", GB_LOG_ERROR, "Create failed while parsing argument "
                                 "to auth  for <%s/%s>",
                                 cobj.volume, cobj.block_name);
        goto out;
      }
      optind++;
    }
  }

  if (argcount - optind < 2) {  /* left with servers and size so 2 */
    MSG("%s\n", "Insufficient arguments for create");
    MSG("%s\n", GB_CREATE_HELP_STR);
    LOG("cli", GB_LOG_ERROR,
        "failed creating block %s on volume %s with hosts %s",
        cobj.block_name, cobj.volume, cobj.block_hosts);
    goto out;
  }

  /* next arg to 'ha count' will be servers */
  if (GB_STRDUP(cobj.block_hosts, options[optind++]) < 0) {
    LOG("cli", GB_LOG_ERROR, "failed while parsing servers for block <%s/%s>",
        cobj.volume, cobj.block_name);
    goto out;
  }

  /* last arg will be size */
  sparse_ret = glusterBlockCreateParseSize("cli", options[optind]);
  if (sparse_ret < 0) {
    MSG("%s\n", "last argument '<size>' doesn't seems to be right");
    MSG("%s\n", GB_CREATE_HELP_STR);
    LOG("cli", GB_LOG_ERROR, "failed while parsing size for block <%s/%s>",
        cobj.volume, cobj.block_name);
    goto out;
  }
  cobj.size = sparse_ret;  /* size is unsigned long long */

  ret = glusterBlockCliRPC_1(&cobj, CREATE_CLI, &out);
  if (ret) {
    LOG("cli", GB_LOG_ERROR,
        "failed creating block %s on volume %s with hosts %s",
        cobj.block_name, cobj.volume, cobj.block_hosts);
  }

  if (out) {
    MSG("%s", out);
  }

 out:
  GB_FREE(cobj.block_hosts);
  GB_FREE(out);

  return ret;
}


static int
glusterBlockList(int argcount, char **options, int json)
{
  blockListCli cobj = {0};
  char *out = NULL;
  int ret = -1;


  cobj.json_resp = json;
  if (argcount != 3) {
    MSG("%s\n", "Insufficient arguments for list:");
    MSG("%s\n", GB_LIST_HELP_STR);
    return -1;
  }

  strcpy(cobj.volume, options[2]);

  ret = glusterBlockCliRPC_1(&cobj, LIST_CLI, &out);
  if (ret) {
    LOG("cli", GB_LOG_ERROR, "failed listing blocks from volume %s",
        cobj.volume);
  }

  if (out) {
    MSG("%s", out);
  }

  GB_FREE(out);

  return ret;
}


static int
glusterBlockDelete(int argcount, char **options, int json)
{
  blockDeleteCli cobj = {0};
  char *out = NULL;
  int ret = -1;


  cobj.json_resp = json;
  if (argcount != 3) {
    MSG("%s\n", "Insufficient arguments for delete:");
    MSG("%s\n", GB_DELETE_HELP_STR);
    return -1;
  }


  if (glusterBlockParseVolumeBlock (options[2], cobj.volume,
                                    cobj.block_name, GB_DELETE_HELP_STR,
                                    "delete")) {
    goto out;
  }

  ret = glusterBlockCliRPC_1(&cobj, DELETE_CLI, &out);
  if (ret) {
    LOG("cli", GB_LOG_ERROR, "failed deleting block %s on volume %s",
        cobj.block_name, cobj.volume);
  }

  if (out) {
    MSG("%s", out);
  }

 out:
  GB_FREE(out);

  return ret;
}


static int
glusterBlockInfo(int argcount, char **options, int json)
{
  blockInfoCli cobj = {0};
  char *out = NULL;
  int ret = -1;


  cobj.json_resp = json;
  if (argcount != 3) {
    MSG("%s\n", "Insufficient arguments for info:");
    MSG("%s\n", GB_INFO_HELP_STR);
    return -1;
  }


  if (glusterBlockParseVolumeBlock (options[2], cobj.volume,
                                    cobj.block_name, GB_INFO_HELP_STR,
                                    "info")) {
    goto out;
  }

  ret = glusterBlockCliRPC_1(&cobj, INFO_CLI, &out);
  if (ret) {
    LOG("cli", GB_LOG_ERROR,
        "failed getting info of block %s on volume %s",
        cobj.block_name, cobj.volume);
  }

  if (out) {
    MSG("%s", out);
  }

 out:
  GB_FREE(out);

  return ret;
}


static int
glusterBlockParseArgs(int count, char **options)
{
  int ret = 0;
  size_t opt = 0;
  int json = GB_JSON_NONE;


  opt = glusterBlockCLIOptEnumParse(options[1]);
  if (!opt || opt >= GB_CLI_OPT_MAX) {
    MSG("unknow option: %s\n", options[1]);
    return -1;
  }

  if (opt > 0 && opt < GB_CLI_HELP) {
          json = jsonResponseFormatParse (options[count-1]);
          if (json == GB_JSON_MAX) {
            MSG("expecting '--json*', but argument %s doesn't seem to be matching",
                options[count-1]);
            return -1;
          } else if (json != GB_JSON_NONE) {
                  count--;/*Commands don't need to handle json*/
          }
  }

  while (1) {
    switch (opt) {
    case GB_CLI_CREATE:
      ret = glusterBlockCreate(count, options, json);
      if (ret && ret != EEXIST) {
        LOG("cli", GB_LOG_ERROR, "%s", FAILED_CREATE);
      }
      goto out;

    case GB_CLI_LIST:
      ret = glusterBlockList(count, options, json);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, "%s", FAILED_LIST);
      }
      goto out;

    case GB_CLI_INFO:
      ret = glusterBlockInfo(count, options, json);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, "%s", FAILED_INFO);
      }
      goto out;

    case GB_CLI_MODIFY:
      ret = glusterBlockModify(count, options, json);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, "%s", FAILED_MODIFY);
      }
      goto out;

    case GB_CLI_DELETE:
      ret = glusterBlockDelete(count, options, json);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, "%s", FAILED_DELETE);
      }
      goto out;

    case GB_CLI_HELP:
    case GB_CLI_HYPHEN_HELP:
    case GB_CLI_USAGE:
    case GB_CLI_HYPHEN_USAGE:
      glusterBlockHelp();
      goto out;

    case GB_CLI_VERSION:
    case GB_CLI_HYPHEN_VERSION:
      MSG("%s\n", argp_program_version);
      goto out;
    }
  }

 out:
  return ret;
}


int
main(int argc, char *argv[])
{
  if (argc <= 1) {
    glusterBlockHelp();
  }

  return glusterBlockParseArgs(argc, argv);
}
