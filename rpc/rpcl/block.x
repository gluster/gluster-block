/* rpcgen generates code that results in unused-variable warnings */
#ifdef RPC_XDR
%#include "rpc-pragmas.h"
#endif

enum JsonResponseFormat {
  GB_JSON_NONE           = 0,

  GB_JSON_PLAIN          = 1,
  GB_JSON_SPACED         = 2,
  GB_JSON_PRETTY         = 3,
  GB_JSON_DEFAULT        = 4,

  GB_JSON_MAX
};

struct blockCreate {
  char      ipaddr[255];
  char      volume[255];
  char      gbid[127];                   /* uuid */
  char      passwd[127];                 /* uuid */
  u_quad_t  size;
  char      block_name[255];
  string    block_hosts<>;               /* for multiple tpg's creation */
  bool      auth_mode;
};

struct blockCreate2 {
  char      ipaddr[255];
  char      volume[255];
  char      gbid[127];                   /* uuid */
  char      passwd[127];                 /* uuid */
  u_quad_t  size;
  char      block_name[255];
  char      prio_path[255];
  string    block_hosts<>;               /* for multiple tpg's creation */
  bool      auth_mode;
  u_int     rb_size;                     /* TCMU Ring Buffer size in kernel */
  opaque    xdata<>;                     /* future reserve */
};

struct blockModify {
  char      volume[255];
  char      block_name[255];
  char      gbid[127];
  char      passwd[127];
  bool      auth_mode;
};

struct blockModifySize {
  char      volume[255];
  char      block_name[255];
  char      gbid[127];
  u_quad_t  size;
};

struct blockReplace {
  char      volume[255];
  char      block_name[255];
  char      gbid[127];
  char      ipaddr[255];
  char      ripaddr[255];
};

struct blockCreateCli {
  char      volume[255];
  u_quad_t  size;
  u_int     rb_size;              /* TCMU Ring Buffer size in kernel */
  u_int     blk_size;             /* TCMU hw block size in kernel */
  u_int     io_timeout;           /* Cmds timeout in tcmu-runner */
  u_int     mpath;                /* HA request count */
  bool      auth_mode;
  bool      prealloc;
  char      storage[255];
  char      block_name[255];
  string    block_hosts<>;
  string    cmd<>;
  enum JsonResponseFormat     json_resp;
};

struct blockDeleteCli {
  char      block_name[255];
  char      volume[255];
  bool      unlink;
  bool      force;
  string    cmd<>;
  enum JsonResponseFormat     json_resp;
};

struct blockDelete {
  char      block_name[255];
  char      gbid[127];
};

struct blockReloadCli {
  char      block_name[255];
  char      volume[255];
  bool      force;
  string    cmd<>;
  enum JsonResponseFormat     json_resp;
};

struct blockReload {
  char      block_name[255];
  char      gbid[127];
};

struct blockInfoCli {
  char      block_name[255];
  char      volume[255];
  enum JsonResponseFormat     json_resp;
};

struct blockListCli {
  char      volume[255];
  u_quad_t  offset;      /* dentry d_name offset */
  enum JsonResponseFormat     json_resp;
};

struct blockModifyCli {
  char      block_name[255];
  char      volume[255];
  bool      auth_mode;
  string     cmd<>;
  enum JsonResponseFormat     json_resp;
};

struct blockModifySizeCli {
  char      block_name[255];
  char      volume[255];
  u_quad_t  size;
  bool      force;
  string    cmd<>;
  enum JsonResponseFormat     json_resp;
};

struct blockReplaceCli {
  char      volume[255];
  char      block_name[255];
  char      old_node[255];
  char      new_node[255];
  bool      force;
  string    cmd<>;
  enum JsonResponseFormat     json_resp;
};

struct blockGenConfigCli {
  string    volume<>;
  char      addr[255];
  enum JsonResponseFormat     json_resp;
};

struct blockResponse {
  int       exit;       /* exit code of the command */
  string    out<>;      /* output; TODO: return respective objects */
  u_quad_t  offset;     /* dentry d_name offset */
  opaque    xdata<>;    /* future reserve */
};

program GLUSTER_BLOCK {
  version GLUSTER_BLOCK_VERS {
    blockResponse BLOCK_CREATE(blockCreate) = 1;
    blockResponse BLOCK_DELETE(blockDelete) = 2;
    blockResponse BLOCK_MODIFY(blockModify) = 3;
    blockResponse BLOCK_VERSION() = 4;
    blockResponse BLOCK_REPLACE(blockReplace) = 5;
    blockResponse BLOCK_MODIFY_SIZE(blockModifySize) = 6;

    blockResponse BLOCK_CREATE_V2(blockCreate2) = 7;
    blockResponse BLOCK_RELOAD(blockReload) = 8;
  } = 1;
} = 21215311; /* B2 L12 O15 C3 K11 */

program GLUSTER_BLOCK_CLI {
  version GLUSTER_BLOCK_CLI_VERS {
    blockResponse BLOCK_CREATE_CLI(blockCreateCli) = 1;
    blockResponse BLOCK_LIST_CLI(blockListCli) = 2;
    blockResponse BLOCK_INFO_CLI(blockInfoCli) = 3;
    blockResponse BLOCK_DELETE_CLI(blockDeleteCli) = 4;
    blockResponse BLOCK_MODIFY_CLI(blockModifyCli) = 5;
    blockResponse BLOCK_REPLACE_CLI(blockReplaceCli) = 6;
    blockResponse BLOCK_MODIFY_SIZE_CLI(blockModifySizeCli) = 7;
    blockResponse BLOCK_GEN_CONFIG_CLI(blockGenConfigCli) = 8;
    blockResponse BLOCK_RELOAD_CLI(blockReloadCli) = 9;
  } = 1;
} = 212153113; /* B2 L12 O15 C3 K11 C3 */
