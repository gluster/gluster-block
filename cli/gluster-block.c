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
      "  gluster-block <command> <volname[/blockname]> [<args>]\n"
      "\n"
      "commands:\n"
      "  create  <volname/blockname> [ha <count>] <host1[,host2,...]> <size>\n"
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
      "  help\n"
      "        show this message and exit.\n"
      "\n"
      "  version\n"
      "        show version info and exit.\n"
      );
}


static int
glusterBlockCreate(int argcount, char **options)
{
  size_t optind = 2;
  int ret = -1;
  ssize_t sparse_ret;
  char *out = NULL;
  blockCreateCli cobj = {0, };
  char *argcopy;
  char *sep;


  if (argcount <= optind) {
    MSG("%s\n", "Insufficient arguments for create:");
    MSG("%s\n", "gluster-block create <volname/blockname> [ha <count>]"
                " <HOST1[,HOST2,...]> <size>");
    return -1;
  }

  /* default mpath */
  cobj.mpath = 1;

  if (GB_STRDUP (argcopy, options[optind++]) < 0) {
    goto out;
  }
  /* part before '/' is the volume name */
  sep = strchr(argcopy, '/');
  if (!sep) {
    MSG("%s\n",
        "first argument '<volname/blockname>' doesn't seems to be right");
    MSG("%s\n", "gluster-block create <volname/blockname> [ha <count>] "
        " <HOST1[,HOST2,...]> <size>");
    LOG("cli", GB_LOG_ERROR, "%s",
        "create failed while parsing <volname/blockname>");
    goto out;
  }
  *sep = '\0';
  strcpy(cobj.volume, argcopy);

  /* part after / is blockname */
  strcpy(cobj.block_name, sep + 1);

  if (argcount - optind >= 2) {  /* atleast 2 needed */
    /* if ha given then collect count which is next by 'ha' arg */
    if (!strcmp(options[optind], "ha")) {
      optind++;
      sscanf(options[optind++], "%u", &cobj.mpath);
    }
  }

  if (argcount - optind < 2) {  /* left with servers and size so 2 */
    MSG("%s\n", "Insufficient arguments for create");
    MSG("%s\n", "gluster-block create <volname/blockname> [ha <count>]"
                " <HOST1[,HOST2,...]> <size>");
    LOG("cli", GB_LOG_ERROR,
        "failed creating block %s on volume %s with hosts %s",
        cobj.block_name, cobj.volume, cobj.block_hosts);
    goto out;
  }

  /* next arg to 'ha count' will be servers */
  if (GB_STRDUP(cobj.block_hosts, options[optind++]) < 0) {
    LOG("cli", GB_LOG_ERROR, "failed while parsing servers for block %s",
        cobj.block_name);
    goto out;
  }

  /* last arg will be size */
  sparse_ret = glusterBlockCreateParseSize("cli", options[optind]);
  if (sparse_ret < 0) {
    MSG("%s\n", "last argument '<size>' doesn't seems to be right");
    MSG("%s\n", "gluster-block create <volname/blockname> [ha <count>] "
                " <HOST1[,HOST2,...]> <size>");
    LOG("cli", GB_LOG_ERROR, "failed while parsing size for block %s",
        cobj.block_name);
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
  GB_FREE(argcopy);
  GB_FREE(cobj.block_hosts);
  GB_FREE(out);

  return ret;
}


static int
glusterBlockList(int argcount, char **options)
{
  blockListCli cobj;
  char *out = NULL;
  int ret = -1;


  if (argcount != 3) {
    MSG("%s\n", "Insufficient arguments for list:");
    MSG("%s\n", "gluster-block list <volname>");
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
glusterBlockDelete(int argcount, char **options)
{
  blockDeleteCli cobj;
  char *out = NULL;
  char *argcopy;
  char *sep;
  int ret = -1;


  if (argcount != 3) {
    MSG("%s\n", "Insufficient arguments for delete:");
    MSG("%s\n", "gluster-block delete <volname/blockname>");
    return -1;
  }


  if (GB_STRDUP (argcopy, options[2]) < 0) {
    goto out;
  }
  /* part before '/' is the volume name */
  sep = strchr(argcopy, '/');
  if (!sep) {
    MSG("%s\n", "argument '<volname/blockname>' doesn't seems to be right");
    MSG("%s\n", "gluster-block delete <volname/blockname>");
    LOG("cli", GB_LOG_ERROR, "%s",
        "delete failed while parsing <volname/blockname>");
    goto out;
  }
  *sep = '\0';
  strcpy(cobj.volume, argcopy);

  /* part after / is blockname */
  strcpy(cobj.block_name, sep + 1);

  ret = glusterBlockCliRPC_1(&cobj, DELETE_CLI, &out);
  if (ret) {
    LOG("cli", GB_LOG_ERROR, "failed deleting block %s on volume %s",
        cobj.block_name, cobj.volume);
  }

  if (out) {
    MSG("%s", out);
  }

 out:
  GB_FREE(argcopy);
  GB_FREE(out);

  return ret;
}


static int
glusterBlockInfo(int argcount, char **options)
{
  blockInfoCli cobj;
  char *out = NULL;
  char *argcopy;
  char *sep;
  int ret = -1;


  if (argcount != 3) {
    MSG("%s\n", "Insufficient arguments for info:");
    MSG("%s\n", "gluster-block info <volname/blockname>");
    return -1;
  }


  if (GB_STRDUP (argcopy, options[2]) < 0) {
    goto out;
  }
  /* part before '/' is the volume name */
  sep = strchr(argcopy, '/');
  if (!sep) {
    MSG("%s\n", "argument '<volname/blockname>' doesn't seems to be right");
    MSG("%s\n", "gluster-block info <volname/blockname>");
    LOG("cli", GB_LOG_ERROR, "%s",
        "info failed while parsing <volname/blockname>");
    goto out;
  }
  *sep = '\0';
  strcpy(cobj.volume, argcopy);

  /* part after / is blockname */
  strcpy(cobj.block_name, sep + 1);

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
  GB_FREE(argcopy);
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
