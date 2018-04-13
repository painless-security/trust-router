/*
 * Copyright (c) 2018, JANET(UK)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of JANET(UK) nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <talloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>

#include <tr.h>
#include <tr_debug.h>
#include <mon_internal.h>
#include <tr_socket.h>
#include <sys/wait.h>
#include <tr_gss.h>

/**
 * Allocate a new MONS_INSTANCE
 *
 * @param mem_ctx talloc context for allocation
 * @return new MONS_INSTANCE or null on failure
 */
MONS_INSTANCE *mons_new(TALLOC_CTX *mem_ctx)
{
  MONS_INSTANCE *mons = talloc(mem_ctx, MONS_INSTANCE);

  if (mons) {
    mons->hostname = NULL;
    mons->port = 0;
    mons->tids = NULL;
    mons->trps = NULL;
    mons->req_handler = NULL;
    mons->auth_handler = NULL;
    mons->cookie = NULL;
    mons->authorized_gss_names = tr_gss_names_new(mons);
    if (mons->authorized_gss_names == NULL) {
      talloc_free(mons);
      mons = NULL;
    }
  }
  return mons;
}

/**
 * Callback to process a request and produce a response
 *
 * @param req_str JSON-encoded request
 * @param data pointer to a MONS_INSTANCE
 * @return pointer to the response string or null to send no response
 */
static char *mons_req_cb(TALLOC_CTX *mem_ctx, const char *req_str, void *data)
{
  return "This is a response.";
}

/**
 * Create a listener for monitoring requests
 *
 * Accept connections with mons_accept()
 *
 * @param mons monitoring server instance
 * @param req_handler
 * @param auth_handler
 * @param hostname
 * @param port
 * @param cookie
 * @param fd_out
 * @param max_fd
 * @return
 */
int mons_get_listener(MONS_INSTANCE *mons,
                      MONS_REQ_FUNC *req_handler,
                      MONS_AUTH_FUNC *auth_handler,
                      unsigned int port,
                      void *cookie,
                      int *fd_out,
                      size_t max_fd)
{
  size_t n_fd=0;
  size_t ii=0;

  mons->port = port;
  n_fd = tr_sock_listen_all(port, fd_out, max_fd);
  if (n_fd<=0)
    tr_err("mons_get_listener: Error opening port %d");
  else {
    /* opening port succeeded */
    tr_info("mons_get_listener: Opened port %d.", port);

    /* make this socket non-blocking */
    for (ii=0; ii<n_fd; ii++) {
      if (0 != fcntl(fd_out[ii], F_SETFL, O_NONBLOCK)) {
        tr_err("mons_get_listener: Error setting O_NONBLOCK.");
        for (ii=0; ii<n_fd; ii++) {
          close(fd_out[ii]);
          fd_out[ii]=-1;
        }
        n_fd=0;
        break;
      }
    }
  }

  if (n_fd>0) {
    /* store the caller's request handler & cookie */
    mons->req_handler = req_handler;
    mons->auth_handler = auth_handler;
    mons->cookie = cookie;
  }

  return (int) n_fd;
}

/**
 * Accept and process a connection on a port opened with mons_get_listener()
 *
 * @param mons monitoring interface instance
 * @param listen FD of the connection socket
 * @return 0 on success
 */
int mons_accept(MONS_INSTANCE *mons, int listen)
{
  int conn=-1;
  int pid=-1;

  if (0 > (conn = accept(listen, NULL, NULL))) {
    perror("Error from monitoring interface accept()");
    return 1;
  }

  if (0 > (pid = fork())) {
    perror("Error on fork()");
    return 1;
  }

  if (pid == 0) {
    close(listen);
    tr_gss_handle_connection(conn,
                             "trustmonitor", mons->hostname, /* acceptor name */
                             mons->auth_handler, mons->cookie, /* auth callback and cookie */
                             mons_req_cb, mons /* req callback and cookie */
    );
    close(conn);
    exit(0); /* exit to kill forked child process */
  } else {
    close(conn);
  }

  /* clean up any processes that have completed */
  while (waitpid(-1, 0, WNOHANG) > 0);

  return 0;
}