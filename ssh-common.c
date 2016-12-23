/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of gluster-block.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


# define   _GNU_SOURCE

# include  <stdio.h>
# include  <stdlib.h>
# include  <string.h>
# include  <stdbool.h>
# include  <errno.h>
# include  <uuid/uuid.h>

# include  "utils.h"
# include  "ssh-common.h"



static int
glusterBlockSSHAuthKbdint(ssh_session blksession, const char *password)
{
  int err;
  const char *ret;
  const char *answer;
  char buffer[128];
  size_t i;
  int n;

  err = ssh_userauth_kbdint(blksession, NULL, NULL);
  while (err == SSH_AUTH_INFO) {
    ret = ssh_userauth_kbdint_getname(blksession);
    if (ret && strlen(ret) > 0)
      MSG("%s", ret);

    ret = ssh_userauth_kbdint_getinstruction(blksession);
    if (ret && strlen(ret) > 0)
      MSG("%s", ret);

    n = ssh_userauth_kbdint_getnprompts(blksession);
    for (i = 0; i < n; i++) {
      char echo;

      ret = ssh_userauth_kbdint_getprompt(blksession, i, &echo);
      if (!ret)
        break;

      if (echo) {
        char *p;

        MSG("%s", ret);

        if (!fgets(buffer, sizeof(buffer), stdin))
          return SSH_AUTH_ERROR;

        buffer[sizeof(buffer) - 1] = '\0';
        if ((p = strchr(buffer, '\n')))
          *p = '\0';

        if (ssh_userauth_kbdint_setanswer(blksession, i, buffer) < 0)
          return SSH_AUTH_ERROR;

        memset(buffer, 0, strlen(buffer));
      } else {
        if (password && strstr(ret, "Password:")) {
          answer = password;
        } else {
          buffer[0] = '\0';

          if (ssh_getpass(ret, buffer, sizeof(buffer), 0, 0) < 0)
            return SSH_AUTH_ERROR;

          answer = buffer;
        }
        err = ssh_userauth_kbdint_setanswer(blksession, i, answer);
        memset(buffer, 0, sizeof(buffer));
        if (err < 0)
          return SSH_AUTH_ERROR;
      }
    }
    err = ssh_userauth_kbdint(blksession, NULL, NULL);
  }

  return err;
}


static int
glusterBlockSSHAuthConsole(ssh_session blksession)
{
  int rc;
  int method;
  char *banner;
  char password[128] = {0};

  // Try to authenticate
  rc = ssh_userauth_none(blksession, NULL);
  if (rc == SSH_AUTH_ERROR) {
    ERROR("%s", ssh_get_error(blksession));
    return rc;
  }

  method = ssh_userauth_list(blksession, NULL);

  while (rc != SSH_AUTH_SUCCESS) {

    // Try to authenticate through the "gssapi-with-mic" method.
    if (method & SSH_AUTH_METHOD_GSSAPI_MIC) {
      rc = ssh_userauth_gssapi(blksession);
      if (rc == SSH_AUTH_ERROR) {
        ERROR("%s", ssh_get_error(blksession));
        return rc;
      } else if (rc == SSH_AUTH_SUCCESS) {
        break;
      }
    }

    // Try to authenticate with public key first
    if (method & SSH_AUTH_METHOD_PUBLICKEY) {
      rc = ssh_userauth_publickey_auto(blksession, NULL, NULL);
      if (rc == SSH_AUTH_ERROR) {
        ERROR("%s", ssh_get_error(blksession));
        return rc;
      } else if (rc == SSH_AUTH_SUCCESS) {
        break;
      }
    }

    // Try to authenticate with keyboard interactive";
    if (method & SSH_AUTH_METHOD_INTERACTIVE) {
      rc = glusterBlockSSHAuthKbdint(blksession, NULL);
      if (rc == SSH_AUTH_ERROR) {
        ERROR("%s", ssh_get_error(blksession));
        return rc;
      } else if (rc == SSH_AUTH_SUCCESS) {
        break;
      }
    }

    if (ssh_getpass("Password: ", password, sizeof(password),
                                            0, 0) < 0)
      return SSH_AUTH_ERROR;

    // Try to authenticate with password
    if (method & SSH_AUTH_METHOD_PASSWORD) {
      rc = ssh_userauth_password(blksession, NULL, password);
      if (rc == SSH_AUTH_ERROR) {
        ERROR("%s", ssh_get_error(blksession));
        return rc;
      } else if (rc == SSH_AUTH_SUCCESS) {
        break;
      }
    }

    memset(password, 0, sizeof(password));
  }

  banner = ssh_get_issue_banner(blksession);
  if (banner) {
    ERROR("%s", banner);
    ssh_string_free_char(banner);
  }

  return rc;
}


static int
glusterBlockSSHVerifyKnownHost(ssh_session blksession)
{
  ssh_key srv_pubkey;
  unsigned char *hash = NULL;
  char *hexa;
  char buf[10];
  size_t hlen;
  int rc;
  int ret;

  rc = ssh_get_publickey(blksession, &srv_pubkey);
  if (rc < 0)
    return -1;

  rc = ssh_get_publickey_hash(srv_pubkey,
                              SSH_PUBLICKEY_HASH_SHA1, &hash, &hlen);
  ssh_key_free(srv_pubkey);
  if (rc < 0)
    return -1;

  switch (ssh_is_server_known(blksession)) {

  case SSH_SERVER_KNOWN_OK:
    break; /* ok we have password less access */

  case SSH_SERVER_KNOWN_CHANGED:
    ERROR("%s", "Host key for server changed : server's one is new :");
    ssh_print_hexa("Public key hash", hash, hlen);
    ssh_clean_pubkey_hash(&hash);
    ERROR("%s", "For security reason, connection will be stopped");
    return -1;

  case SSH_SERVER_FOUND_OTHER:
    ERROR("%s", "The host key for this server was not found, but an other "
                "type of key exists.");
    ERROR("%s", "An attacker might change the default server key to "
                "confuse your client into thinking the key does not exist"
                "\nWe advise you to rerun the client with -d or -r for "
                "more safety.");
    return -1;

  case SSH_SERVER_FILE_NOT_FOUND:
    ERROR("%s", "Could not find known host file. If you accept the host "
                "key here, the file will be automatically created.");
    /* fallback to SSH_SERVER_NOT_KNOWN behavior */

  case SSH_SERVER_NOT_KNOWN:
    hexa = ssh_get_hexa(hash, hlen);
    ERROR("The server is unknown. Do you trust the host key ?\n"
          "Public key hash: %s", hexa);
    ssh_string_free_char(hexa);
    if (!fgets(buf, sizeof(buf), stdin)) {
      ret = -1;
      goto fail;
    }
    if (strncasecmp(buf, "yes", 3) != 0) {
      ret = -1;
      goto fail;
    }
    ERROR("%s", "This new key will be written on disk for further usage. "
                "do you agree ?");
    if (!fgets(buf, sizeof(buf), stdin)) {
      ret = -1;
      goto fail;
    }
    if (strncasecmp(buf, "yes", 3) == 0) {
      if (ssh_write_knownhost(blksession) < 0) {
        ERROR("%s", strerror(errno));
        ret = -1;
        goto fail;
      }
    }
    break;

  case SSH_SERVER_ERROR:
    ERROR("%s", ssh_get_error(blksession));
    ret = -1;
    goto fail;
  }

  ret = 0;

 fail:
  ssh_clean_pubkey_hash(&hash);

  return ret;
}


static ssh_session
glusterBlockSSHConnect(const char *host, const char *user, int verbosity)
{
  int auth = 0;
  ssh_session blksession;

  blksession = ssh_new();
  if (!blksession)
    return NULL;

  if (user) {
    if (ssh_options_set(blksession, SSH_OPTIONS_USER, user) < 0)
      goto sfree;
  }

  if (ssh_options_set(blksession, SSH_OPTIONS_HOST, host) < 0)
    goto sfree;

  ssh_options_set(blksession, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
  if (ssh_connect(blksession)) {
    ERROR("Connection failed : %s", ssh_get_error(blksession));
    goto sdnt;
  }

  if (glusterBlockSSHVerifyKnownHost(blksession)<0)
    goto sdnt;

  auth = glusterBlockSSHAuthConsole(blksession);
  if (auth == SSH_AUTH_SUCCESS) {
    return blksession;
  } else if (auth == SSH_AUTH_DENIED) {
    ERROR("%s", "Authentication failed");
  } else {
    ERROR("while authenticating : %s", ssh_get_error(blksession));
  }

 sdnt:
  ssh_disconnect(blksession);

 sfree:
  ssh_free(blksession);

  return NULL;
}


char *
glusterBlockSSHRun(char *host, char *cmd, bool console)
{
  FILE *fd = NULL;
  int ret;
  int nbytes;
  int rc;
  uuid_t out;
  char uuid[256];
  char *file;
  char buffer[256];
  ssh_session blksession;
  ssh_channel blkchannel;

  blksession = glusterBlockSSHConnect(host, NULL/*user*/, 0);
  if (!blksession) {
    ssh_finalize();
    return NULL;
  }

  blkchannel = ssh_channel_new(blksession);
  if (!blkchannel) {
    ret = 1;
    goto chfail;
  }

  rc = ssh_channel_open_session(blkchannel);
  if (rc < 0) {
    ret = 1;
    goto fail;
  }

  rc = ssh_channel_request_exec(blkchannel, cmd);
  if (rc < 0) {
    ret = 1;
    goto fail;
  }

  if (!console) {
    uuid_generate(out);
    uuid_unparse(out, uuid);
    asprintf(&file, "/tmp/%s", uuid);
    fd = fopen(file, "w");
  }

  nbytes = ssh_channel_read(blkchannel, buffer, sizeof(buffer), 0);
  while (nbytes > 0) {
    if (fwrite(buffer, 1, nbytes, fd ? fd : stdout) != (unsigned int) nbytes) {
      ret = 1;
      goto fail;
    }
    nbytes = ssh_channel_read(blkchannel, buffer, sizeof(buffer), 0);
  }

  if (nbytes < 0) {
    ret = 1;
    goto fail;
  }

  ssh_channel_send_eof(blkchannel);
  ret = 0;

  if (console && ret == 0)
    file = "stdout";  /* just to differentiate b/w success and failure */

 fail:
  if (!console)
     fclose(fd);

  ssh_channel_close(blkchannel);
  ssh_channel_free(blkchannel);

 chfail:
  ssh_disconnect(blksession);
  ssh_free(blksession);
  ssh_finalize();

  return (!ret) ? file : NULL;
}
