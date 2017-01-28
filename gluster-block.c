/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# define   _GNU_SOURCE         /* See feature_test_macros(7) */

# include  <getopt.h>

# include  "common.h"
# include  "rpc/block.h"


# define  LIST             "list"
# define  CREATE           "create"
# define  DELETE           "delete"
# define  INFO             "info"
# define  MODIFY           "modify"
# define  BLOCKHOST        "block-host"
# define  VOLUME           "volume"
# define  HELP             "help"


typedef enum opterations {
  CREATE_CLI = 1,
  LIST_CLI   = 2,
  INFO_CLI   = 3,
  DELETE_CLI = 4
} opterations;


static int
gluster_block_cli_1(void *cobj, opterations opt, char **out)
{
  CLIENT *clnt;
  int sockfd, len;
  int ret = -1;
  struct sockaddr_un saun;
  blockResponse *reply = NULL;

  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    LOG("cli", ERROR, "socket creation failed (%s)", strerror (errno));
    goto out;
  }

  saun.sun_family = AF_UNIX;
  strcpy(saun.sun_path, ADDRESS);

  len = sizeof(saun.sun_family) + strlen(saun.sun_path);

  if (connect(sockfd, (struct sockaddr *) &saun, len) < 0) {
    LOG("cli", ERROR, "connect failed (%s)", strerror (errno));
    goto out;
  }

  clnt = clntunix_create ((struct sockaddr_un *) &saun, GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS, &sockfd, 0, 0);
  if (clnt == NULL) {
    LOG("cli", ERROR, "%s, unix addr %s",
        clnt_spcreateerror("client create failed"), ADDRESS);
  }
  switch(opt) {
  case CREATE_CLI:
    reply = block_create_cli_1((blockCreateCli *)cobj, clnt);
    if (reply == NULL) {
      LOG("cli", ERROR, "%s", clnt_sperror(clnt, "block create failed"));
      goto out;
    }
    break;
  case DELETE_CLI:
    reply = block_delete_cli_1((blockDeleteCli *)cobj, clnt);
    if (reply == NULL) {
      LOG("cli", ERROR, "%s", clnt_sperror(clnt, "block delete failed"));
      goto out;
    }
    break;
  case INFO_CLI:
    reply = block_info_cli_1((blockInfoCli *)cobj, clnt);
    if (reply == NULL) {
      LOG("cli", ERROR, "%s", clnt_sperror(clnt, "block info failed"));
      goto out;
    }
    break;
  case LIST_CLI:
    reply = block_list_cli_1((blockListCli *)cobj, clnt);
    if (reply == NULL) {
      LOG("cli", ERROR, "%s", clnt_sperror(clnt, "block list failed"));
      goto out;
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
    LOG("cli", ERROR, "%s", clnt_sperror (clnt, "clnt_freeres failed"));

  clnt_destroy (clnt);

  return ret;
}


static void
glusterBlockHelp(void)
{
  MSG("%s",
      "gluster-block (Version 0.1) \n"
      " -c, --create      <name>          Create the gluster block\n"
      "     -h, --host         <gluster-node>   node addr from gluster pool\n"
      "     -s, --size         <size>           block storage size in KiB|MiB|GiB|TiB..\n"
      "     -m, --multipath    <count>          multi path requirement for high availablity\n"
      "\n"
      " -l, --list                        List available gluster blocks\n"
      "\n"
      " -i, --info        <name>          Details about gluster block\n"
      "\n"
      " -m, --modify      <resize|auth>   Modify the metadata\n"
      "\n"
      " -d, --delete      <name>          Delete the gluster block\n"
      "\n"
      "     -v, --volume       <vol>            gluster volume name\n"
      "    [-b, --block-host   <IP1,IP2,IP3...>]  block servers, clubbed with any option\n");
}


static int
glusterBlockCreate(int count, char **options, char *name)
{
  int c;
  int ret = 0;
  char *out = NULL;
  static blockCreateCli cobj;

  if (!name) {
    LOG("cli", ERROR, "%s", "Insufficient arguments supplied for"
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
      {"multipath",  required_argument, 0, 'm'},
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
    case 'm':
      sscanf(optarg, "%u", &cobj.mpath);
      ret++;
      break;

    case 'b':
      if (GB_STRDUP(cobj.block_hosts, optarg) < 0)
        return -1;
      ret++;
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
        LOG("cli", ERROR, "%s", "failed while parsing size");
        ret = -1;
        goto out;
      }
      ret++;
      break;

    case '?':
      MSG("unrecognized option '%s'\n", options[optind-1]);
      MSG("%s", "Hint: gluster-block --help\n");
      goto out;

    default:
      break;
    }
  }

  /* Print any remaining command line arguments (not options). */
  if (optind < count) {
    LOG("cli", ERROR, "%s", "non-option ARGV-elements: ");
    while (optind < count)
      printf("%s ", options[optind++]);
    putchar('\n');

    ret = -1;
    goto out;
  }

  if (ret != 5) {
    LOG("cli", ERROR, "%s", "Insufficient arguments supplied for"
                "'gluster-block create'\n");
    ret = -1;
    goto out;
  }

  ret = gluster_block_cli_1(&cobj, CREATE_CLI, &out);

  MSG("%s", out);

  out:
  GB_FREE(cobj.block_hosts);
  GB_FREE(out);

  return ret;
}


static int
glusterBlockList(char *volume)
{
  static blockListCli cobj;
  char *out = NULL;
  int ret = -1;

  strcpy(cobj.volume, volume);

  ret = gluster_block_cli_1(&cobj, LIST_CLI, &out);

  MSG("%s", out);
  GB_FREE(out);

  return ret;
}


static int
glusterBlockDelete(char* name, char* volume)
{
  static blockDeleteCli cobj;
  char *out = NULL;
  int ret = -1;

  strcpy(cobj.block_name, name);
  strcpy(cobj.volume, volume);

  ret = gluster_block_cli_1(&cobj, DELETE_CLI, &out);

  MSG("%s", out);
  GB_FREE(out);

  return ret;
}


static int
glusterBlockInfo(char* name, char* volume)
{
  static blockInfoCli cobj;
  char *out = NULL;
  int ret = -1;

  strcpy(cobj.block_name, name);
  strcpy(cobj.volume, volume);

  ret = gluster_block_cli_1(&cobj, INFO_CLI, &out);

  MSG("%s", out);
  GB_FREE(out);

  return ret;
}


static int
glusterBlockParseArgs(int count, char **options)
{
  int c;
  int ret = 0;
  int optFlag = 0;
  char *block = NULL;
  char *volume = NULL;

  while (1) {
    static const struct option long_options[] = {
      {HELP,      no_argument,       0, 'h'},
      {CREATE,    required_argument, 0, 'c'},
      {DELETE,    required_argument, 0, 'd'},
      {LIST,      no_argument,       0, 'l'},
      {INFO,      required_argument, 0, 'i'},
      {MODIFY,    required_argument, 0, 'm'},
      {VOLUME,    required_argument, 0, 'v'},
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
    case 'v':
      volume = optarg;
      break;

    case 'c':
      ret = glusterBlockCreate(count, options, optarg);
      if (ret && ret != EEXIST) {
        LOG("cli", ERROR, "%s", FAILED_CREATE);
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
      break;

    case 'm':
      MSG("option --modify yet TODO '%s'\n", optarg);
      break;

    case 'h':
      glusterBlockHelp();
      break;

    case '?':
      /* getopt_long already printed an error message. */
      break;
    }
  }

  switch (optFlag) {
  case 'l':
    ret = glusterBlockList(volume);
    if (ret)
        LOG("cli", ERROR, "%s", FAILED_LIST);
    break;
  case 'i':
    ret = glusterBlockInfo(block, volume);
    if (ret)
        LOG("cli", ERROR, "%s", FAILED_INFO);
    break;
  case 'd':
    ret = glusterBlockDelete(block, volume);
    if (ret)
        LOG("cli", ERROR, "%s", FAILED_DELETE);
    break;
  }

 out:
  if (ret == 0 && optind < count) {
    LOG("cli", ERROR, "%s", "Unable to parse elements: ");
    while (optind < count)
      printf("%s ", options[optind++]);
    putchar('\n');
    MSG("Hint: %s --help\n", options[0]);
  }

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
