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

# include <arpa/inet.h>

# define  GB_CREATE_HELP_STR  "gluster-block create <volname/blockname> "      \
                                "[ha <count>] [auth <enable|disable>] "        \
                                "[prealloc <full|no>] [storage <filename>] "   \
                                "[ring-buffer <size-in-MB-units>] "            \
                                "[block-size <size-in-Byte-units>] "           \
                                "[io-timeout <N-in-Second>] "                  \
                                "<HOST1[,HOST2,...]> [size] [--json*]"
# define  GB_DELETE_HELP_STR  "gluster-block delete <volname/blockname> "      \
                                "[unlink-storage <yes|no>] [force] [--json*]"
# define  GB_RELOAD_HELP_STR  "gluster-block reload <volname/blockname> " \
                              "[force] [--json*]"
# define  GB_MODIFY_HELP_STR  "gluster-block modify <volname/blockname> "      \
                                "[auth <enable|disable>] [size <size> "       \
                                "[force]] [--json*]"
# define  GB_REPLACE_HELP_STR "gluster-block replace <volname/blockname> "     \
                                "<old-node> <new-node> [force] [--json*]"
# define  GB_GENCONF_HELP_STR "gluster-block genconfig <volname[,volume2,volume3,...]> "\
                              "enable-tpg <host> [--json*]"
# define  GB_INFO_HELP_STR    "gluster-block info <volname/blockname> [--json*]"
# define  GB_LIST_HELP_STR    "gluster-block list <volname> [--json*]"


# define  GB_ARGCHECK_OR_RETURN(argcount, count, cmd, helpstr)        \
          do {                                                        \
            if (argcount != count) {                                  \
              MSG(stderr, "Inadequate arguments for %s:\n%s", cmd, helpstr); \
              return -1;                                              \
            }                                                         \
          } while(0)

extern const char *argp_program_version;

struct timeval TIMEOUT;           /* cli process to daemon cli thread timeout */
static size_t cliOptTimeout;

gbProcessCtx gbCtx = GB_CLI_MODE; /* set process mode */

typedef enum clioperations {
  CREATE_CLI = 1,
  LIST_CLI   = 2,
  INFO_CLI   = 3,
  DELETE_CLI = 4,
  MODIFY_CLI = 5,
  MODIFY_SIZE_CLI = 6,
  REPLACE_CLI = 7,
  GENCONF_CLI = 8,
  RELOAD_CLI = 9
} clioperations;


gbConfig *
glusterBlockCLILoadConfig(void)
{
  gbConfig *cfg = NULL;

  if (GB_ALLOC(cfg) < 0) {
    LOG("cli", GB_LOG_ERROR,
        "Alloc GB config failed for configPath: %s!", GB_DEF_CONFIGPATH);
    return NULL;
  }

  if (GB_STRDUP(cfg->configPath, GB_DEF_CONFIGPATH) < 0) {
    LOG("cli", GB_LOG_ERROR,
        "failed to copy configPath: %s", GB_DEF_CONFIGPATH);
    goto freeConfig;
  }

  if (glusterBlockLoadConfig(cfg, false)) {
    LOG("cli", GB_LOG_ERROR,
        "Loading GB config failed for configPath: %s!", GB_DEF_CONFIGPATH);
    goto freeConfigPath;
  }

  return cfg;

 freeConfigPath:
  GB_FREE(cfg->configPath);
 freeConfig:
  GB_FREE(cfg);

  return NULL;
}


static int
glusterBlockCliRPC_1(void *cobj, clioperations opt)
{
  CLIENT *clnt = NULL;
  int ret = -1;
  int sockfd = RPC_ANYSOCK;
  struct sockaddr_un saun = {0,};
  blockCreateCli *create_obj;
  blockDeleteCli *delete_obj;
  blockReloadCli *reload_obj;
  blockInfoCli *info_obj;
  blockListCli *list_obj;
  blockModifyCli *modify_obj;
  blockModifySizeCli *modify_size_obj;
  blockReplaceCli *replace_obj;
  blockGenConfigCli *genconfig_obj;
  blockResponse reply = {0,};
  char          errMsg[2048] = {0};
  gbConfig *conf = NULL;
  char *cli_timeout;


  if (strlen(GB_UNIX_ADDRESS) > SUN_PATH_MAX) {
    snprintf (errMsg, sizeof (errMsg), "%s: path length is more than "
              "SUN_PATH_MAX: (%zu > %zu chars)", GB_UNIX_ADDRESS,
              strlen(GB_UNIX_ADDRESS), SUN_PATH_MAX);
    goto out;
  }

  saun.sun_family = AF_UNIX;
  GB_STRCPYSTATIC(saun.sun_path, GB_UNIX_ADDRESS);

#ifndef HAVE_LIBTIRPC
  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    snprintf (errMsg, sizeof (errMsg), "%s: socket creation failed (%s)",
              GB_UNIX_ADDRESS, strerror (errno));
    goto out;
  }

  if (connect(sockfd, (struct sockaddr *) &saun,
              sizeof(struct sockaddr_un)) < 0) {
    if (errno == ENOENT || errno == ECONNREFUSED) {
      snprintf (errMsg, sizeof (errMsg), "Connection failed. Please check if "
                "gluster-block daemon is operational.");
    } else {
      snprintf (errMsg, sizeof (errMsg), "%s: connect failed (%s)",
                GB_UNIX_ADDRESS, strerror (errno));
    }
    goto out;
  }
#endif  /* HAVE_LIBTIRPC */

  clnt = clntunix_create((struct sockaddr_un *) &saun,
                         GLUSTER_BLOCK_CLI, GLUSTER_BLOCK_CLI_VERS,
                         &sockfd, 0, 0);
  if (!clnt) {
    snprintf (errMsg, sizeof (errMsg), "%s, unix addr %s",
              clnt_spcreateerror("client create failed"), GB_UNIX_ADDRESS);
    goto out;
  }

  conf = glusterBlockCLILoadConfig();
  if (!conf) {
      LOG("cli", GB_LOG_ERROR, "glusterBlockCLILoadConfig() failed");
      goto out;
  }

  /* follow precedence */
  if (conf->GB_CLI_TIMEOUT) {
    TIMEOUT.tv_sec = conf->GB_CLI_TIMEOUT;
  } else if (cliOptTimeout) {
    TIMEOUT.tv_sec = cliOptTimeout;
  } else if ((cli_timeout = getenv("GB_CLI_TIMEOUT"))) {
    sscanf(cli_timeout, "%lu", &TIMEOUT.tv_sec);
  } else {
    TIMEOUT.tv_sec = CLI_TIMEOUT_DEF;
  }
  LOG("cli", GB_LOG_DEBUG, "cli timeout now is %lu", TIMEOUT.tv_sec);

  switch(opt) {
  case CREATE_CLI:
    create_obj = cobj;
    if (block_create_cli_1(create_obj, &reply, clnt) != RPC_SUCCESS) {
      LOG("cli", GB_LOG_ERROR,
          "%s block %s create on volume %s with hosts %s failed",
          clnt_sperror(clnt, "block_create_cli_1"), create_obj->block_name,
          create_obj->volume, create_obj->block_hosts);
      goto out;
    }
    break;
  case DELETE_CLI:
    delete_obj = cobj;
    if (block_delete_cli_1(delete_obj, &reply, clnt) != RPC_SUCCESS) {
      LOG("cli", GB_LOG_ERROR, "%s block %s delete on volume %s failed",
          clnt_sperror(clnt, "block_delete_cli_1"),
          delete_obj->block_name, delete_obj->volume);
      goto out;
    }
    break;
  case RELOAD_CLI:
    reload_obj = cobj;
    if (block_reload_cli_1(reload_obj, &reply, clnt) != RPC_SUCCESS) {
      LOG("cli", GB_LOG_ERROR, "%s block %s reload on volume %s failed",
          clnt_sperror(clnt, "block_reload_cli_1"),
          reload_obj->block_name, reload_obj->volume);
      goto out;
    }
    break;
  case INFO_CLI:
    info_obj = cobj;
    if (block_info_cli_1(info_obj, &reply, clnt) != RPC_SUCCESS) {
      LOG("cli", GB_LOG_ERROR, "%s block %s info on volume %s failed",
          clnt_sperror(clnt, "block_info_cli_1"),
          info_obj->block_name, info_obj->volume);
      goto out;
    }
    break;
  case LIST_CLI:
    list_obj = cobj;
    if (block_list_cli_1(list_obj, &reply, clnt) != RPC_SUCCESS) {
      LOG("cli", GB_LOG_ERROR, "%s block list on volume %s failed",
          clnt_sperror(clnt, "block_list_cli_1"), list_obj->volume);
      goto out;
    }
    break;
  case MODIFY_CLI:
    modify_obj = cobj;
    if (block_modify_cli_1(modify_obj, &reply, clnt) != RPC_SUCCESS) {
      LOG("cli", GB_LOG_ERROR, "%s block modify auth on volume %s failed",
          clnt_sperror(clnt, "block_modify_cli_1"), modify_obj->volume);
      goto out;
    }
    break;
  case MODIFY_SIZE_CLI:
    modify_size_obj = cobj;
    if (block_modify_size_cli_1(modify_size_obj, &reply, clnt) != RPC_SUCCESS) {
      LOG("cli", GB_LOG_ERROR, "%s block modify size on volume %s failed",
          clnt_sperror(clnt, "block_modify_size_cli_1"), modify_size_obj->volume);
      goto out;
    }
    break;
  case REPLACE_CLI:
    replace_obj = cobj;
    if (block_replace_cli_1(replace_obj, &reply, clnt) != RPC_SUCCESS) {
      LOG("cli", GB_LOG_ERROR, "%s block %s replace on volume %s failed",
          clnt_sperror(clnt, "block_replace_cli_1"), replace_obj->block_name,
          replace_obj->volume);
      goto out;
    }
    break;
  case GENCONF_CLI:
    genconfig_obj = cobj;
    if (block_gen_config_cli_1(genconfig_obj, &reply, clnt) != RPC_SUCCESS) {
      LOG("cli", GB_LOG_ERROR, "%s genconfig on volume %s failed",
          clnt_sperror(clnt, "block_gen_config_cli_1"), genconfig_obj->volume);
      goto out;
    }
    break;
  }

 out:
  if (reply.out) {
    ret = reply.exit;
    if (!ret) {
      MSG(stdout, "%s", reply.out);
    } else {
      MSG(stderr, "%s", reply.out);
    }
  } else if (errMsg[0]) {
    LOG("cli", GB_LOG_ERROR, "%s", errMsg);
    MSG(stderr, "%s", errMsg);
  } else {
    MSG(stderr, "Did not receive any response from gluster-block daemon."
        " Please check log files to find the reason");
    ret = -1;
  }

  if (clnt) {
    if (reply.out && !clnt_freeres(clnt, (xdrproc_t)xdr_blockResponse,
                                   (char *)&reply)) {
      LOG("cli", GB_LOG_ERROR, "%s",
          clnt_sperror(clnt, "clnt_freeres failed"));
    }
    clnt_destroy (clnt);
  }

  if (sockfd != RPC_ANYSOCK) {
    close (sockfd);
  }

  if (conf) {
    GB_FREE(conf->configPath);
    GB_FREE(conf);
  }

  return ret;
}


static void
glusterBlockHelp(void)
{
  MSG(stdout,
      PACKAGE_NAME" ("PACKAGE_VERSION")\n"
      "usage:\n"
      "  gluster-block [timeout <seconds>] <command> <volname[/blockname]> [<args>] [--json*]\n"
      "\n"
      "commands:\n"
      "  create  <volname/blockname> [ha <count>]\n"
      "                              [auth <enable|disable>]\n"
      "                              [prealloc <full|no>]\n"
      "                              [storage <filename>]\n"
      "                              [ring-buffer <size-in-MB-units>]\n"
      "                              [block-size <size-in-Byte-units>]\n"
      "                              [io-timeout <N-in-Second>]\n"
      "                              <host1[,host2,...]> [size]\n"
      "        create block device [defaults: ha 1, auth disable, prealloc full, size in bytes,\n"
      "                             ring-buffer and block-size default size dependends on kernel,\n"
      "                             io-timeout 43s]\n"
      "\n"
      "  list    <volname>\n"
      "        list available block devices.\n"
      "\n"
      "  info    <volname/blockname>\n"
      "        details about block device.\n"
      "\n"
      "  delete  <volname/blockname> [unlink-storage <yes|no>] [force]\n"
      "        delete block device.\n"
      "\n"
      "  modify  <volname/blockname> [auth <enable|disable>] [size <size> [force]]\n"
      "        modify block device.\n"
      "\n"
      "  replace <volname/blockname> <old-node> <new-node> [force]\n"
      "        replace operations.\n"
      "\n"
      "  reload <volname/blockname> [force]\n"
      "        reload a block device.\n"
      "\n"
      "  genconfig <volname[,volume2,volume3,...]> enable-tpg <host>\n"
      "        generate the block volumes target configuration.\n"
      "\n"
      "  help\n"
      "        show this message and exit.\n"
      "\n"
      "  version\n"
      "        show version info and exit.\n"
      "\n"
      "common cli options: (fixed formats)\n"
      "  timeout <seconds>\n"
      "        it is the time in seconds that cli can wait for daemon to respond.\n"
      "        [default: timeout 300]\n"
      "  --json*\n"
      "        used to request the output result in json format [default: plain text]\n"
      "        supported JSON formats: --json|--json-plain|--json-spaced|--json-pretty\n"
      );
}

static bool
glusterBlockIsNameAcceptable(char *name)
{
  int i = 0;


  if (!name || strlen(name) >= 255) {
    return FALSE;
  }
  for (i = 0; i < strlen(name); i++) {
    if (!isalnum(name[i]) && (name[i] != '_') && (name[i] != '-'))
      return FALSE;
  }
  return TRUE;
}

static bool
glusterBlockIsVolListAcceptable(char *name)
{
  char *tok, *tmp;
  char delim[2] = {'\0', };


  if (!name || GB_STRDUP(tmp, name) < 0) {
    return FALSE;
  }

  delim[0] = GB_DELIMITER;
  tok = strtok(tmp, delim);
  while (tok != NULL) {
    if (!glusterBlockIsNameAcceptable(tok)) {
      GB_FREE(tmp);
      return FALSE;
    }
    tok = strtok(NULL, delim);
  }

  GB_FREE(tmp);
  return TRUE;
}

static bool
glusterBlockIsAddrAcceptable(char *addr)
{
  int i = 0;
  struct in_addr buf;
  struct in6_addr buf6;


  if (!addr || strlen(addr) == 0 || strlen(addr) > 255) {
    return FALSE;
  }

  for (i = 0; i < strlen(addr); i++) {
    if (!isdigit(addr[i]) && (addr[i] != '.'))
      return FALSE;
  }

  if ((inet_pton(AF_INET, addr, &buf) != 1) &&
      (inet_pton(AF_INET6, addr, &buf6) != 1)) {
    return FALSE;
  }

  return TRUE;
}

/* Checks if the block servers passed at commandline are ip's and match HA count
 * ret > 0 -> ip's and HA requested is mismatching
 * ret = 0 -> address list is acceptable
 * ret < 0 -> hostname is passed or unknown delimers
 */
static int
glusterBlockIsAddrListAcceptable(size_t count, char *addr_list)
{
  char *tok, *tmp;
  char delim[2] = {'\0', };
  unsigned int ncommas = 0;


  if (!addr_list || strlen(addr_list) == 0 || GB_STRDUP(tmp, addr_list) < 0) {
    return -1;
  }

  delim[0] = GB_DELIMITER;
  tok = strtok(tmp, delim);
  while (tok != NULL) {
    if (!glusterBlockIsAddrAcceptable(tok)) {
      GB_FREE(tmp);
      return -1;
    }
    tok = strtok(NULL, delim);
    ncommas++;
  }
  GB_FREE(tmp);

  if (ncommas != count) {
    return ncommas;  /* number of ip's */
  }

  return 0;
}

static int
glusterBlockParseVolumeBlock(char *volumeblock, char *volume, char *block,
                             size_t vol_len, size_t block_len,
                             char *helpstr, char *op)
{
  int ret = -1;
  char *sep = NULL;
  char *tmp = NULL;


  if (GB_STRDUP(tmp, volumeblock) < 0) {
    goto out;
  }
  /* part before '/' is the volume name */
  sep = strchr(tmp, '/');
  if (!sep) {
    MSG(stderr, "argument '<volname/blockname>'(%s) is incorrect",
        volumeblock);
    MSG(stderr, "%s", helpstr);
    LOG("cli", GB_LOG_ERROR, "%s failed while parsing <volname/blockname>", op);
    goto out;
  }
  *sep = '\0';

  if (!glusterBlockIsNameAcceptable(tmp)) {
    MSG(stderr, "volume name(%s) should contain only aplhanumeric,'-', '_' characters "
        "and should be less than 255 characters long", volume);
    goto out;
  }
  /* part after / is blockname */
  if (!glusterBlockIsNameAcceptable(sep+1)) {
    MSG(stderr, "block name(%s) should contain only aplhanumeric,'-', '_' characters "
        "and should be less than 255 characters long", block);
    goto out;
  }
  GB_STRCPY(volume, tmp, vol_len);
  GB_STRCPY(block, sep+1, block_len);
  ret = 0;

 out:
  GB_FREE(tmp);
  return ret;
}

void
getCommandString(char **cmd, int argcount, char **options)
{
  int total_length = 0;
  int i;

  for(i = 1; i < argcount; i++){
    total_length = total_length + strlen(options[i])+1;
  }

  if (GB_ALLOC_N(*cmd, (total_length + 1))) {
    LOG("cmdlog", GB_LOG_ERROR, "Could not allocate memory for command string");
    return;
  }
  for (i = 1; i < argcount; i++) {
    strcat(*cmd, options[i]);
    strcat(*cmd, " ");
  }
}

static int
glusterBlockModify(int argcount, char **options, int json)
{
  size_t optind = 1;
  blockModifyCli mobj = {{0}, };
  blockModifySizeCli msobj = {{0}, };
  char volume[255] = {0};
  char block[255] = {0};
  ssize_t sparse_ret;
  int ret = -1;


  if (argcount < 4 || argcount > 5) {
    MSG(stderr, "Inadequate arguments for modify:\n%s", GB_MODIFY_HELP_STR);
    return -1;
  }

  if (glusterBlockParseVolumeBlock(options[optind++], volume, block,
                                   sizeof(volume), sizeof(block),
                                   GB_MODIFY_HELP_STR, "modify")) {
    goto out;
  }

  /* if auth given then collect status which is next by 'auth' arg */
  if (!strcmp(options[optind], "auth")) {
    optind++;
    ret = convertStringToTrillianParse(options[optind++]);
    if(ret >= 0) {
      mobj.auth_mode = ret;
    } else {
      MSG(stderr, "'auth' option is incorrect");
      MSG(stderr, GB_MODIFY_HELP_STR);
      LOG("cli", GB_LOG_ERROR, "Modify failed while parsing argument "
                               "to auth  for <%s/%s>", volume, block);
      goto out;
    }

    if ((argcount - optind)) {
      MSG(stderr, "unknown/unsupported option '%s' for modify auth:\n%s",
          options[optind], GB_MODIFY_HELP_STR);
      ret = -1;
      goto out;
    }

    GB_STRCPYSTATIC(mobj.volume, volume);
    GB_STRCPYSTATIC(mobj.block_name, block);
    mobj.json_resp = json;
    getCommandString(&mobj.cmd, argcount, options);

    ret = glusterBlockCliRPC_1(&mobj, MODIFY_CLI);
    if (ret) {
      LOG("cli", GB_LOG_ERROR,
          "failed modifying auth of block %s on volume %s", block, volume);
    }
  } else if (!strcmp(options[optind], "size")) {
    optind++;
    sparse_ret = glusterBlockParseSize("cli", options[optind++], 0);
    if (sparse_ret < 0) {
      MSG(stderr, "'<size>' is incorrect");
      MSG(stderr, GB_MODIFY_HELP_STR);
      LOG("cli", GB_LOG_ERROR, "Modify failed while parsing size for block <%s/%s>",
          volume, block);
      goto out;
    }

    if ((argcount - optind) && !strcmp(options[optind], "force")) {
      optind++;
      msobj.force = true;
    }

    if ((argcount - optind)) {
      MSG(stderr, "unknown option '%s' for modify size:\n%s",
          options[optind], GB_MODIFY_HELP_STR);
      ret = -1;
      goto out;
    }

    GB_STRCPYSTATIC(msobj.volume, volume);
    GB_STRCPYSTATIC(msobj.block_name, block);
    msobj.size = sparse_ret;  /* size is unsigned long long */
    msobj.json_resp = json;
    getCommandString(&msobj.cmd, argcount, options);

    ret = glusterBlockCliRPC_1(&msobj, MODIFY_SIZE_CLI);
    if (ret) {
      LOG("cli", GB_LOG_ERROR,
          "failed modifying size of block %s on volume %s",
          msobj.block_name, msobj.volume);
    }
  } else {
    MSG(stderr, "unknown option '%s' for modify:\n%s",
        options[optind], GB_MODIFY_HELP_STR);
    ret = -1;
  }

 out:
  GB_FREE(msobj.cmd);
  GB_FREE(mobj.cmd);

  return ret;
}

static int
glusterBlockCreate(int argcount, char **options, int json)
{
  size_t optind = 1;
  int ret = -1;
  ssize_t sparse_ret;
  blockCreateCli cobj = {{0}, };
  bool TAKE_SIZE=true;
  bool PREALLOC_OPT=false;


  if (argcount <= optind) {
    MSG(stderr, "Inadequate arguments for create:\n%s", GB_CREATE_HELP_STR);
    return -1;
  }
  /* set defaults */
  cobj.json_resp = json;
  cobj.mpath = 1;
  cobj.prealloc = 1;

  if (glusterBlockParseVolumeBlock(options[optind++], cobj.volume, cobj.block_name,
                                    sizeof(cobj.volume), sizeof(cobj.block_name),
                                    GB_CREATE_HELP_STR, "create")) {
    goto out;
  }

  while (argcount - optind > 2) {
    switch (glusterBlockCLICreateOptEnumParse(options[optind++])) {
    case GB_CLI_CREATE_HA:
      if (isNumber(options[optind])) {
        sscanf(options[optind++], "%u", &cobj.mpath);
        if (!cobj.mpath) {
          MSG(stderr, "'ha' cannot be zero.");
          MSG(stderr, GB_CREATE_HELP_STR);
          LOG("cli", GB_LOG_ERROR, "failed parsing ha as 0 for block <%s/%s>",
              cobj.volume, cobj.block_name);
          goto out;
        }
      } else {
        MSG(stderr, "'ha' option is incorrect");
        MSG(stderr, GB_CREATE_HELP_STR);
        LOG("cli", GB_LOG_ERROR, "failed while parsing ha for block <%s/%s>",
            cobj.volume, cobj.block_name);
        goto out;
      }
      break;
    case GB_CLI_CREATE_AUTH:
      ret = convertStringToTrillianParse(options[optind++]);
      if(ret >= 0) {
        cobj.auth_mode = ret;
      } else {
        MSG(stderr, "'auth' option is incorrect");
        MSG(stderr, GB_CREATE_HELP_STR);
        LOG("cli", GB_LOG_ERROR, "Create failed while parsing argument "
                                 "to auth  for <%s/%s>",
                                 cobj.volume, cobj.block_name);
        goto out;
      }
      break;
    case GB_CLI_CREATE_PREALLOC:
      ret = convertStringToTrillianParse(options[optind++]);
      if(ret >= 0) {
        cobj.prealloc = ret;
        PREALLOC_OPT=true;
      } else {
        MSG(stderr, "'prealloc' option is incorrect");
        MSG(stderr, GB_CREATE_HELP_STR);
        LOG("cli", GB_LOG_ERROR, "Create failed while parsing argument "
                                 "to prealloc  for <%s/%s>",
                                 cobj.volume, cobj.block_name);
        goto out;
      }
      break;
    case GB_CLI_CREATE_STORAGE:
      GB_STRCPYSTATIC(cobj.storage, options[optind++]);
      TAKE_SIZE=false;
      break;
    case GB_CLI_CREATE_IO_TIMEOUT:
      if (isNumber(options[optind])) {
        sscanf(options[optind++], "%u", &cobj.io_timeout);
        if (cobj.io_timeout < 1) {
          MSG(stderr, "'io-timeout' should equal or larger than 1 second");
          MSG(stderr, GB_CREATE_HELP_STR);
          LOG("cli", GB_LOG_ERROR,
              "failed while parsing io-timeout for block <%s/%s>",
              cobj.volume, cobj.block_name);
        goto out;
        }
      } else {
        MSG(stderr, "'io-timeout' option is incorrect, hint: should be uint type");
        MSG(stderr, GB_CREATE_HELP_STR);
        LOG("cli", GB_LOG_ERROR, "failed while parsing io-timeout for block <%s/%s>",
            cobj.volume, cobj.block_name);
        goto out;
      }
      break;
    case GB_CLI_CREATE_RBSIZE:
      if (isNumber(options[optind])) {
        sscanf(options[optind++], "%u", &cobj.rb_size);
        if (cobj.rb_size < 1 || cobj.rb_size > 1024) {
          MSG(stderr, "'ring-buffer' should be in range [1MB - 1024MB]");
          MSG(stderr, GB_CREATE_HELP_STR);
          LOG("cli", GB_LOG_ERROR,
              "failed while parsing ring-buffer range [1MB - 1024MB] for block <%s/%s>",
              cobj.volume, cobj.block_name);
        goto out;
        }
      } else {
        MSG(stderr, "'ring-buffer' option is incorrect, hint: should be uint type");
        MSG(stderr, GB_CREATE_HELP_STR);
        LOG("cli", GB_LOG_ERROR, "failed while parsing ring-buffer for block <%s/%s>",
            cobj.volume, cobj.block_name);
        goto out;
      }
      break;
    case GB_CLI_CREATE_BLKSIZE:
      if (isNumber(options[optind])) {
        sscanf(options[optind++], "%u", &cobj.blk_size);
        if (cobj.blk_size % 512) {
          MSG(stderr, "'block-size' should be aligned to 512 bytes");
          MSG(stderr, GB_CREATE_HELP_STR);
          LOG("cli", GB_LOG_ERROR,
              "failed while parsing block-size aligned to 512 bytes for block <%s/%s>",
              cobj.volume, cobj.block_name);
        goto out;
        }
      } else {
        MSG(stderr, "'block-size' option is incorrect, hint: should be uint type");
        MSG(stderr, GB_CREATE_HELP_STR);
        LOG("cli", GB_LOG_ERROR, "failed while parsing block-size for block <%s/%s>",
            cobj.volume, cobj.block_name);
        goto out;
      }
      break;
    case GB_CLI_CREATE_OPT_MAX:
    default:
      MSG(stderr, "unknown option '%s'", options[optind - 1]);
      MSG(stderr, GB_CREATE_HELP_STR);
      LOG("cli", GB_LOG_ERROR, "received an unknown option '%s' for block <%s/%s>",
          options[optind - 1], cobj.volume, cobj.block_name);
      goto out;
    }
  }

  if (TAKE_SIZE) {
    if (argcount - optind != 2) {
      MSG(stderr, "Inadequate arguments for create:\n%s", GB_CREATE_HELP_STR);
      LOG("cli", GB_LOG_ERROR,
          "failed with Inadequate args for create block %s on volume %s with hosts %s",
          cobj.block_name, cobj.volume, cobj.block_hosts);
      goto out;
    }
  } else {
    if (PREALLOC_OPT) {
      MSG(stderr, "Inadequate arguments for create:\n%s", GB_CREATE_HELP_STR);
      MSG(stderr, "Hint: do not use [prealloc <full|no>] in combination with [storage <filename>] option");
      LOG("cli", GB_LOG_ERROR,
          "failed with Inadequate args for create block %s on volume %s with hosts %s",
          cobj.block_name, cobj.volume, cobj.block_hosts);
      goto out;
    }

    if (argcount - optind != 1) {
      MSG(stderr, "Inadequate arguments for create:\n%s", GB_CREATE_HELP_STR);
      MSG(stderr, "Hint: do not use [size] in combination with [storage <filename>] option");
      LOG("cli", GB_LOG_ERROR,
          "failed with Inadequate args for create block %s on volume %s with hosts %s",
          cobj.block_name, cobj.volume, cobj.block_hosts);
      goto out;
    }
  }

  if (GB_STRDUP(cobj.block_hosts, options[optind++]) < 0) {
    LOG("cli", GB_LOG_ERROR, "failed while parsing servers for block <%s/%s>",
        cobj.volume, cobj.block_name);
    goto out;
  }

  /* defend on the use of hostnames */
  ret = glusterBlockIsAddrListAcceptable(cobj.mpath, cobj.block_hosts);
  if (ret < 0) {
    MSG(stderr, "hostnames are not supported with gluster-block, use ips only");
    MSG(stderr, "Hint: if you are already using ips, make sure there are no typos");
  } else if (ret > 0) {
    MSG(stderr, "number of ip's in the list passed are not matching HA count requested");
  }
  if (ret) {
    LOG("cli", GB_LOG_ERROR,
        "failed while parsing the host-list for create block %s on volume %s with hosts %s",
        cobj.block_name, cobj.volume, cobj.block_hosts);
    ret = -1;
    goto out;
  }

  if (TAKE_SIZE) {
    sparse_ret = glusterBlockParseSize("cli", options[optind], cobj.blk_size);
    if (sparse_ret < 0) {
      MSG(stderr, "'[size]' is incorrect");
      MSG(stderr, GB_CREATE_HELP_STR);
      LOG("cli", GB_LOG_ERROR, "failed while parsing size for block <%s/%s>",
          cobj.volume, cobj.block_name);
      goto out;
    }
    cobj.size = sparse_ret;  /* size is unsigned long long */
  }

  getCommandString(&cobj.cmd, argcount, options);
  ret = glusterBlockCliRPC_1(&cobj, CREATE_CLI);
  if (ret) {
    LOG("cli", GB_LOG_ERROR,
        "failed creating block %s on volume %s with hosts %s",
        cobj.block_name, cobj.volume, cobj.block_hosts);
  }

 out:
  GB_FREE(cobj.block_hosts);
  GB_FREE(cobj.cmd);

  return ret;
}


static int
glusterBlockList(int argcount, char **options, int json)
{
  blockListCli cobj = {{0},};
  int ret = -1;


  GB_ARGCHECK_OR_RETURN(argcount, 2, "list", GB_LIST_HELP_STR);
  cobj.json_resp = json;

  GB_STRCPYSTATIC(cobj.volume, options[1]);

  ret = glusterBlockCliRPC_1(&cobj, LIST_CLI);
  if (ret) {
    LOG("cli", GB_LOG_ERROR, "failed listing blocks from volume %s",
        cobj.volume);
  }

  return ret;
}


static int
glusterBlockDelete(int argcount, char **options, int json)
{
  blockDeleteCli dobj = {{0},};
  size_t optind = 1;
  int ret = -1;


  if (argcount < 2 || argcount > 5) {
    MSG(stderr, "Inadequate arguments for delete:\n%s", GB_DELETE_HELP_STR);
    return -1;
  }

  dobj.json_resp = json;

  /* default: delete storage */
  dobj.unlink = 1;

  if (glusterBlockParseVolumeBlock (options[optind++], dobj.volume,
                                    dobj.block_name, sizeof(dobj.volume),
                                    sizeof(dobj.block_name), GB_DELETE_HELP_STR,
                                    "delete")) {
    goto out;
  }

  if ((argcount - optind) && !strcmp(options[optind], "unlink-storage")) {
    optind++;
    ret = convertStringToTrillianParse(options[optind++]);
    if(ret >= 0) {
      dobj.unlink = ret;
    } else {
      MSG(stderr, "'unlink-storage' option is incorrect");
      MSG(stderr, GB_DELETE_HELP_STR);
      LOG("cli", GB_LOG_ERROR, "Delete failed while parsing argument "
                               "to unlink-storage  for <%s/%s>",
                               dobj.volume, dobj.block_name);
      goto out;
    }
  }

  if ((argcount - optind) && !strcmp(options[optind], "force")) {
    optind++;
    dobj.force = true;
  }

  if (argcount - optind) {
    MSG(stderr, "Unknown option: '%s'\n%s", options[optind], GB_DELETE_HELP_STR);
    LOG("cli", GB_LOG_ERROR, "Delete failed parsing argument unknow option '%s'"
        " for <%s/%s>", options[optind], dobj.volume, dobj.block_name);
    goto out;
  }

  getCommandString(&dobj.cmd, argcount, options);

  ret = glusterBlockCliRPC_1(&dobj, DELETE_CLI);
  if (ret) {
    LOG("cli", GB_LOG_ERROR, "failed deleting block %s on volume %s",
        dobj.block_name, dobj.volume);
  }

 out:
  GB_FREE(dobj.cmd);

  return ret;
}

static int
glusterBlockReload(int argcount, char **options, int json)
{
  blockReloadCli robj = {{0},};
  int ret = -1;


  if (argcount < 2 || argcount > 3) {
    MSG(stderr, "Inadequate arguments for replace:\n%s", GB_RELOAD_HELP_STR);
    return -1;
  }

  robj.json_resp = json;

  if (glusterBlockParseVolumeBlock (options[1], robj.volume, robj.block_name,
                                    sizeof(robj.volume), sizeof(robj.block_name),
                                    GB_RELOAD_HELP_STR, "reload")) {
    goto out;
  }

  if (argcount == 3) {
    if (strcmp(options[2], "force")) {
      MSG(stderr, "unknown option '%s' for reload:\n%s", options[2], GB_RELOAD_HELP_STR);
      return -1;
    } else {
      robj.force = true;
    }
  }

  getCommandString(&robj.cmd, argcount, options);

  ret = glusterBlockCliRPC_1(&robj, RELOAD_CLI);
  if (ret) {
    LOG("cli", GB_LOG_ERROR,
        "failed reload of block %s on volume %s",
        robj.block_name, robj.volume);
  }

 out:

  return ret;
}

static int
glusterBlockInfo(int argcount, char **options, int json)
{
  blockInfoCli cobj = {{0},};
  int ret = -1;


  GB_ARGCHECK_OR_RETURN(argcount, 2, "info", GB_INFO_HELP_STR);
  cobj.json_resp = json;

  if (glusterBlockParseVolumeBlock (options[1], cobj.volume, cobj.block_name,
                                    sizeof(cobj.volume), sizeof(cobj.block_name),
                                    GB_INFO_HELP_STR, "info")) {
    goto out;
  }

  ret = glusterBlockCliRPC_1(&cobj, INFO_CLI);
  if (ret) {
    LOG("cli", GB_LOG_ERROR,
        "failed getting info of block %s on volume %s",
        cobj.block_name, cobj.volume);
  }

 out:

  return ret;
}


static int
glusterBlockReplace(int argcount, char **options, int json)
{
  blockReplaceCli robj = {{0},};
  int ret = -1;
  int optind = 1;


  if (argcount < 4 || argcount > 5) {
    MSG(stderr, "Inadequate arguments for replace:\n%s", GB_REPLACE_HELP_STR);
    return -1;
  }

  if (glusterBlockParseVolumeBlock(options[optind++], robj.volume, robj.block_name,
                                   sizeof(robj.volume), sizeof(robj.block_name),
                                   GB_REPLACE_HELP_STR, "replace")) {
    goto out;
  }

  if (!glusterBlockIsAddrAcceptable(options[optind])) {
    MSG(stderr, "host addr (%s) should be a valid ip address\n%s",
        options[optind], GB_REPLACE_HELP_STR);
    goto out;
  }
  GB_STRCPYSTATIC(robj.old_node, options[optind++]);

  if (!glusterBlockIsAddrAcceptable(options[optind])) {
    MSG(stderr, "host addr (%s) should be a valid ip address\n%s",
        options[optind], GB_REPLACE_HELP_STR);
    goto out;
  }
  GB_STRCPYSTATIC(robj.new_node, options[optind++]);

  if (!strcmp(robj.old_node, robj.new_node)) {
    MSG(stderr, "<old-node> (%s) and <new-node> (%s) cannot be same\n%s",
        robj.old_node, robj.new_node, GB_REPLACE_HELP_STR);
    goto out;
  }

  robj.json_resp = json;

  if (argcount == 5) {
    if (strcmp(options[optind], "force")) {
      MSG(stderr, "unknown option '%s' for replace:\n%s", options[optind], GB_REPLACE_HELP_STR);
      return -1;
    } else {
      robj.force = true;
    }
  }

  getCommandString(&robj.cmd, argcount, options);
  ret = glusterBlockCliRPC_1(&robj, REPLACE_CLI);
  if (ret) {
    LOG("cli", GB_LOG_ERROR, "failed replace on volume %s",
        robj.volume);
  }

 out:
  GB_FREE(robj.cmd);

  return ret;
}


static int
glusterBlockGenConfig(int argcount, char **options, int json)
{
  blockGenConfigCli robj = {0,};
  int ret = -1;
  int optind = 1;


  GB_ARGCHECK_OR_RETURN(argcount, 4, "genconfig", GB_GENCONF_HELP_STR);

  if (!glusterBlockIsVolListAcceptable(options[optind])) {
    MSG(stderr, "volume list(%s) should be delimited by '%c' character only\n%s",
        options[optind], GB_DELIMITER, GB_GENCONF_HELP_STR);
    goto out;
  }

  GB_STRDUP(robj.volume, options[optind++]);

  if (!strcmp(options[optind++], "enable-tpg")) {
    if (!glusterBlockIsAddrAcceptable(options[optind])) {
      MSG(stderr, "host addr (%s) should be a valid ip address\n%s",
          options[optind -1], GB_REPLACE_HELP_STR);
      goto out;
    }
    GB_STRCPYSTATIC(robj.addr, options[optind]);
  } else {
      MSG(stderr, "unknown option '%s' for genconfig:\n%s", options[optind -1], GB_GENCONF_HELP_STR);
      goto out;
  }
  robj.json_resp = json;

  ret = glusterBlockCliRPC_1(&robj, GENCONF_CLI);
  if (ret) {
    LOG("cli", GB_LOG_ERROR, "failed genconfig on volume %s", robj.volume);
  }

 out:
  GB_FREE(robj.volume);

  return ret;
}


static int
glusterBlockParseArgs(int count, char **options, size_t opt, int json)
{
  int ret = 0;


  while (1) {
    switch (opt) {
    case GB_CLI_CREATE:
      ret = glusterBlockCreate(count, options, json);
      if (ret && ret != EEXIST) {
        LOG("cli", GB_LOG_ERROR, FAILED_CREATE);
      }
      goto out;

    case GB_CLI_LIST:
      ret = glusterBlockList(count, options, json);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, FAILED_LIST);
      }
      goto out;

    case GB_CLI_INFO:
      ret = glusterBlockInfo(count, options, json);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, FAILED_INFO);
      }
      goto out;

    case GB_CLI_MODIFY:
      ret = glusterBlockModify(count, options, json);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, FAILED_MODIFY);
      }
      goto out;

    case GB_CLI_REPLACE:
      ret = glusterBlockReplace(count, options, json);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, FAILED_REPLACE);
      }
      goto out;

    case GB_CLI_GENCONFIG:
      ret = glusterBlockGenConfig(count, options, json);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, FAILED_GENCONFIG);
      }
      goto out;

    case GB_CLI_DELETE:
      ret = glusterBlockDelete(count, options, json);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, FAILED_DELETE);
      }
      goto out;

    case GB_CLI_RELOAD:
      ret = glusterBlockReload(count, options, json);
      if (ret) {
        LOG("cli", GB_LOG_ERROR, FAILED_RELOAD);
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
      MSG(stdout, "%s", argp_program_version);
      goto out;

    default:
      ret = -1;
      goto out;
    }
  }

 out:
  return ret;
}


int
main(int argc, char *argv[])
{
  size_t opt = 0;
  int count = 1;
  int args = argc;
  int json = GB_JSON_NONE;
  int ret;


  if (argc <= 1) {
    glusterBlockHelp();
    goto fail;
  }

  if (initGbConfig()) {
    goto fail;
  }

  if(initLogging()) {
    goto fail;
  }

  while (1) {
    opt = glusterBlockCLIOptEnumParse(argv[count]);
    if (opt < 1 || opt >= GB_CLI_OPT_MAX) {
      MSG(stderr, "Unknown option: %s", argv[count]);
      goto fail;
    }
    if (opt == GB_CLI_TIMEOUT) {
      if (args <= 3) {     /* some command is manditory */
        glusterBlockHelp();
        goto fail;
      }

      if (isNumber(argv[2])) {
        sscanf(argv[2], "%ld", &cliOptTimeout);
      } else {
        MSG(stderr, "'timeout' option is incorrect, check usage.");
        MSG(stderr, "hint: timeout argument accept only time in seconds.");
        goto fail;
      }
      args = args - 2;
      count = count + 2;
      continue;
    }

    json = jsonResponseFormatParse(argv[argc-1]);
    if (json == GB_JSON_MAX) {
      MSG(stderr, "expecting '--json*', got '%s'",
          argv[argc-1]);
      return -1;
    } else if (json != GB_JSON_NONE) {
      args = args - 1; /*Commands don't need to handle json*/
    }
    break;
  }

  ret = glusterBlockParseArgs(args - 1, &argv[count], opt, json);

  finiGbConfig();

  return ret;

 fail:
  finiGbConfig();
  exit(EXIT_FAILURE);
}
