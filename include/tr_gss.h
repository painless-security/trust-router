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

#ifndef TRUST_ROUTER_TR_GSS_H
#define TRUST_ROUTER_TR_GSS_H

#include <tr_msg.h>

typedef int (TR_GSS_AUTH_FN)(gss_name_t, TR_NAME *, void *);
typedef enum tr_gss_rc (TR_GSS_HANDLE_REQ_FN)(TALLOC_CTX *, TR_MSG *, TR_MSG **, void *);

typedef enum tr_gss_rc {
  TR_GSS_SUCCESS = 0, /* success */
  TR_GSS_AUTH_FAILED, /* authorization failed */
  TR_GSS_REQUEST_FAILED, /* request failed */
  TR_GSS_INTERNAL_ERROR, /* internal error (memory allocation, etc) */
  TR_GSS_ERROR,       /* unspecified error */
} TR_GSS_RC;

TR_GSS_RC tr_gss_handle_connection(int conn,
                                   const char *acceptor_service,
                                   const char *acceptor_hostname,
                                   TR_GSS_AUTH_FN auth_cb,
                                   void *auth_cookie,
                                   TR_GSS_HANDLE_REQ_FN req_cb,
                                   void *req_cookie);

#endif //TRUST_ROUTER_TR_GSS_H
