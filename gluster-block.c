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
# include  <getopt.h>
# include  <uuid/uuid.h>

# include "utils.h"
# include "glfs-operations.h"
# include "ssh-common.h"

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
# define  ATTRIBUTES       "generate_node_acls=1 demo_mode_write_protect=0"
# define  LIST_CMD         "ls | grep ' %s ' | cut -d'[' -f2 | cut -d']' -f1"
# define  LUNS_LIST        "ls | grep -v user:glfs | cut -d'-' -f2 | cut -d' ' -f2"

# define  IQN_PREFIX       "iqn.2016-12.org.gluster-block:"

# define MSERVER_DELIMITER ","



typedef struct blockServerDef {
  size_t nhosts;
  char   **hosts;
} blockServerDef;
typedef blockServerDef *blockServerDefPtr;


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
      " -m, --modify      <RESIZE|AUTH>   Modify the metadata\n"
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


static int
glusterBlockCreate(int count, char **options, char *name)
{
  int c;
  int ret = 0;
  char *cmd = NULL;
  char *exec = NULL;
  char *iqn = NULL;
  char *blkServers = NULL;
  blockServerDefPtr list;
  uuid_t out;
  glusterBlockDefPtr blk;
  char *sshout;
  size_t i;


  if (GB_ALLOC(blk) < 0)
    return -1;

  blk->filename = CALLOC(UUID_BUF_SIZE);

  uuid_generate(out);
  uuid_unparse(out, blk->filename);

  if (!name) {
    ERROR("%s", "Insufficient arguments supplied for"
                "'gluster-block create'");
    ret = -1;
    goto out;
  }

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
      blkServers = optarg;
      break;

    case 'v':
      GB_STRDUP(blk->volume, optarg);
      ret++;
      break;

    case 'h':
      GB_STRDUP(blk->host, optarg);
      ret++;
      break;

    case 's':
      blk->size = glusterBlockCreateParseSize(optarg);
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

  if (blkServers) {
    list = blockServerParse(blkServers);
  } else {
    list = blockServerParse("localhost");
  }

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

  ret = glusterBlockCreateEntry(blk);
  if (ret)
    goto out;

  if (asprintf(&cmd, "%s %s %s %zu %s@%s/%s", TARGETCLI_GLFS,
               CREATE, name, blk->size, blk->volume, blk->host,
               blk->filename) < 0)
    goto out;

  /* Created user-backed storage object LUN size 2147483648. */
  for (i = 0; i < list->nhosts; i++) {
    MSG("[OnHost: %s]", list->hosts[i]);
    sshout = glusterBlockSSHRun(list->hosts[i], cmd, true);
    if (!sshout) {
      ret = -1;
      goto out;
    }

    asprintf(&iqn, "%s%s-%s",
             IQN_PREFIX, list->hosts[i], blk->filename);
    asprintf(&exec, "%s %s %s", TARGETCLI_ISCSI, CREATE, iqn);
    sshout = glusterBlockSSHRun(list->hosts[i], exec, true);
    if (!sshout) {
      ret = -1;
      goto out;
    }
    GB_FREE(exec);


    asprintf(&exec, "%s/%s/tpg1/luns %s %s/%s",
             TARGETCLI_ISCSI, iqn, CREATE, GLFS_PATH, name);
    sshout = glusterBlockSSHRun(list->hosts[i], exec, true);
    if (!sshout) {
      ret = -1;
      goto out;
    }
    GB_FREE(exec);


    asprintf(&exec, "%s/%s/tpg1 set attribute %s",
             TARGETCLI_ISCSI, iqn, ATTRIBUTES);
    sshout = glusterBlockSSHRun(list->hosts[i], exec, true);
    if (!sshout) {
      ret = -1;
      goto out;
    }
    GB_FREE(exec);
    GB_FREE(iqn);
    putchar('\n');
  }

 out:
  GB_FREE(cmd);
  GB_FREE(exec);
  GB_FREE(iqn);
  glusterBlockDefFree(blk);
  blockServerDefFree(list);

  return ret;
}


static int
glusterBlockParseCfgStringToDef(char* cfgstring,
                                glusterBlockDefPtr blk)
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
  if (GB_STRDUP(blk->volume, p) < 0) {
    ret = -1;
    goto fail;
  }

  /* part between '@' and '/' is the server name */
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

  /* part between '/' and '(' is the filename */
  p = sep + 1;
  sep = strchr(p, '(');
  if (!sep) {
    ret = -1;
    goto fail;
  }

  *(sep - 1) = '\0';  /* discard extra space at end of filename */
  if (GB_STRDUP(blk->filename, p) < 0) {
    ret = -1;
    goto fail;
  }

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
    ret = -1;
    goto fail;
  }


  /* part between ')' and '\n' is the status */
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


static char *
getCfgstring(char* name, char *blkServer)
{
  FILE *fd;
  char *cmd;
  char *exec;
  char *sshout;
  char *buf = CALLOC(CFG_STRING_SIZE);

  asprintf(&cmd, "%s %s", TARGETCLI_GLFS, LIST_CMD);
  asprintf(&exec, cmd, name);

  sshout = glusterBlockSSHRun(blkServer, exec, false);
  if (!sshout) {
    GB_FREE(buf);
    buf = NULL;
    goto fail;
  }

  fd = fopen(sshout, "r");
  if (!fgets(buf, CFG_STRING_SIZE, fd)) {
    GB_FREE(buf);
    buf = NULL;
  }

 fail:
  fclose(fd);
  remove(sshout);
  GB_FREE(sshout);
  GB_FREE(cmd);
  GB_FREE(exec);

  return buf;
}


/* TODO: need to implement sessions [ list | detail ] [sid] */
static int
glusterBlockList(blockServerDefPtr blkServers)
{
  size_t i;
  int ret;
  char *cmd;
  char *pos;
  char *sshout;
  char *cfgstring = NULL;
  char buf[CFG_STRING_SIZE];
  FILE *fd = NULL;
  glusterBlockDefPtr blk = NULL;

  MSG("%s", "BlockName      Volname      Host      Size      Status");

  asprintf(&cmd, "%s %s", TARGETCLI_GLFS, LUNS_LIST);

  for (i = 0; i < blkServers->nhosts; i++) {
    MSG("[OnHost: %s]", blkServers->hosts[i]);
    sshout = glusterBlockSSHRun(blkServers->hosts[i], cmd, false);
    if (!sshout) {
      ret = -1;
      goto fail;
    }

    fd = fopen(sshout, "r");
    while (fgets(buf, sizeof(buf), fd)) {
      pos = strtok(buf, "\n");

      cfgstring = getCfgstring(pos, blkServers->hosts[i]);
      if (!cfgstring) {
        ret = -1;
        goto fail;
      }

      if (GB_ALLOC(blk) < 0)
        goto fail;

      ret = glusterBlockParseCfgStringToDef(cfgstring, blk);
      if (ret)
        goto fail;

      fprintf(stdout, " %s   %s   %s   %s   %s\n",
              pos, blk->volume, blk->host,
              glusterBlockListGetHumanReadableSize(blk->size),
              blk->status ? "Online" : "Offline");

      GB_FREE(cfgstring);
      glusterBlockDefFree(blk);
    }

    fclose(fd);
    remove(sshout);
    GB_FREE(sshout);
  }

  GB_FREE(cmd);
  return 0;

 fail:
  fclose(fd);
  remove(sshout);
  glusterBlockDefFree(blk);
  GB_FREE(cfgstring);

  GB_FREE(sshout);
  GB_FREE(cmd);

  return -1;
}


static int
glusterBlockDelete(char* name, blockServerDefPtr blkServers)
{
  size_t i;
  int ret;
  char *cmd;
  char *exec = NULL;
  char *sshout;
  char *cfgstring = NULL;
  char *iqn = NULL;
  glusterBlockDefPtr blk = NULL;

  asprintf(&cmd, "%s %s %s", TARGETCLI_GLFS, DELETE, name);

  for (i = 0; i < blkServers->nhosts; i++) {
    MSG("[OnHost: %s]", blkServers->hosts[i]);
    if (cfgstring)
      GB_FREE(cfgstring);
    cfgstring = getCfgstring(name, blkServers->hosts[i]);
    if (!cfgstring) {
      ret = -1;
      goto fail;
    }

    sshout = glusterBlockSSHRun(blkServers->hosts[i], cmd, true);
    if (!sshout) {
      ret = -1;
      goto fail;
    }

    /* cfgstring is constant across all tcmu nodes */
    if (!blk) {
      if (GB_ALLOC(blk) < 0)
        goto fail;

      ret = glusterBlockParseCfgStringToDef(cfgstring, blk);
      if (ret)
        goto fail;
    }

    asprintf(&iqn, "%s%s-%s",
             IQN_PREFIX, blkServers->hosts[i], blk->filename);
    asprintf(&exec, "%s %s %s", TARGETCLI_ISCSI, DELETE, iqn);
    sshout = glusterBlockSSHRun(blkServers->hosts[i], exec, true);
    if (!sshout) {
      ret = -1;
      goto fail;
    }
    GB_FREE(exec);
    GB_FREE(iqn);
  }

  ret = glusterBlockDeleteEntry(blk);

 fail:
  glusterBlockDefFree(blk);
  GB_FREE(cfgstring);
  GB_FREE(iqn);
  GB_FREE(exec);
  GB_FREE(cmd);

  return ret;
}


static int
glusterBlockInfo(char* name, blockServerDefPtr blkServers)
{
  size_t i;
  int ret = 0;
  char *cmd;
  char *sshout;

  asprintf(&cmd, "%s/%s %s", TARGETCLI_GLFS, name, INFO);

  for (i = 0; i < blkServers->nhosts; i++) {
    MSG("[OnHost: %s]", blkServers->hosts[i]);
    sshout = glusterBlockSSHRun(blkServers->hosts[i], cmd, true);
    if (!sshout)
      ret = -1;
    putchar('\n');
  }

  GB_FREE(cmd);
  return ret;
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
  } else {
    list = blockServerParse("localhost");
  }

  switch (optFlag) {
  case 'l':
    ret = glusterBlockList(list);
    break;
  case 'i':
    ret = glusterBlockInfo(block, list);
    break;
  case 'd':
    ret = glusterBlockDelete(block, list);
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
