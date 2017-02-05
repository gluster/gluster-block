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



typedef enum clioperations {
  CREATE_CLI = 1,
  LIST_CLI   = 2,
  INFO_CLI   = 3,
  DELETE_CLI = 4
} clioperations;


static int
glusterBlockCliRPC_1(void *cobj, operations opt, char **out)
{
  CLIENT *clnt = NULL;
  int ret = -1;
  int sockfd;
  struct sockaddr_un saun;
  blockResponse *reply;


  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    LOG("cli", GB_LOG_ERROR, "socket creation failed (%s)", strerror (errno));
    goto out;
  }

  saun.sun_family = AF_UNIX;
  strcpy(saun.sun_path, ADDRESS);

  if (connect(sockfd, (struct sockaddr *) &saun,
              sizeof(struct sockaddr_un)) < 0) {
    LOG("cli", GB_LOG_ERROR, "connect failed (%s)", strerror (errno));
    goto out;
  }

  clnt = clntunix_create ((struct sockaddr_un *) &saun,
                          GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS,
                          &sockfd, 0, 0);
  if (!clnt) {
    LOG("cli", GB_LOG_ERROR, "%s, unix addr %s",
        clnt_spcreateerror("client create failed"), ADDRESS);
    goto out;
  }

  switch(opt) {
  case CREATE_CLI:
    reply = block_create_cli_1((blockCreateCli *)cobj, clnt);
    if (!reply) {
      LOG("cli", GB_LOG_ERROR, "%s", clnt_sperror(clnt, "block create failed"));
      goto out;
    }
    break;
  case DELETE_CLI:
    reply = block_delete_cli_1((blockDeleteCli *)cobj, clnt);
    if (!reply) {
      LOG("cli", GB_LOG_ERROR, "%s", clnt_sperror(clnt, "block delete failed"));
      goto out;
    }
    break;
  case INFO_CLI:
    reply = block_info_cli_1((blockInfoCli *)cobj, clnt);
    if (!reply) {
      LOG("cli", GB_LOG_ERROR, "%s", clnt_sperror(clnt, "block info failed"));
      goto out;
    }
    break;
  case LIST_CLI:
    reply = block_list_cli_1((blockListCli *)cobj, clnt);
    if (!reply) {
      LOG("cli", GB_LOG_ERROR, "%s", clnt_sperror(clnt, "block list failed"));
      goto out;
    }
    break;
  }

  if (GB_STRDUP(*out, reply->out) < 0)
    goto out;
  ret = reply->exit;

 out:
  if (clnt) {
    if (!reply || !clnt_freeres(clnt, (xdrproc_t)xdr_blockResponse, (char *)reply))
      LOG("cli", GB_LOG_ERROR, "%s", clnt_sperror(clnt, "clnt_freeres failed"));

    clnt_destroy (clnt);
  }

  return ret;
}


static void
glusterBlockHelp(void)
{
  MSG("%s",
      "gluster-block (Version 0.1) \n"
      " create         <name>          Create the gluster block\n"
      "    volserver      [gluster-node]     node addr from gluster pool(default: localhost)\n"
      "    size           <size>             block storage size in KiB|MiB|GiB|TiB..\n"
      "    mpath          <count>            multi path requirement for high availablity\n"
      "    servers        <IP1,IP2,IP3...>   block servers, clubbed with any option\n"
      "\n"
      " list                           List available gluster blocks\n"
      "\n"
      " info           <name>          Details about gluster block\n"
      "\n"
      " modify         <resize|auth>   Modify the metadata\n"
      "\n"
      " delete         <name>          Delete the gluster block\n"
      "\n"
      "    volume         <vol>              gluster volume name\n"
      );
}


static int
glusterBlockCreate(int argcount, char **options)
{
  size_t opt;
  size_t optind = 2;
  int ret = 0;
  char *out = NULL;
  bool volserver = FALSE;
  static blockCreateCli cobj = {0, };


  if(argcount <= optind) {
    MSG("%s\n", "Insufficient options for create");
    return -1;
  }

  /* name of block */
  strcpy(cobj.block_name, options[optind++]);

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

    case GB_CLI_CREATE_VOLSERVER:
      strcpy(cobj.volfileserver, options[optind++]);
      volserver = TRUE;
      break;

    case GB_CLI_CREATE_MULTIPATH:
      sscanf(options[optind++], "%u", &cobj.mpath);
      ret++;
      break;

    case GB_CLI_CREATE_SIZE:
      cobj.size = glusterBlockCreateParseSize(options[optind++]);
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
      goto out;
    }
  }

  /* check all options required by create command are specified */
  if(ret < 4) {
    MSG("%s\n", "Insufficient options for create");
    ret = -1;
    goto out;
  }

  if(!volserver) {
    strcpy(cobj.volfileserver, "localhost");
  }

  ret = glusterBlockCliRPC_1(&cobj, CREATE_CLI, &out);

  if(out)
    MSG("%s", out);

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
    MSG("%s\n", "Insufficient options for list");
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

  if ((opt == GB_CLI_COMMON_VOLUME)) {
    strcpy(cobj.volume, options[optind]);

    ret = glusterBlockCliRPC_1(&cobj, LIST_CLI, &out);

    if(out)
      MSG("%s", out);

    GB_FREE(out);
  }

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
    MSG("%s\n", "Insufficient options for delete");
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

  if ((opt == GB_CLI_COMMON_VOLUME)) {
    strcpy(cobj.volume, options[optind]);
    ret = glusterBlockCliRPC_1(&cobj, DELETE_CLI, &out);

    if(out)
      MSG("%s", out);

    GB_FREE(out);
  }

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
    MSG("%s\n", "Insufficient options for info");
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

  if ((opt == GB_CLI_COMMON_VOLUME)) {
    strcpy(cobj.volume, options[optind]);
    ret = glusterBlockCliRPC_1(&cobj, INFO_CLI, &out);

    if(out)
      MSG("%s", out);

    GB_FREE(out);
  }

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

    case GB_CLI_DELETE:
      ret = glusterBlockDelete(count, options);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, "%s", FAILED_DELETE);
      }
      goto out;

    case GB_CLI_HELP:
      glusterBlockHelp();
      goto out;
    }
  }

 out:
  return ret;
}


int
main(int argc, char *argv[])
{
  if (argc <= 1)
    glusterBlockHelp();

  return glusterBlockParseArgs(argc, argv);
}
