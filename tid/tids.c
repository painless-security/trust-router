/*
 * Copyright (c) 2012, 2015, JANET(UK)
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

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <jansson.h>
#include <talloc.h>
#include <poll.h>
#include <tid_internal.h>
#include <gsscon.h>
#include <tr_debug.h>
#include <tr_msg.h>
#include <tr_socket.h>
#include <tr_gss.h>
#include <tr_event.h>
#include <sys/resource.h>

/**
 * Create a response with minimal fields filled in
 *
 * @param mem_ctx talloc context for the return value
 * @param req request to respond to
 * @return new response structure allocated in the mem_ctx context
 */
static TID_RESP *tids_create_response(TALLOC_CTX *mem_ctx, TID_REQ *req)
{
  TID_RESP *resp=NULL;
  int success=0;

  if (NULL == (resp = tid_resp_new(mem_ctx))) {
    tr_crit("tids_create_response: Error allocating response structure.");
    return NULL;
  }
  
  resp->result = TID_SUCCESS; /* presume success */
  if ((NULL == (resp->rp_realm = tr_dup_name(req->rp_realm))) ||
      (NULL == (resp->realm = tr_dup_name(req->realm))) ||
      (NULL == (resp->comm = tr_dup_name(req->comm)))) {
    tr_crit("tids_create_response: Error allocating fields in response.");
    goto cleanup;
  }
  if (req->orig_coi) {
    if (NULL == (resp->orig_coi = tr_dup_name(req->orig_coi))) {
      tr_crit("tids_create_response: Error allocating fields in response.");
      goto cleanup;
    }
  }
  if (req->request_id) {
    if (NULL == (resp->request_id = tr_dup_name(req->request_id))) {
      tr_crit("tids_create_response: Error allocating fields in response.");
      goto cleanup;
    }
  }

  success=1;

cleanup:
  if ((!success) && (resp!=NULL)) {
    talloc_free(resp);
    resp=NULL;
  }
  return resp;
}

static int tids_handle_request(TIDS_INSTANCE *tids, TID_REQ *req, TID_RESP *resp)
{
  int rc=-1;

  /* Check that this is a valid TID Request.  If not, send an error return. */
  if ((!req) ||
      (!(req->rp_realm)) ||
      (!(req->realm)) ||
      (!(req->comm))) {
    tr_notice("tids_handle_request(): Not a valid TID Request.");
    tid_resp_set_result(resp, TID_ERROR);
    tid_resp_set_err_msg(resp, tr_new_name("Bad request format"));
    return -1;
  }

  tr_debug("tids_handle_request: adding self to req path.");
  tid_req_add_path(req, tids->hostname, tids->tids_port);
  
  /* Call the caller's request handler */
  /* TBD -- Handle different error returns/msgs */
  if (0 > (rc = (*tids->req_handler)(tids, req, resp, tids->cookie))) {
    /* set-up an error response */
    tr_debug("tids_handle_request: req_handler returned error.");
    tid_resp_set_result(resp, TID_ERROR);
    if (!tid_resp_get_err_msg(resp))	/* Use msg set by handler, if any */
      tid_resp_set_err_msg(resp, tr_new_name("Internal processing error"));
  } else {
    /* set-up a success response */
    tr_debug("tids_handle_request: req_handler returned success.");
    tid_resp_set_result(resp, TID_SUCCESS);
    resp->err_msg = NULL;	/* No error msg on successful return */
  }
    
  return rc;
}

/**
 * Produces a JSON-encoded msg containing the TID response
 *
 * @param mem_ctx talloc context for the return value
 * @param resp outgoing response
 * @return JSON-encoded message containing the TID response
 */
static char *tids_encode_response(TALLOC_CTX *mem_ctx, TID_RESP *resp)
{
  TR_MSG mresp;
  char *resp_buf = NULL;

  /* Construct the response message */
  mresp.msg_type = TID_RESPONSE;
  tr_msg_set_resp(&mresp, resp);

  /* Encode the message to JSON */
  resp_buf = tr_msg_encode(mem_ctx, &mresp);
  if (resp_buf == NULL) {
    tr_err("tids_encode_response: Error encoding json response.");
    return NULL;
  }
  tr_debug("tids_encode_response: Encoded response: %s", resp_buf);

  /* Success */
  return resp_buf;
}

/**
 * Encode/send an error response
 *
 * Part of the public interface
 *
 * @param tids
 * @param req
 * @param err_msg
 * @return
 */
int tids_send_err_response (TIDS_INSTANCE *tids, TID_REQ *req, const char *err_msg) {
  TID_RESP *resp = NULL;
  int rc = 0;

  if ((!tids) || (!req) || (!err_msg)) {
    tr_debug("tids_send_err_response: Invalid parameters.");
    return -1;
  }

  /* If we already sent a response, don't send another no matter what. */
  if (req->resp_sent)
    return 0;

  if (NULL == (resp = tids_create_response(req, req))) {
    tr_crit("tids_send_err_response: Can't create response.");
    return -1;
  }

  /* mark this as an error response, and include the error message */
  resp->result = TID_ERROR;
  resp->err_msg = tr_new_name((char *)err_msg);
  resp->error_path = req->path;

  rc = tids_send_response(tids, req, resp);

  tid_resp_free(resp);
  return rc;
}

/**
 * Encode/send a response
 *
 * Part of the public interface
 *
 * @param tids not actually used, but kept for ABI compatibility
 * @param req
 * @param resp
 * @return
 */
int tids_send_response (TIDS_INSTANCE *tids, TID_REQ *req, TID_RESP *resp)
{
  int err;
  char *resp_buf;

  if ((!tids) || (!req) || (!resp)) {
    tr_debug("tids_send_response: Invalid parameters.");
    return -1;
  }

  /* Never send a second response if we already sent one. */
  if (req->resp_sent)
    return 0;

  resp_buf = tids_encode_response(NULL, NULL);
  if (resp_buf == NULL) {
    tr_err("tids_send_response: Error encoding json response.");
    tr_audit_req(req);
    return -1;
  }

  tr_debug("tids_send_response: Encoded response: %s", resp_buf);

  /* If external logging is enabled, fire off a message */
  /* TODO Can be moved to end once segfault in gsscon_write_encrypted_token fixed */
  tr_audit_resp(resp);

  /* Send the response over the connection */
  err = gsscon_write_encrypted_token (req->conn, req->gssctx, resp_buf,
                                            strlen(resp_buf) + 1);
  if (err) {
    tr_notice("tids_send_response: Error sending response over connection.");
    tr_audit_req(req);
    return -1;
  }

  /* indicate that a response has been sent for this request */
  req->resp_sent = 1;

  free(resp_buf);

  return 0;
}

/**
 * Callback to process a request and produce a response
 *
 * @param req_str JSON-encoded request
 * @param data pointer to a TIDS_INSTANCE
 * @return pointer to the response string or null to send no response
 */
static TR_GSS_RC tids_req_cb(TALLOC_CTX *mem_ctx, TR_MSG *mreq, TR_MSG **mresp, void *data)
{
  TALLOC_CTX *tmp_ctx = talloc_new(NULL);
  TIDS_INSTANCE *tids = talloc_get_type_abort(data, TIDS_INSTANCE);
  TID_REQ *req = NULL;
  TID_RESP *resp = NULL;
  TR_GSS_RC rc = TR_GSS_ERROR;

  /* If this isn't a TID Request, just drop it. */
  if (mreq->msg_type != TID_REQUEST) {
    tr_debug("tids_req_cb: Not a TID request, dropped.");
    rc = TR_GSS_INTERNAL_ERROR;
    goto cleanup;
  }

  /* Get a handle on the request itself. Don't free req - it belongs to mreq */
  req = tr_msg_get_req(mreq);

  /* Allocate a response message */
  *mresp = talloc(tmp_ctx, TR_MSG);
  if (*mresp == NULL) {
    /* We cannot create a response message, so all we can really do is emit
     * an error message and return. */
    tr_crit("tids_req_cb: Error allocating response message.");
    rc = TR_GSS_INTERNAL_ERROR;
    goto cleanup;
  }

  /* Allocate a response structure and populate common fields. Put it in the
   * response message's talloc context. */
  resp = tids_create_response(mresp, req);
  if (resp == NULL) {
    /* If we were unable to create a response, we cannot reply. Log an
     * error if we can, then drop the request. */
    tr_crit("tids_req_cb: Error creating response structure.");
    *mresp = NULL; /* the contents are in tmp_ctx, so they will still be cleaned up */
    rc = TR_GSS_INTERNAL_ERROR;
    goto cleanup;
  }
  /* Now officially assign the response to the message. */
  tr_msg_set_resp(*mresp, resp);

  /* Handle the request and fill in resp */
  if (tids_handle_request(tids, req, resp) >= 0)
    rc = TR_GSS_SUCCESS;
  else {
    /* The TID request was an error response */
    tr_debug("tids_req_cb: Error from tids_handle_request");
    rc = TR_GSS_REQUEST_FAILED;
    /* Fall through, to send the response, either way */
  }

  /* put the response message in the caller's context */
  talloc_steal(mem_ctx, *mresp);

cleanup:
  talloc_free(tmp_ctx);
  return rc;
}

static int tids_destructor(void *object)
{
  TIDS_INSTANCE *tids = talloc_get_type_abort(object, TIDS_INSTANCE);
  if (tids->pids)
    g_array_unref(tids->pids);
  return 0;
}

TIDS_INSTANCE *tids_new(TALLOC_CTX *mem_ctx)
{
  TIDS_INSTANCE *tids = talloc_zero(mem_ctx, TIDS_INSTANCE);
  if (tids) {
    tids->pids = g_array_new(FALSE, FALSE, sizeof(struct tid_process));
    if (tids->pids == NULL) {
      talloc_free(tids);
      return NULL;
    }
    talloc_set_destructor((void *)tids, tids_destructor);
  }
  return tids;
}

/**
 * Create a new TIDS instance
 *
 * Deprecated: exists for ABI compatibility, but tids_new() should be used instead
 *
 */
TIDS_INSTANCE *tids_create(void)
{
  return tids_new(NULL);
}

/* Get a listener for tids requests, returns its socket fd. Accept
 * connections with tids_accept() */
nfds_t tids_get_listener(TIDS_INSTANCE *tids,
                         TIDS_REQ_FUNC *req_handler,
                         tids_auth_func *auth_handler,
                         const char *hostname,
                         int port,
                         void *cookie,
                         int *fd_out,
                         size_t max_fd)
{
  nfds_t n_fd = 0;
  nfds_t ii = 0;

  tids->tids_port = port;
  n_fd = tr_sock_listen_all(port, fd_out, max_fd);

  if (n_fd == 0)
    tr_err("tids_get_listener: Error opening port %d", port);
  else {
    /* opening port succeeded */
    tr_info("tids_get_listener: Opened port %d.", port);
    
    /* make this socket non-blocking */
    for (ii=0; ii<n_fd; ii++) {
      if (0 != fcntl(fd_out[ii], F_SETFL, O_NONBLOCK)) {
        tr_err("tids_get_listener: Error setting O_NONBLOCK.");
        for (ii=0; ii<n_fd; ii++) {
          close(fd_out[ii]);
          fd_out[ii]=-1;
        }
        n_fd = 0;
        break;
      }
    }
  }

  if (n_fd > 0) {
    /* store the caller's request handler & cookie */
    tids->req_handler = req_handler;
    tids->auth_handler = auth_handler;
    tids->hostname = hostname;
    tids->cookie = cookie;
  }

  return (int)n_fd;
}

/* Strings used to report results from the handler process. The
 * TIDS_MAX_MESSAGE_LEN must be longer than the longest message, including
 * null termination (i.e., strlen() + 1) */
#define TIDS_MAX_MESSAGE_LEN (10)
#define TIDS_SUCCESS_MESSAGE "OK" /* a success message was sent */
#define TIDS_ERROR_MESSAGE   "ERR" /* an error message was sent */
#define TIDS_REQ_FAIL_MESSAGE "FAIL" /* sending failed */

/**
 * Process to handle an incoming TIDS request
 *
 * This should be run in the child process after a fork(). Handles
 * the request, writes the result to result_fd, and terminates.
 * Never returns to the caller.
 *
 * @param tids TID server instance
 * @param conn_fd file descriptor for the incoming connection
 * @param result_fd writable file descriptor for the result, or 0 to disable reporting
 */
static void tids_handle_proc(TIDS_INSTANCE *tids, int conn_fd, int result_fd)
{
  const char *response_message = NULL;
  struct rlimit rlim; /* for disabling core dump */

  switch(tr_gss_handle_connection(conn_fd,
                                  "trustidentity", tids->hostname, /* acceptor name */
                                  tids->auth_handler, tids->cookie, /* auth callback and cookie */
                                  tids_req_cb, tids /* req callback and cookie */
  )) {
    case TR_GSS_SUCCESS:
      response_message = TIDS_SUCCESS_MESSAGE;
      break;

    case TR_GSS_REQUEST_FAILED:
      response_message = TIDS_ERROR_MESSAGE;
      break;

    case TR_GSS_INTERNAL_ERROR:
    case TR_GSS_ERROR:
    default:
      response_message = TIDS_REQ_FAIL_MESSAGE;
      break;
  }

  if (0 != result_fd) {
    /* write strlen + 1 to include the null termination */
    if (write(result_fd, response_message, strlen(response_message) + 1) < 0)
      tr_err("tids_accept: child process unable to write to pipe");
  }

  close(result_fd);
  close(conn_fd);

  /* This ought to be an exit(0), but log4shib does not play well with fork() due to
   * threading issues. To ensure we do not get stuck in the exit handler, we will
   * abort. First disable core dump for this subprocess (the main process will still
   * dump core if the environment allows). */
  rlim.rlim_cur = 0; /* max core size of 0 */
  rlim.rlim_max = 0; /* prevent the core size limit from being raised later */
  setrlimit(RLIMIT_CORE, &rlim);
  abort(); /* exit hard */
}

/* Accept and process a connection on a port opened with tids_get_listener() */
int tids_accept(TIDS_INSTANCE *tids, int listen)
{
  int conn=-1;
  int pid=-1;
  int pipe_fd[2];
  struct tid_process tp = {0};

  if (0 > (conn = tr_sock_accept(listen))) {
    tr_debug("tids_accept: Error accepting connection");
    return 1;
  }

  if (0 > pipe(pipe_fd)) {
    perror("Error on pipe()");
    return 1;
  }
  /* pipe_fd[0] is for reading, pipe_fd[1] is for writing */

  if (0 > (pid = fork())) {
    perror("Error on fork()");
    return 1;
  }

  if (pid == 0) {
    /* Only the child process gets here */
    close(pipe_fd[0]); /* close the read end of the pipe, the child only writes */
    close(listen); /* close the child process's handle on the listen port */

    tids_handle_proc(tids, conn, pipe_fd[1]); /* never returns */
  }

  /* Only the parent process gets here */
  close(pipe_fd[1]); /* close the write end of the pipe, the parent only listens */
  close(conn); /* connection belongs to the child, so close parent's handle */

  /* remember the PID of our child process */
  tr_info("tids_accept: Spawned TID process %d to handle incoming connection.", pid);
  tp.pid = pid;
  tp.read_fd = pipe_fd[0];
  g_array_append_val(tids->pids, tp);

  /* clean up any processes that have completed */
  tids_sweep_procs(tids);
  return 0;
}

/**
 * Clean up any finished TID request processes
 *
 * This is called by the main process after forking each TID request. If you want to be
 * sure finished processes are cleaned up promptly even during a lull in TID requests,
 * this can be called from the main thread of the main process. It is not thread-safe,
 * so should not be used from sub-threads. It should not be called by child processes -
 * this would probably be harmless but ineffective.
 *
 * @param tids
 */
void tids_sweep_procs(TIDS_INSTANCE *tids)
{
  guint ii;
  struct tid_process tp = {0};
  char result[TIDS_MAX_MESSAGE_LEN] = {0};
  ssize_t result_len;
  int status;
  int wait_rc;

  /* loop backwards over the array so we can remove elements as we go */
  for (ii=tids->pids->len; ii > 0; ii--) {
    /* ii-1 is the current index - get our own copy, we may destroy the list's copy */
    tp = g_array_index(tids->pids, struct tid_process, ii-1);

    wait_rc = waitpid(tp.pid, &status, WNOHANG);
    if (wait_rc == 0)
      continue; /* process still running */

    if (wait_rc < 0) {
      /* invalid options will probably keep being invalid, report that condition */
      if(errno == EINVAL)
        tr_crit("tids_sweep_procs: waitpid called with invalid options");

      /* If we got ECHILD, that means the PID was invalid; we'll assume the process was
       * terminated and we missed it. For all other errors, move on
       * to the next PID to check. */
      if (errno != ECHILD)
        continue;

      tr_warning("tid_sweep_procs: TID process %d disappeared", tp.pid);
    }

    /* remove the item (we still have a copy of the data) */
    g_array_remove_index_fast(tids->pids, ii-1); /* disturbs only indices >= ii-1 which we've already handled */

    /* Report exit status unless we got ECHILD above or somehow waitpid returned the wrong pid */
    if (wait_rc == tp.pid) {
      if (WIFEXITED(status)) {
        tr_debug("tids_sweep_procs: TID process %d exited with status %d.", tp.pid, WTERMSIG(status));
      } else if (WIFSIGNALED(status)) {
        tr_debug("tids_sweep_procs: TID process %d terminated by signal %d.", tp.pid, WTERMSIG(status));
      }
    } else if (wait_rc > 0) {
      tr_err("tids_sweep_procs: waitpid returned pid %d, expected %d", wait_rc, tp.pid);
    }

    /* read the pipe - if the TID request worked, it will have written status before terminating */
    result_len = read(tp.read_fd, result, TIDS_MAX_MESSAGE_LEN);
    close(tp.read_fd);

    if ((result_len > 0) && (strcmp(result, TIDS_SUCCESS_MESSAGE) == 0)) {
      tids->req_count++;
      tr_info("tids_sweep_procs: TID process %d exited after successful request.", tp.pid);
    } else if ((result_len > 0) && (strcmp(result, TIDS_ERROR_MESSAGE) == 0)) {
      tids->req_error_count++;
      tr_info("tids_sweep_procs: TID process %d exited after unsuccessful request.", tp.pid);
    } else {
      tids->error_count++;
      tr_info("tids_sweep_procs: TID process %d exited with an error.", tp.pid);
    }
  }
}

/* Process tids requests forever. Should not return except on error. */
int tids_start(TIDS_INSTANCE *tids,
               TIDS_REQ_FUNC *req_handler,
               tids_auth_func *auth_handler,
               const char *hostname,
               int port,
               void *cookie)
{
  int fd[TR_MAX_SOCKETS]={0};
  nfds_t n_fd=0;
  struct pollfd poll_fd[TR_MAX_SOCKETS]={{0}};
  int ii=0;

  n_fd = tids_get_listener(tids, req_handler, auth_handler, hostname, port, cookie, fd, TR_MAX_SOCKETS);
  if (n_fd <= 0) {
    perror ("Error from tids_listen()");
    return 1;
  }

  tr_info("Trust Path Query Server starting on host %s:%d.", hostname, port);

  /* set up the poll structs */
  for (ii=0; ii<n_fd; ii++) {
    poll_fd[ii].fd=fd[ii];
    poll_fd[ii].events=POLLIN;
  }

  while(1) {	/* accept incoming conns until we are stopped */
    /* clear out events from previous iteration */
    for (ii=0; ii<n_fd; ii++)
      poll_fd[ii].revents=0;

    /* wait indefinitely for a connection */
    if (poll(poll_fd, n_fd, -1) < 0) {
      perror("Error from poll()");
      return 1;
    }

    /* fork handlers for any sockets that have data */
    for (ii=0; ii<n_fd; ii++) {
      if (poll_fd[ii].revents == 0)
        continue;

      if ((poll_fd[ii].revents & POLLERR) || (poll_fd[ii].revents & POLLNVAL)) {
        perror("Error polling fd");
        continue;
      }

      if (poll_fd[ii].revents & POLLIN) {
        if (tids_accept(tids, poll_fd[ii].fd))
          tr_debug("tids_start: error in tids_accept().");
      }
    }
  }

  return 1;	/* should never get here, loops "forever" */
}

void tids_destroy (TIDS_INSTANCE *tids)
{
  /* clean up logfiles */
  tr_log_close();

  if (tids)
    free(tids);
}
