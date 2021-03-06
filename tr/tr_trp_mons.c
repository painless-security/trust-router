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

/* Monitoring handlers for trust router TID server */

#include <jansson.h>
#include <trp_internal.h>
#include <tr_trp.h>
#include <trp_rtable.h>
#include <trp_ptable.h>
#include <tr_comm.h>
#include <tr_idp.h>
#include <mon_internal.h>
#include <mons_handlers.h>

static MON_RC handle_show_routes(void *cookie, json_t **response_ptr)
{
  TRPS_INSTANCE *trps = talloc_get_type_abort(cookie, TRPS_INSTANCE);

  *response_ptr = trp_rtable_to_json(trps->rtable);
  return (*response_ptr == NULL) ? MON_NOMEM : MON_SUCCESS;
}

static MON_RC handle_show_peers(void *cookie, json_t **response_ptr)
{
  TRPS_INSTANCE *trps = talloc_get_type_abort(cookie, TRPS_INSTANCE);

  *response_ptr = trp_ptable_to_json(trps->ptable);
  return (*response_ptr == NULL) ? MON_NOMEM : MON_SUCCESS;
}

static MON_RC handle_show_communities(void *cookie, json_t **response_ptr)
{
  TRPS_INSTANCE *trps = talloc_get_type_abort(cookie, TRPS_INSTANCE);

  *response_ptr = tr_comm_table_to_json(trps->ctable);
  return (*response_ptr == NULL) ? MON_NOMEM : MON_SUCCESS;
}

static MON_RC handle_show_realms(void *cookie, json_t **response_ptr)
{
  TRPS_INSTANCE *trps = talloc_get_type_abort(cookie, TRPS_INSTANCE);

  *response_ptr = tr_idp_realms_to_json(trps->ctable->idp_realms);
  return (*response_ptr == NULL) ? MON_NOMEM : MON_SUCCESS;
}

void tr_trp_register_mons_handlers(TRPS_INSTANCE *trps, MONS_INSTANCE *mons)
{
  mons_register_handler(mons,
                        MON_CMD_SHOW, OPT_TYPE_SHOW_ROUTES,
                        handle_show_routes, trps);
  mons_register_handler(mons,
                        MON_CMD_SHOW, OPT_TYPE_SHOW_PEERS,
                        handle_show_peers, trps);
  mons_register_handler(mons,
                        MON_CMD_SHOW, OPT_TYPE_SHOW_COMMUNITIES,
                        handle_show_communities, trps);
  mons_register_handler(mons,
                        MON_CMD_SHOW, OPT_TYPE_SHOW_REALMS,
                        handle_show_realms, trps);
}
