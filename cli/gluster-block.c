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



typedef enum clioperations {
  CREATE_CLI = 1,
  LIST_CLI   = 2,
  INFO_CLI   = 3,
  DELETE_CLI = 4
} clioperations;

const char *argp_program_version = ""                                 \
  PACKAGE_NAME" ("PACKAGE_VERSION")"                                  \
  "\nRepository rev: https://github.com/pkalever/gluster-block.git\n" \
  "Copyright (c) 2016 Red Hat, Inc. <https://redhat.com/>\n"          \
  "gluster-block comes with ABSOLUTELY NO WARRANTY.\n"                \
  "It is licensed to you under your choice of the GNU Lesser\n"       \
  "General Public License, version 3 or any later version (LGPLv3\n"  \
  "or later), or the GNU General Public License, version 2 (GPLv2),\n"\
  "in all cases as published by the Free Software Foundation.";

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
      LOG("cli", GB_LOG_ERROR, "%sblock %s create on volume %s failed\n",
          clnt_sperror(clnt, "block_create_cli_1"),
          create_obj->block_name, create_obj->volume);
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
  }

  if (reply) {
    if (GB_STRDUP(*out, reply->out) < 0) {
      goto out;
    }
    ret = reply->exit;
  }

 out:
  if (clnt) {
    if (!reply || !clnt_freeres(clnt, (xdrproc_t)xdr_blockResponse, (char *)reply))
      LOG("cli", GB_LOG_ERROR, "%s", clnt_sperror(clnt, "clnt_freeres failed"));

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
      "  gluster-block <command> [<args>] <volume=volname>\n"
      "\n"
      "commands and arguments:\n"
      "  create         <name>          create block device\n"
      "    size           <size>             size in KiB|MiB|GiB|TiB..\n"
      "    [mpath          <count>]          multipath requirement for high availability(default: 1)\n"
      "    servers        <IP1,IP2,IP3...>   servers in the pool where targets are exported\n"
      "  list                           list available block devices\n"
      "  info           <name>          details about block device\n"
      "  modify         <resize|auth>   modify metadata\n"
      "  delete         <name>          delete block device\n"
      "    volume         <volname>          volume that hosts the block device\n"
      "  help                           show this message and exit\n"
      "  version                        show version info and exit\n"
      );
}


static int
glusterBlockCreate(int argcount, char **options)
{
  size_t opt;
  size_t optind = 2;
  int ret = 0;
  char *out = NULL;
  static blockCreateCli cobj = {0, };


  if(argcount <= optind) {
    MSG("%s\n", "Insufficient arguments for create:");
    MSG("%s\n", "gluster-block create <block-name> volume <volname> "
                "size <bytes> [mpath <count>] servers <IP1,IP2,...>");
    return -1;
  }

  /* name of block */
  strcpy(cobj.block_name, options[optind++]);

  /* default mpath */
  cobj.mpath = 1;

  while (1) {
    if(argcount <= optind) {
      break;
    }

    opt = glusterBlockCLICreateOptEnumParse(options[optind++]);
    if (opt == GB_CLI_CREATE_OPT_MAX) {
      MSG("unrecognized option '%s'\n", options[optind-1]);
      return -1;
    } else if (opt && !options[optind]) {
      MSG("%s: require argument\n", options[optind-1]);
      return -1;
    }

    switch (opt) {
    case GB_CLI_CREATE_VOLUME:
      strcpy(cobj.volume, options[optind++]);
      ret++;
      break;

    case GB_CLI_CREATE_MULTIPATH:
      sscanf(options[optind++], "%u", &cobj.mpath);
      break;

    case GB_CLI_CREATE_SIZE:
      cobj.size = glusterBlockCreateParseSize("cli", options[optind++]);
      if (cobj.size < 0) {
        LOG("cli", GB_LOG_ERROR, "%s", "failed while parsing size");
        ret = -1;
        goto out;
      }
      ret++;
      break;

    case GB_CLI_CREATE_BACKEND_SERVESRS:
      if (GB_STRDUP(cobj.block_hosts, options[optind++]) < 0) {
        LOG("cli", GB_LOG_ERROR, "%s", "failed while parsing size");
        ret = -1;
        goto out;
      }
      ret++;
      break;

    default:
      MSG("unrecognized option '%s'\n", options[optind-1]);
      MSG("%s", "Hint: gluster-block help\n");
      ret = -1;
      goto out;
    }
  }

  /* check all options required by create command are specified */
  if(ret < 3) {
    MSG("%s\n", "Insufficient arguments for create:");
    MSG("%s\n", "gluster-block create <block-name> volume <volname> "
                "size <bytes> [mpath <count>] servers <IP1,IP2,...>");
    ret = -1;
    goto out;
  }

  ret = glusterBlockCliRPC_1(&cobj, CREATE_CLI, &out);

  if(out) {
    MSG("%s", out);
  }

 out:
  GB_FREE(cobj.block_hosts);
  GB_FREE(out);

  return ret;
}


static int
glusterBlockList(int argcount, char **options)
{
  size_t opt;
  size_t optind = 2;
  static blockListCli cobj;
  char *out = NULL;
  int ret = -1;


  if(argcount <= optind) {
    MSG("%s\n", "Insufficient arguments for list:");
    MSG("%s\n", "gluster-block list volume <volname>");
    return -1;
  }

  opt = glusterBlockCLICommonOptEnumParse(options[optind++]);
  if (opt == GB_CLI_COMMON_OPT_MAX) {
    MSG("unrecognized option '%s'\n", options[optind-1]);
    MSG("%s\n", "List needs 'volume' option");
    return -1;
  } else if (!options[optind]) {
    MSG("%s: require argument\n", options[optind-1]);
    return -1;
  }

  strcpy(cobj.volume, options[optind]);
  ret = glusterBlockCliRPC_1(&cobj, LIST_CLI, &out);

  if(out) {
    MSG("%s", out);
  }

  GB_FREE(out);

  return ret;
}


static int
glusterBlockDelete(int argcount, char **options)
{
  size_t opt;
  size_t optind = 2;
  static blockDeleteCli cobj;
  char *out = NULL;
  int ret = -1;


  if(argcount <= optind) {
    MSG("%s\n", "Insufficient arguments for delete:");
    MSG("%s\n", "gluster-block delete <block-name> volume <volname>");
    return -1;
  }

  /* name of block */
  strcpy(cobj.block_name, options[optind++]);

  opt = glusterBlockCLICommonOptEnumParse(options[optind++]);
  if (opt == GB_CLI_COMMON_OPT_MAX) {
    MSG("unrecognized option '%s'\n", options[optind-1]);
    MSG("%s\n", "Delete needs 'volume' option");
    return -1;
  } else if (!options[optind]) {
    MSG("%s: require argument\n", options[optind-1]);
    return -1;
  }

  strcpy(cobj.volume, options[optind]);
  ret = glusterBlockCliRPC_1(&cobj, DELETE_CLI, &out);

  if(out) {
    MSG("%s", out);
  }

  GB_FREE(out);

  return ret;
}


static int
glusterBlockInfo(int argcount, char **options)
{
  size_t opt;
  size_t optind = 2;
  static blockInfoCli cobj;
  char *out = NULL;
  int ret = -1;


  if(argcount <= optind) {
    MSG("%s\n", "Insufficient arguments for info:");
    MSG("%s\n", "gluster-block info <block-name> volume <volname>");
    return -1;
  }

  /* name of block */
  strcpy(cobj.block_name, options[optind++]);

  opt = glusterBlockCLICommonOptEnumParse(options[optind++]);
  if (opt == GB_CLI_COMMON_OPT_MAX) {
    MSG("unrecognized option '%s'\n", options[optind-1]);
    MSG("%s\n", "Info needs 'volume' option");
    return -1;
  } else if (!options[optind]) {
    MSG("%s: require argument\n", options[optind-1]);
    return -1;
  }

  strcpy(cobj.volume, options[optind]);
  ret = glusterBlockCliRPC_1(&cobj, INFO_CLI, &out);

  if(out) {
    MSG("%s", out);
  }

  GB_FREE(out);

  return ret;
}


static int
glusterBlockParseArgs(int count, char **options)
{
  int ret = 0;
  size_t opt = 0;


  opt = glusterBlockCLIOptEnumParse(options[1]);
  if (!opt || opt >= GB_CLI_OPT_MAX) {
    MSG("unknow option: %s\n", options[1]);
    return -1;
  }

  while (1) {
    switch (opt) {
    case GB_CLI_CREATE:
      ret = glusterBlockCreate(count, options);
      if (ret && ret != EEXIST) {
        LOG("cli", GB_LOG_ERROR, "%s", FAILED_CREATE);
      }
      goto out;

    case GB_CLI_LIST:
      ret = glusterBlockList(count, options);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, "%s", FAILED_LIST);
      }
      goto out;

    case GB_CLI_INFO:
      ret = glusterBlockInfo(count, options);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, "%s", FAILED_INFO);
      }
      goto out;

    case GB_CLI_MODIFY:
      MSG("option '%s' is not supported yet.\n", options[1]);
      goto out;

    case GB_CLI_DELETE:
      ret = glusterBlockDelete(count, options);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, "%s", FAILED_DELETE);
      }
      goto out;

    case GB_CLI_HELP:
      glusterBlockHelp();
      goto out;

    case GB_CLI_VERSION:
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
