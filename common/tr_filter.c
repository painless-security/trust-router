/*
 * Copyright (c) 2012, 2013, JANET(UK)
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <talloc.h>
#include <assert.h>

#include <tr_filter.h>
#include <trp_internal.h>
#include <tid_internal.h>
#include <tr_debug.h>

/* Function types for handling filter fields generally. All target values
 * are represented as strings in a TR_NAME.
 */

/* CMP functions return values like strcmp: 0 on match, <0 on target<val, >0 on target>val */
typedef int (*TR_FILTER_FIELD_CMP)(TR_FILTER_TARGET *target, TR_NAME *val);
/* get functions return TR_NAME format of the field value. Caller must free it. */
typedef TR_NAME *(*TR_FILTER_FIELD_GET)(TR_FILTER_TARGET *target);

static TR_FILTER_TARGET *tr_filter_target_new(TALLOC_CTX *mem_ctx)
{
  TR_FILTER_TARGET *target=talloc(mem_ctx, TR_FILTER_TARGET);
  if (target) {
    target->trp_inforec=NULL;
    target->trp_upd=NULL;
    target->tid_req=NULL;
  }
  return target;
}
void tr_filter_target_free(TR_FILTER_TARGET *target)
{
  talloc_free(target);
}

/**
 * Create a filter target for a TID request. Does not change the context of the request,
 * so this is only valid until that is freed.
 *
 * @param mem_ctx talloc context for the object
 * @param req TID request object
 * @return pointer to a TR_FILTER_TARGET structure, or null on allocation failure
 */
TR_FILTER_TARGET *tr_filter_target_tid_req(TALLOC_CTX *mem_ctx, TID_REQ *req)
{
  TR_FILTER_TARGET *target=tr_filter_target_new(mem_ctx);
  if (target)
    target->tid_req=req; /* borrowed, not adding to our context */
  return target;
}

/**
 * Create a filter target for a TRP inforec. Does not change the context of the inforec or duplicate TR_NAMEs,
 * so this is only valid until those are freed.
 *
 * @param mem_ctx talloc context for the object
 * @param upd Update containing the TRP inforec
 * @param inforec TRP inforec
 * @return pointer to a TR_FILTER_TARGET structure, or null on allocation failure
 */
TR_FILTER_TARGET *tr_filter_target_trp_inforec(TALLOC_CTX *mem_ctx, TRP_UPD *upd, TRP_INFOREC *inforec)
{
  TR_FILTER_TARGET *target=tr_filter_target_new(mem_ctx);
  if (target) {
    target->trp_inforec = inforec; /* borrowed, not adding to our context */
    target->trp_upd=upd;
  }
  return target;
}

/** Handler functions for TID RP_REALM field */
static int tr_ff_cmp_tid_rp_realm(TR_FILTER_TARGET *target, TR_NAME *val)
{
  return tr_name_cmp(tid_req_get_rp_realm(target->tid_req), val);
}

static TR_NAME *tr_ff_get_tid_rp_realm(TR_FILTER_TARGET *target)
{
  return tr_dup_name(tid_req_get_rp_realm(target->tid_req));
}

/** Handler functions for TRP info_type field */
static int tr_ff_cmp_trp_info_type(TR_FILTER_TARGET *target, TR_NAME *val)
{
  TRP_INFOREC *inforec=target->trp_inforec;
  char *valstr=NULL;
  int val_type=0;

  assert(val);
  assert(inforec);

  /* nothing matches unknown */
  if (inforec->type==TRP_INFOREC_TYPE_UNKNOWN)
    return 0;

  valstr = tr_name_strdup(val); /* get this as an official null-terminated string */
  val_type = trp_inforec_type_from_string(valstr);
  free(valstr);

  /* we do not define an ordering of info types */
  return (val_type==inforec->type);
}

static TR_NAME *tr_ff_get_trp_info_type(TR_FILTER_TARGET *target)
{
  TRP_INFOREC *inforec=target->trp_inforec;
  return tr_new_name(trp_inforec_type_to_string(inforec->type));
}

/** Handlers for TRP realm field */
static int tr_ff_cmp_trp_realm(TR_FILTER_TARGET *target, TR_NAME *val)
{
  return tr_name_cmp(trp_upd_get_realm(target->trp_upd), val);
}

static TR_NAME *tr_ff_get_trp_realm(TR_FILTER_TARGET *target)
{
  return tr_dup_name(trp_upd_get_realm(target->trp_upd));
}

/** Handlers for TID realm field */
static int tr_ff_cmp_tid_realm(TR_FILTER_TARGET *target, TR_NAME *val)
{
  return tr_name_cmp(tid_req_get_realm(target->tid_req), val);
}

static TR_NAME *tr_ff_get_tid_realm(TR_FILTER_TARGET *target)
{
  return tr_dup_name(tid_req_get_realm(target->tid_req));
}

/** Handlers for TRP community field */
static int tr_ff_cmp_trp_comm(TR_FILTER_TARGET *target, TR_NAME *val)
{
  return tr_name_cmp(trp_upd_get_comm(target->trp_upd), val);
}

static TR_NAME *tr_ff_get_trp_comm(TR_FILTER_TARGET *target)
{
  return tr_dup_name(trp_upd_get_comm(target->trp_upd));
}

/** Handlers for TID community field */
static int tr_ff_cmp_tid_comm(TR_FILTER_TARGET *target, TR_NAME *val)
{
  return tr_name_cmp(tid_req_get_comm(target->tid_req), val);
}

static TR_NAME *tr_ff_get_tid_comm(TR_FILTER_TARGET *target)
{
  return tr_dup_name(tid_req_get_comm(target->tid_req));
}

/** Handlers for TRP community_type field */
static TR_NAME *tr_ff_get_trp_comm_type(TR_FILTER_TARGET *target)
{
  TR_NAME *type=NULL;

  switch(trp_inforec_get_comm_type(target->trp_inforec)) {
    case TR_COMM_APC:
      type=tr_new_name("apc");
      break;
    case TR_COMM_COI:
      type=tr_new_name("coi");
      break;
    default:
      type=NULL;
      break; /* unknown types always fail */
  }

  return type;
}

static int tr_ff_cmp_trp_comm_type(TR_FILTER_TARGET *target, TR_NAME *val)
{
  TR_NAME *type=tr_ff_get_trp_comm_type(target);
  int retval=0;

  if (type==NULL)
    retval=1;
  else {
    retval = tr_name_cmp(val, type);
    tr_free_name(type);
  }
  return retval;
}

/** Handlers for TRP realm_role field */
static TR_NAME *tr_ff_get_trp_realm_role(TR_FILTER_TARGET *target)
{
  TR_NAME *type=NULL;

  switch(trp_inforec_get_role(target->trp_inforec)) {
    case TR_ROLE_IDP:
      type=tr_new_name("idp");
      break;
    case TR_ROLE_RP:
      type=tr_new_name("rp");
      break;
    default:
      type=NULL;
      break; /* unknown types always fail */
  }

  return type;
}

static int tr_ff_cmp_trp_realm_role(TR_FILTER_TARGET *target, TR_NAME *val)
{
  TR_NAME *type=tr_ff_get_trp_realm_role(target);
  int retval=0;

  if (type==NULL)
    retval=1;
  else {
    retval = tr_name_cmp(val, type);
    tr_free_name(type);
  }
  return retval;
}

/** Handlers for TRP apc field */
/* TODO: Handle multiple APCs, not just the first */
static int tr_ff_cmp_trp_apc(TR_FILTER_TARGET *target, TR_NAME *val)
{
  return tr_name_cmp(tr_apc_get_id(trp_inforec_get_apcs(target->trp_inforec)), val);
}

static TR_NAME *tr_ff_get_trp_apc(TR_FILTER_TARGET *target)
{
  TR_APC *apc=trp_inforec_get_apcs(target->trp_inforec);
  if (apc==NULL)
    return NULL;

  return tr_dup_name(tr_apc_get_id(apc));
}

/** Handlers for TRP owner_realm field */
static int tr_ff_cmp_trp_owner_realm(TR_FILTER_TARGET *target, TR_NAME *val)
{
  return tr_name_cmp(trp_inforec_get_owner_realm(target->trp_inforec), val);
}

static TR_NAME *tr_ff_get_trp_owner_realm(TR_FILTER_TARGET *target)
{
  return tr_dup_name(trp_inforec_get_owner_realm(target->trp_inforec));
}

/** Handlers for TRP trust_router field */
static int tr_ff_cmp_trp_trust_router(TR_FILTER_TARGET *target, TR_NAME *val)
{
  return tr_name_cmp(trp_inforec_get_trust_router(target->trp_inforec), val);
}

static TR_NAME *tr_ff_get_trp_trust_router(TR_FILTER_TARGET *target)
{
  return tr_dup_name(trp_inforec_get_trust_router(target->trp_inforec));
}

/** Handlers for TRP owner_contact field */
static int tr_ff_cmp_trp_owner_contact(TR_FILTER_TARGET *target, TR_NAME *val)
{
  return tr_name_cmp(trp_inforec_get_owner_contact(target->trp_inforec), val);
}

static TR_NAME *tr_ff_get_trp_owner_contact(TR_FILTER_TARGET *target)
{
  return tr_dup_name(trp_inforec_get_owner_contact(target->trp_inforec));
}

/** Handlers for TID req original_coi field */
static int tr_ff_cmp_tid_orig_coi(TR_FILTER_TARGET *target, TR_NAME *val)
{
  return tr_name_cmp(tid_req_get_orig_coi(target->tid_req), val);
}

static TR_NAME *tr_ff_get_tid_orig_coi(TR_FILTER_TARGET *target)
{
  return tr_dup_name(tid_req_get_orig_coi(target->tid_req));
}

/**
 * Filter field handler table
 */
struct tr_filter_field_entry {
  TR_FILTER_TYPE filter_type;
  const char *name;
  TR_FILTER_FIELD_CMP cmp;
  TR_FILTER_FIELD_GET get;
};
static struct tr_filter_field_entry tr_filter_field_table[] = {
    /* realm */
    {TR_FILTER_TYPE_TID_INBOUND, "realm", tr_ff_cmp_tid_realm, tr_ff_get_tid_realm},
    {TR_FILTER_TYPE_TRP_INBOUND, "realm", tr_ff_cmp_trp_realm, tr_ff_get_trp_realm},
    {TR_FILTER_TYPE_TRP_OUTBOUND, "realm", tr_ff_cmp_trp_realm, tr_ff_get_trp_realm},

    /* community */
    {TR_FILTER_TYPE_TID_INBOUND, "comm", tr_ff_cmp_tid_comm, tr_ff_get_tid_comm},
    {TR_FILTER_TYPE_TRP_INBOUND, "comm", tr_ff_cmp_trp_comm, tr_ff_get_trp_comm},
    {TR_FILTER_TYPE_TRP_OUTBOUND, "comm", tr_ff_cmp_trp_comm, tr_ff_get_trp_comm},

    /* community type */
    {TR_FILTER_TYPE_TRP_INBOUND, "comm_type", tr_ff_cmp_trp_comm_type, tr_ff_get_trp_comm_type},
    {TR_FILTER_TYPE_TRP_OUTBOUND, "comm_type", tr_ff_cmp_trp_comm_type, tr_ff_get_trp_comm_type},

    /* realm role */
    {TR_FILTER_TYPE_TRP_INBOUND, "realm_role", tr_ff_cmp_trp_realm_role, tr_ff_get_trp_realm_role},
    {TR_FILTER_TYPE_TRP_OUTBOUND, "realm_role", tr_ff_cmp_trp_realm_role, tr_ff_get_trp_realm_role},

    /* apc */
    {TR_FILTER_TYPE_TRP_INBOUND, "apc", tr_ff_cmp_trp_apc, tr_ff_get_trp_apc},
    {TR_FILTER_TYPE_TRP_OUTBOUND, "apc", tr_ff_cmp_trp_apc, tr_ff_get_trp_apc},

    /* trust_router */
    {TR_FILTER_TYPE_TRP_INBOUND, "trust_router", tr_ff_cmp_trp_trust_router, tr_ff_get_trp_trust_router},
    {TR_FILTER_TYPE_TRP_OUTBOUND, "trust_router", tr_ff_cmp_trp_trust_router, tr_ff_get_trp_trust_router},

    /* owner_realm */
    {TR_FILTER_TYPE_TRP_INBOUND, "owner_realm", tr_ff_cmp_trp_owner_realm, tr_ff_get_trp_owner_realm},
    {TR_FILTER_TYPE_TRP_OUTBOUND, "owner_realm", tr_ff_cmp_trp_owner_realm, tr_ff_get_trp_owner_realm},

    /* owner_contact */
    {TR_FILTER_TYPE_TRP_INBOUND, "owner_contact", tr_ff_cmp_trp_owner_contact, tr_ff_get_trp_owner_contact},
    {TR_FILTER_TYPE_TRP_OUTBOUND, "owner_contact", tr_ff_cmp_trp_owner_contact, tr_ff_get_trp_owner_contact},

    /* rp_realm */
    {TR_FILTER_TYPE_TID_INBOUND, "rp_realm", tr_ff_cmp_tid_rp_realm, tr_ff_get_tid_rp_realm},

    /* original coi */
    {TR_FILTER_TYPE_TID_INBOUND, "original_coi", tr_ff_cmp_tid_orig_coi, tr_ff_get_tid_orig_coi},

    /* info_type */
    {TR_FILTER_TYPE_TRP_INBOUND, "info_type", tr_ff_cmp_trp_info_type, tr_ff_get_trp_info_type},
    {TR_FILTER_TYPE_TRP_OUTBOUND, "info_type", tr_ff_cmp_trp_info_type, tr_ff_get_trp_info_type},

    /* Unknown */
    {TR_FILTER_TYPE_UNKNOWN, NULL } /* This must be the final entry */
};

/* TODO: support TRP metric field (requires > < comparison instead of wildcard match) */

static struct tr_filter_field_entry *tr_filter_field_entry(TR_FILTER_TYPE filter_type, TR_NAME *field_name)
{
  unsigned int ii;

  for (ii=0; tr_filter_field_table[ii].filter_type!=TR_FILTER_TYPE_UNKNOWN; ii++) {
    if ((tr_filter_field_table[ii].filter_type==filter_type)
        && (tr_name_cmp_str(field_name, tr_filter_field_table[ii].name)==0)) {
      return tr_filter_field_table+ii;
    }
  }
  return NULL;
}

/**
 * Apply a filter to a target record or TID request.
 *
 * If one of the filter lines matches, out_action is set to the applicable action. If constraints
 * is not NULL, the constraints from the matching filter line will be added to the constraint set
 * *constraints, or to a new one if *constraints is NULL. In this case, TR_FILTER_MATCH will be
 * returned.
 *
 * If there is no match, returns TR_FILTER_NO_MATCH, out_action is undefined, and constraints
 * will not be changed.
 *
 * @param target Record or request to which the filter is applied
 * @param filt Filter to apply
 * @param constraints Pointer to existing set of constraints (NULL if not tracking constraints)
 * @param out_action Action to be carried out (output)
 * @return TR_FILTER_MATCH or TR_FILTER_NO_MATCH
 */
int tr_filter_apply(TR_FILTER_TARGET *target,
                    TR_FILTER *filt,
                    TR_CONSTRAINT_SET **constraints,
                    TR_FILTER_ACTION *out_action)
{
  TALLOC_CTX *tmp_ctx = talloc_new(NULL);
  TR_FILTER_ITER *filt_iter = tr_filter_iter_new(tmp_ctx);
  TR_FLINE *this_fline = NULL;
  unsigned int jj=0;
  int retval=TR_FILTER_NO_MATCH;

  /* Default action is reject */
  *out_action = TR_FILTER_ACTION_REJECT;

  /* Validate filter */
  if ((filt_iter == NULL) || (filt==NULL) || (filt->type==TR_FILTER_TYPE_UNKNOWN)) {
    talloc_free(tmp_ctx);
    return TR_FILTER_NO_MATCH;
  }

  /* Step through filter lines looking for a match. If a line matches, retval
   * will be set to TR_FILTER_MATCH, so stop then. */
  this_fline = tr_filter_iter_first(filt_iter, filt);
  while(this_fline) {
    /* Assume we are going to succeed. If any specs fail to match, we'll set
     * this to TR_FILTER_NO_MATCH. */
    retval=TR_FILTER_MATCH;
    for (jj=0; jj<TR_MAX_FILTER_SPECS; jj++) {
      /* skip empty specs (these shouldn't really happen either) */
      if (this_fline->specs[jj]==NULL)
        continue;

      if (!tr_fspec_matches(this_fline->specs[jj], filt->type, target)) {
        retval=TR_FILTER_NO_MATCH; /* set this in case this is the last filter line */
        break; /* give up on this filter line */
      }
    }

    if (retval==TR_FILTER_MATCH)
      break;
  }

  if (retval==TR_FILTER_MATCH) {
    /* Matched line ii. Grab its action and constraints. */
    *out_action = this_fline->action;
    if (constraints!=NULL) {
      /* if either constraint is missing, these are no-ops */
      tr_constraint_add_to_set(constraints, this_fline->realm_cons);
      tr_constraint_add_to_set(constraints, this_fline->domain_cons);
    }
  }

  return retval;
}

void tr_fspec_free(TR_FSPEC *fspec)
{
  talloc_free(fspec);
}

static int tr_fspec_destructor(void *obj)
{
  TR_FSPEC *fspec = talloc_get_type_abort(obj, TR_FSPEC);
  size_t ii;

  if (fspec->field != NULL)
    tr_free_name(fspec->field);
  for (ii=0; ii<TR_MAX_FILTER_SPEC_MATCHES; ii++) {
    if (fspec->match[ii] != NULL)
      tr_free_name(fspec->match[ii]);
  }
  return 0;
}

TR_FSPEC *tr_fspec_new(TALLOC_CTX *mem_ctx)
{
  TR_FSPEC *fspec = talloc(mem_ctx, TR_FSPEC);
  size_t ii=0;

  if (fspec != NULL) {
    fspec->field = NULL;
    for (ii=0; ii<TR_MAX_FILTER_SPEC_MATCHES; ii++)
      fspec->match[ii] = NULL;

    talloc_set_destructor((void *)fspec, tr_fspec_destructor);
  }
  return fspec;
}

void tr_fspec_add_match(TR_FSPEC *fspec, TR_NAME *match)
{
  size_t ii;
  for (ii=0; ii<TR_MAX_FILTER_SPEC_MATCHES; ii++) {
    if (fspec->match[ii]==NULL) {
      fspec->match[ii]=match;
      break;
    }
  }
  /* TODO: handle case that adding the match failed */
}

/* returns 1 if the spec matches */
int tr_fspec_matches(TR_FSPEC *fspec, TR_FILTER_TYPE ftype, TR_FILTER_TARGET *target)
{
  struct tr_filter_field_entry *field=NULL;
  TR_NAME *name=NULL;
  int retval=0;

  size_t ii=0;

  if (fspec==NULL)
    return 0;

  /* Look up how to handle the requested field */
  field = tr_filter_field_entry(ftype, fspec->field);
  if (field==NULL) {
    tr_err("tr_fspec_matches: No entry to handle field %.*s for %*s filter.",
           fspec->field->len, fspec->field->buf,
           tr_filter_type_to_string(ftype));
    return 0;
  }

  name=field->get(target);
  if (name==NULL)
    return 0; /* if there's no value, there's no match */

  for (ii=0; ii<TR_MAX_FILTER_SPEC_MATCHES; ii++) {
    if (fspec->match[ii]!=NULL) {
      if (tr_name_prefix_wildcard_match(name, fspec->match[ii])) {
        retval=1;
        tr_debug("tr_fspec_matches: Field %.*s value \"%.*s\" matches \"%.*s\" for %s filter.",
                 fspec->field->len, fspec->field->buf,
                 name->len, name->buf,
                 fspec->match[ii]->len, fspec->match[ii]->buf,
                 tr_filter_type_to_string(ftype));
        break;
      }
    }
  }

  if (!retval) {
        tr_debug("tr_fspec_matches: Field %.*s value \"%.*s\" does not match for %s filter.",
                 fspec->field->len, fspec->field->buf,
                 name->len, name->buf,
                 tr_filter_type_to_string(ftype));
  }
  tr_free_name(name);
  return retval;
}

void tr_fline_free(TR_FLINE *fline)
{
  talloc_free(fline);
}

TR_FLINE *tr_fline_new(TALLOC_CTX *mem_ctx)
{
  TR_FLINE *fl = talloc(mem_ctx, TR_FLINE);
  int ii = 0;

  if (fl != NULL) {
    fl->action = TR_FILTER_ACTION_UNKNOWN;
    fl->realm_cons = NULL;
    fl->domain_cons = NULL;
    for (ii = 0; ii < TR_MAX_FILTER_SPECS; ii++)
      fl->specs[ii] = NULL;
  }
  return fl;
}

TR_FILTER *tr_filter_new(TALLOC_CTX *mem_ctx)
{
  TR_FILTER *f = talloc(mem_ctx, TR_FILTER);

  if (f != NULL) {
    f->type = TR_FILTER_TYPE_UNKNOWN;
    f->lines = g_ptr_array_new();
    if (f->lines == NULL) {
      talloc_free(f);
      return NULL;
    }
  }
  return f;
}

void tr_filter_free(TR_FILTER *filt)
{
  talloc_free(filt);
}

void tr_filter_set_type(TR_FILTER *filt, TR_FILTER_TYPE type)
{
  filt->type = type;
}

TR_FILTER_TYPE tr_filter_get_type(TR_FILTER *filt)
{
  return filt->type;
}

/**
 * Add a TR_FLINE to a filter
 *
 * Steals the line into its context on success
 *
 * @param filt
 * @param line
 * @return line, or null on failure
 */
TR_FLINE *tr_filter_add_line(TR_FILTER *filt, TR_FLINE *line)
{
  guint old_len = filt->lines->len;
  g_ptr_array_add(filt->lines, line);
  talloc_steal(filt, line); /* take this no matter what */
  if (old_len == filt->lines->len)
    return NULL; /* failed to add */
  return line;
}

/**
 * Iterator for TR_FLINES in a TR_FILTER
 *
 * @param mem_ctx
 * @return
 */
TR_FILTER_ITER *tr_filter_iter_new(TALLOC_CTX *mem_ctx)
{
  TR_FILTER_ITER *iter = talloc(mem_ctx, TR_FILTER_ITER);
  if (iter) {
    iter->filter = NULL;
  }
  return iter;
}

void tr_filter_iter_free(TR_FILTER_ITER *iter)
{
  talloc_free(iter);
}

TR_FLINE *tr_filter_iter_next(TR_FILTER_ITER *iter)
{
  if (!iter)
    return NULL;

  if (iter->ii < iter->filter->lines->len)
    return g_ptr_array_index(iter->filter->lines, iter->ii++);
  return NULL;
}

TR_FLINE *tr_filter_iter_first(TR_FILTER_ITER *iter, TR_FILTER *filter)
{
  if (!iter || !filter)
    return NULL;

  iter->filter = filter;
  iter->ii = 0;
  return tr_filter_iter_next(iter);
}

/**
 * Check that a filter is valid, i.e., can be processed.
 *
 * @param filt Filter to verify
 * @return 1 if the filter is valid, 0 otherwise
 */
int tr_filter_validate(TR_FILTER *filt)
{
  TALLOC_CTX *tmp_ctx = talloc_new(NULL);
  size_t jj=0, kk=0;
  TR_FILTER_ITER *filt_iter = tr_filter_iter_new(tmp_ctx);
  TR_FLINE *this_fline = NULL;
  
  if (!filt) {
    talloc_free(tmp_ctx);
    return 0;
  }

  /* check that we recognize the type */
  switch(filt->type) {
    case TR_FILTER_TYPE_TID_INBOUND:
    case TR_FILTER_TYPE_TRP_INBOUND:
    case TR_FILTER_TYPE_TRP_OUTBOUND:
      break;

    default:
      talloc_free(tmp_ctx);
      return 0; /* if we get here, either TR_FILTER_TYPE_UNKNOWN or an invalid value was found */
  }
  
  this_fline = tr_filter_iter_first(filt_iter, filt);
  while(this_fline) {
    /* check that we recognize the action */
    switch(this_fline->action) {
      case TR_FILTER_ACTION_ACCEPT:
      case TR_FILTER_ACTION_REJECT:
        break;

      default:
        /* if we get here, either TR_FILTER_ACTION_UNKNOWN or an invalid value was found */
        talloc_free(tmp_ctx);
        return 0;
    }

    for (jj=0; jj<TR_MAX_FILTER_SPECS; jj++) {
      if (this_fline->specs[jj]==NULL)
        continue; /* an empty filter spec is valid */

      if (!tr_filter_validate_spec_field(filt->type, this_fline->specs[jj])) {
        talloc_free(tmp_ctx);
        return 0;
      }

      /* check that at least one match is non-null */
      for (kk=0; kk<TR_MAX_FILTER_SPEC_MATCHES; kk++) {
        if (this_fline->specs[jj]->match[kk]!=NULL)
          break;
      }
      if (kk==TR_MAX_FILTER_SPEC_MATCHES) {
        talloc_free(tmp_ctx);
        return 0;
      }
    }
    this_fline = tr_filter_iter_next(filt_iter);
  }

  /* We ran the gauntlet. Success! */
  talloc_free(tmp_ctx);
  return 1;
}

int tr_filter_validate_spec_field(TR_FILTER_TYPE ftype, TR_FSPEC *fspec)
{
  if ((fspec==NULL) || (tr_filter_field_entry(ftype, fspec->field)==NULL))
    return 0; /* unknown field */

  return 1;
}

/**
 * Allocate a new filter set.
 *
 * @param mem_ctx Talloc context for the new set
 * @return Pointer to new set, or null on error
 */
TR_FILTER_SET *tr_filter_set_new(TALLOC_CTX *mem_ctx)
{
  TR_FILTER_SET *set=talloc(mem_ctx, TR_FILTER_SET);
  if (set!=NULL) {
    set->next=NULL;
    set->this=NULL;
  }
  return set;
}

/**
 * Free a filter set
 *
 * @param fs Filter set to free
 */
void tr_filter_set_free(TR_FILTER_SET *fs)
{
  talloc_free(fs);
}

/**
 * Find the tail of the filter set linked list.
 *
 * @param set Set to find tail of
 * @return Last element in the list
 */
static TR_FILTER_SET *tr_filter_set_tail(TR_FILTER_SET *set)
{
  while (set->next)
    set=set->next;
  return set;
}

/**
 * Add new filter to filter set.
 *
 * @param set Filter set
 * @param new New filter to add
 * @return 0 on success, nonzero on error
 */
int tr_filter_set_add(TR_FILTER_SET *set, TR_FILTER *new)
{
  TR_FILTER_SET *tail=NULL;

  if (set->this==NULL)
    tail=set;
  else {
    tail=tr_filter_set_tail(set);
    tail->next=tr_filter_set_new(set);
    if (tail->next==NULL)
      return 1;
    tail=tail->next;
  }
  tail->this=new;
  talloc_steal(tail, new);
  return 0;
}

/**
 * Find a filter of a given type in the filter set. If there are multiple, returns the first one.
 *
 * @param set Filter set to search
 * @param type Type of filter to find
 * @return Borrowed pointer to the filter, or null if no filter of that type is found
 */
TR_FILTER *tr_filter_set_get(TR_FILTER_SET *set, TR_FILTER_TYPE type)
{
  TR_FILTER_SET *cur=set;
  while(cur!=NULL) {
    if ((cur->this != NULL) && (cur->this->type == type))
      return cur->this;
    cur=cur->next;
  }
  return NULL;
}

TR_FILTER_TYPE filter_type[]={TR_FILTER_TYPE_TID_INBOUND,
                              TR_FILTER_TYPE_TRP_INBOUND,
                              TR_FILTER_TYPE_TRP_OUTBOUND};
const char *filter_label[]={"tid_inbound",
                            "trp_inbound",
                            "trp_outbound"};
size_t num_filter_types=sizeof(filter_type)/sizeof(filter_type[0]);

const char *tr_filter_type_to_string(TR_FILTER_TYPE ftype)
{
  size_t ii=0;

  for (ii=0; ii<num_filter_types; ii++) {
    if (ftype==filter_type[ii])
      return filter_label[ii];
  }
  return "unknown";
}

TR_FILTER_TYPE tr_filter_type_from_string(const char *s)
{
  size_t ii=0;

  for(ii=0; ii<num_filter_types; ii++) {
    if (0==strcmp(s, filter_label[ii]))
      return filter_type[ii];
  }
  return TR_FILTER_TYPE_UNKNOWN;
}

