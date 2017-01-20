
struct blockCreate {
  char      volume[255];
  char      volfileserver[255];
  char      gbid[127];                   /* uuid */
  u_quad_t  size;
  char      block_name[255];
};

struct blockCreateCli {
  char      volume[255];
  char      volfileserver[255];
  u_quad_t  size;
  char      block_name[255];
  string    block_hosts<>;
};

struct blockResponse {
  int       exit;       /* exit code of the command */
  string    out<>;      /* json output */
  u_quad_t  offset;     /* dentry d_name offset */
  opaque    xdata<>;    /* future reserve */
};

program GLUSTER_BLOCK_CLI {
  version GLUSTER_BLOCK_CLI_VERS {
    blockResponse BLOCK_CREATE_CLI(blockCreateCli) = 1;
  } = 1;
} = 212153113; /* B2 L12 O15 C3 K11 C3 */

program GLUSTER_BLOCK {
  version GLUSTER_BLOCK_VERS {
    blockResponse BLOCK_CREATE(blockCreate) = 1;
  } = 1;
} = 21215311; /* B2 L12 O15 C3 K11 */
