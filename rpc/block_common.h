
/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# ifndef   _BLOCK_COMMON_H
# define   _BLOCK_COMMON_H      1

# include  "common.h"
# include  "capabilities.h"
# include  "glfs-operations.h"

# include  <pthread.h>
# include  <netdb.h>
# include  <uuid/uuid.h>
# include  <json-c/json.h>


# define   UUID_BUF_SIZE        38

# define   GB_DEFAULT_ERRCODE   255

# define   GB_TGCLI_GLFS_PATH   "/backstores/user:glfs"
# define   GB_TGCLI_GLFS        "targetcli " GB_TGCLI_GLFS_PATH
# define   GB_TGCLI_CHECK       GB_TGCLI_GLFS " ls | grep ' %s ' | grep '/%s' > " DEVNULLPATH
# define   GB_TGCLI_ISCSI_PATH  "/iscsi"
# define   GB_TGCLI_ISCSI       "targetcli " GB_TGCLI_ISCSI_PATH
# define   GB_TGCLI_ISCSI_CHECK GB_TGCLI_ISCSI " ls | grep ' %s%s ' > " DEVNULLPATH
# define   GB_TGCLI_GLFS_SAVE   GB_TGCLI_GLFS_PATH "/%s saveconfig"
# define   GB_TGCLI_ATTRIBUTES  "generate_node_acls=1 demo_mode_write_protect=0"
# define   GB_TGCLI_IQN_PREFIX  "iqn.2016-12.org.gluster-block:"

# define   GB_RING_BUFFER_STR   "max_data_area_mb"
# define   GB_BLOCK_SIZE_STR    "hw_block_size"
# define   GB_IO_TIMEOUT_STR    "tcmur_cmd_time_out"

#define    GB_CMD_TIME_OUT      130

# define   GB_JSON_OBJ_TO_STR(x) json_object_new_string(x?x:"")
# define   GB_DEFAULT_ERRMSG    "Operation failed, please check the log "\
                                "file to find the reason."

# define   GB_OLD_CAP_MAX       9

# define   GB_OP_SKIPPED        222
# define   GB_NODE_NOT_EXIST    223
# define   GB_NODE_IN_USE       224
# define   GB_BLOCK_NOT_LOADED  225
# define   GB_BLOCK_NOT_FOUND   226


typedef enum operations {
  CREATE_SRV = 1,
  DELETE_SRV,
  MODIFY_SRV,
  MODIFY_TPGC_SRV,
  MODIFY_SIZE_SRV,
  REPLACE_SRV,
  REPLACE_GET_PORTAL_TPG_SRV,
  LIST_SRV,
  INFO_SRV,
  VERSION_SRV,
  GENCONFIG_SRV,
  RELOAD_SRV
} operations;


typedef struct blockRemoteObj {
    struct glfs *glfs;
    void *obj;
    char *volume;
    char *addr;
    char *reply;
    int  exit;
} blockRemoteObj;


typedef struct blockRemoteResp {
  char *attempt;
  char *success;
  char *skipped;
  int   status;
} blockRemoteResp;


typedef struct blockRemoteReplaceResp {
  blockRemoteResp *cop;
  blockRemoteResp *dop;
  blockRemoteResp *rop;
  bool force;
  int   status;
} blockRemoteReplaceResp;


typedef struct blockRemoteModifyResp {
  char *attempt;
  char *success;
  char *rb_attempt;
  char *rb_success;
} blockRemoteModifyResp;


typedef struct soTgObj {
   struct json_object *so_arr;
   struct json_object *tg_arr;
} soTgObj;


typedef struct blockRemoteDeleteResp {
  char *d_attempt;
  char *d_success;
} blockRemoteDeleteResp;


typedef struct blockRemoteCreateResp {
  char *errMsg;
  char *backend_size;
  char *iqn;
  size_t nportal;
  char **portal;
  blockRemoteDeleteResp *obj;
} blockRemoteCreateResp;


extern pthread_mutex_t lock;


int mapJsonFlagToJsonCstring(int jsonflag);

void blockStr2arrayAddToJsonObj(json_object *json_obj,
                                char *string, char *label);

void blockFormatErrorResponse(operations op, int json_resp, int errCode,
                              char *errMsg, blockResponse *reply);

int blockValidateCommandOutput(const char *out, int opt, void *data);

void convertTypeCreate2ToCreate(blockCreate2 *blk_v2, blockCreate *blk_v1);

int glusterBlockCollectAttemptSuccess(blockRemoteObj *args, MetaInfo *info,
                                      operations opt, size_t count,
                                      char **attempt, char **success);

blockServerDefPtr blockMetaInfoToServerParse(MetaInfo *info);

blockServerDefPtr glusterBlockGetListFromInfo(MetaInfo *info);

int blockGetHostStatus(MetaInfo *info, char *host);

void blockRemoteRespFree(blockRemoteResp *resp);

int blockCheckBlockLoadedStatus(char *block_name,
                                char *gbid, blockResponse *reply);

char *blockInfoGetCurrentSizeOfNode(char *block_name,
                                    MetaInfo *info, char *host);

int glusterBlockCallRPC_1(char *host, void *cobj, operations opt,
                          bool *rpc_sent, char **out);

void *glusterBlockCreateRemote(void *data);

void *glusterBlockDeleteRemote(void *data);

int glusterBlockCleanUp(struct glfs *glfs, char *blockname, bool forcedel,
                        bool unlink, blockRemoteDeleteResp *drobj);

int glusterBlockCheckCapabilities(void* blk, operations opt,
                                  blockServerDefPtr list,
                                  bool *resultCaps, char **errMsg);

#endif  /* _BLOCK_COMMON_H */
