/*
 * Copyright (c) 2012-2018, JANET(UK)
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
#include <stdio.h>
#include <talloc.h>
#include <argp.h>
#include <unistd.h>

#include <mon_internal.h>
#include <gsscon.h>
#include <tr_debug.h>
#include <trust_router/tr_dh.h>


/* command-line option setup */

/* argp global parameters */
const char *argp_program_bug_address=PACKAGE_BUGREPORT; /* bug reporting address */

/* doc strings */
static const char doc[]=PACKAGE_NAME " - TR Monitoring Client";
static const char arg_doc[]="<message> <server> [<port>]"; /* string describing arguments, if any */

/* define the options here. Fields are:
 * { long-name, short-name, variable name, options, help description } */
static const struct argp_option cmdline_options[] = {
  { "repeat", 'r', "N", OPTION_ARG_OPTIONAL, "Repeat message until terminated, or N times." },
  {NULL}
};

/* structure for communicating with option parser */
struct cmdline_args {
  char *msg;
  char *server;
  unsigned int port; /* optional */
  int repeat; /* how many times to repeat, or -1 for infinite */
};

/* parser for individual options - fills in a struct cmdline_args */
static error_t parse_option(int key, char *arg, struct argp_state *state)
{
  long tmp_l = 0;

  /* get a shorthand to the command line argument structure, part of state */
  struct cmdline_args *arguments=state->input;

  switch (key) {
    case 'r':
      if (arg==NULL)
        arguments->repeat=-1;
      else
        tmp_l = strtol(arg, NULL, 10);
      if ((errno == 0) && (tmp_l > 0) && (tmp_l < INT_MAX))
        arguments->repeat = (int) tmp_l;
      else
        argp_usage(state);
      break;

    case ARGP_KEY_ARG: /* handle argument (not option) */
      switch (state->arg_num) {
        case 0:
          arguments->msg=arg;
          break;

        case 1:
          arguments->server=arg;
          break;

        case 2:
          tmp_l = strtol(arg, NULL, 10);
          if (errno || (tmp_l < 0) || (tmp_l > 65535)) /* max valid port */
            argp_usage(state);

          arguments->port=(unsigned int) tmp_l;
          break;

        default:
          /* too many arguments */
          argp_usage(state);
      }
      break;

    case ARGP_KEY_END: /* no more arguments */
      if (state->arg_num < 2) {
        /* not enough arguments encountered */
        argp_usage(state);
      }
      break;

    default:
      return ARGP_ERR_UNKNOWN;
  }

  return 0; /* success */
}


/* assemble the argp parser */
static struct argp argp = {cmdline_options, parse_option, arg_doc, doc};

int main(int argc, char *argv[])
{
  TALLOC_CTX *main_ctx=talloc_new(NULL);
  MONC_INSTANCE *monc = NULL;
  MON_REQ *req = NULL;
  MON_RESP *resp = NULL;

  struct cmdline_args opts;
  int retval=1; /* exit with an error status unless this gets set to zero */

  /* parse the command line*/
  /* set defaults */
  opts.msg=NULL;
  opts.server=NULL;
  opts.port=TRP_PORT;
  opts.repeat=1;

  argp_parse(&argp, argc, argv, 0, 0, &opts);

  /* Use standalone logging */
  tr_log_open();

  /* set logging levels */
  talloc_set_log_stderr();
  tr_log_threshold(LOG_CRIT);
  tr_console_threshold(LOG_DEBUG);

  printf("TR Monitor:\nServer = %s, port = %i\n", opts.server, opts.port);

  /* Create a MON client instance & the client DH */
  monc = monc_new(main_ctx);
  if (monc == NULL) {
    printf("Error allocating client instance.\n");
    goto cleanup;
  }


  /* fill in the DH parameters */
  monc_set_dh(monc, tr_create_dh_params(NULL, 0));
  if (monc_get_dh(monc) == NULL) {
    printf("Error creating client DH params.\n");
    goto cleanup;
  }

  /* Set-up MON connection */
  if (0 != monc_open_connection(monc, opts.server, opts.port)) {
    /* Handle error */
    printf("Error opening connection to %s:%d.\n", opts.server, opts.port);
    goto cleanup;
  };

  req = mon_req_new(main_ctx, MON_CMD_SHOW);

  /* Send a MON request and get the response */
  resp = monc_send_request(main_ctx, monc, req);

  if (resp == NULL) {
    /* Handle error */
    printf("Error executing monitoring request.\n");
    goto cleanup;
  }

  /* success */
  retval = 0;

  /* Clean-up the MON client instance, and exit */
cleanup:
  talloc_free(main_ctx);
  return retval;
}
