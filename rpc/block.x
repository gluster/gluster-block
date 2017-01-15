

struct blockTrans {
  int     exit;    /* exit code of the command */
  string  out<>;   /* stdout of the command */
};

program GLUSTER_BLOCK {
  version GLUSTER_BLOCK_VERS {
    blockTrans BLOCK_EXEC(string) = 1;
  } = 1;
} = 21215311; /* B2 L12 O15 C3 K11 */
