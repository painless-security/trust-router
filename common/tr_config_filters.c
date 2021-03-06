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
#include <string.h>
#include <jansson.h>
#include <talloc.h>

#include <tr_constraint_internal.h>
#include <tr_cfgwatch.h>
#include <tr_debug.h>

#if JANSSON_VERSION_HEX < 0x020500
#include "jansson_iterators.h"
#endif

static TR_CONSTRAINT *tr_cfg_parse_one_constraint(TALLOC_CTX *mem_ctx, const char *ctype, json_t *jc, TR_CFG_RC *rc)
{
  TR_CONSTRAINT *cons=NULL;
  size_t i=0;

  if (!rc) {
    tr_err("tr_cfg_parse_one_constraint: rc is null, cannot process constraint.");
    return NULL;
  }

  if ((!ctype) || (!jc) ||
      (!json_is_array(jc)) ||
      (0 >= json_array_size(jc)) ||
      (!json_is_string(json_array_get(jc, 0)))) {
    tr_err("tr_cfg_parse_one_constraint: config error.");
    *rc=TR_CFG_NOPARSE;
    return NULL;
  }

  if (NULL==(cons=tr_constraint_new(mem_ctx))) {
    tr_debug("tr_cfg_parse_one_constraint: Out of memory (cons).");
    *rc=TR_CFG_NOMEM;
    return NULL;
  }

  if (NULL==(cons->type=tr_new_name(ctype))) {
    tr_err("tr_cfg_parse_one_constraint: Out of memory (type).");
    *rc=TR_CFG_NOMEM;
    tr_constraint_free(cons);
    return NULL;
  }

  for (i=0; i < json_array_size(jc); i++) {
    if (NULL == tr_constraint_add_match(cons, tr_new_name(json_string_value(json_array_get(jc, i))))) {
      tr_err("tr_cfg_parse_one_constraint: Out of memory (match %d).", i);
      *rc=TR_CFG_NOMEM;
      tr_constraint_free(cons);
      return NULL;
    }
  }

  return cons;
}

static TR_FILTER *tr_cfg_parse_one_filter(TALLOC_CTX *mem_ctx, json_t *jfilt, TR_FILTER_TYPE ftype, TR_CFG_RC *rc)
{
  TALLOC_CTX *tmp_ctx = talloc_new(NULL);
  TR_FILTER *filt = NULL;
  TR_FLINE *fline = NULL;
  TR_FSPEC *fspec = NULL;
  json_t *jfaction = NULL;
  json_t *jfline = NULL;
  json_t *jfspecs = NULL;
  json_t *this_jfspec = NULL;
  json_t *jfield = NULL;
  json_t *jmatch = NULL;
  json_t *jrc = NULL;
  json_t *jdc = NULL;
  json_t *this_jmatch = NULL;
  TR_NAME *name = NULL;
  size_t i = 0, j = 0, k = 0;

  *rc = TR_CFG_ERROR;

  if ((jfilt == NULL) || (rc == NULL)) {
    tr_err("tr_cfg_parse_one_filter: null argument");
    *rc = TR_CFG_BAD_PARAMS;
    goto cleanup;
  }

  if (NULL == (filt = tr_filter_new(tmp_ctx))) {
    tr_err("tr_cfg_parse_one_filter: Out of memory.");
    *rc = TR_CFG_NOMEM;
    goto cleanup;
  }
  tr_filter_set_type(filt, ftype);

  /* For each entry in the filter... */
  json_array_foreach(jfilt, i, jfline) {
    if ((NULL == (jfaction = json_object_get(jfline, "action"))) ||
        (!json_is_string(jfaction))) {
      tr_debug("tr_cfg_parse_one_filter: Error parsing filter action.");
      *rc = TR_CFG_NOPARSE;
      goto cleanup;
    }

    if ((NULL == (jfspecs = json_object_get(jfline, "specs"))) ||
        (!json_is_array(jfspecs)) ||
        (0 == json_array_size(jfspecs))) {
      tr_debug("tr_cfg_parse_one_filter: Error parsing filter specs.");
      *rc = TR_CFG_NOPARSE;
      goto cleanup;
    }

    fline = tr_fline_new(tmp_ctx);
    if (fline == NULL) {
      tr_debug("tr_cfg_parse_one_filter: Out of memory allocating filter line %d.", i);
      *rc = TR_CFG_NOMEM;
      goto cleanup;
    }
    if (!strcmp(json_string_value(jfaction), "accept")) {
      fline->action = TR_FILTER_ACTION_ACCEPT;
      tr_debug("tr_cfg_parse_one_filter: Filter action is 'accept'");

    } else if (!strcmp(json_string_value(jfaction), "reject")) {
      fline->action = TR_FILTER_ACTION_REJECT;
      tr_debug("tr_cfg_parse_one_filter: Filter action is 'reject'");
    } else {
      tr_debug("tr_cfg_parse_one_filter: Error parsing filter action, unknown action '%s'.",
               json_string_value(jfaction));
      *rc = TR_CFG_NOPARSE;
      goto cleanup;
    }

    if (NULL != (jrc = json_object_get(jfline, "realm_constraints"))) {
      if (!json_is_array(jrc)) {
        tr_err("tr_cfg_parse_one_filter: cannot parse realm_constraints, not an array.");
        *rc = TR_CFG_NOPARSE;
        goto cleanup;
      } else if (json_array_size(jrc) > 0) {
        /* ok we actually have entries to process */
        if (NULL == (fline->realm_cons = tr_cfg_parse_one_constraint(fline, "realm", jrc, rc))) {
          tr_debug("tr_cfg_parse_one_filter: Error parsing realm constraint");
          *rc = TR_CFG_NOPARSE;
          goto cleanup;
        }
      }
    }

    if (NULL != (jdc = json_object_get(jfline, "domain_constraints"))) {
      if (!json_is_array(jdc)) {
        tr_err("tr_cfg_parse_one_filter: cannot parse domain_constraints, not an array.");
        *rc = TR_CFG_NOPARSE;
        goto cleanup;
      } else if (json_array_size(jdc) > 0) {
        if (NULL == (fline->domain_cons = tr_cfg_parse_one_constraint(fline, "domain", jdc, rc))) {
          tr_debug("tr_cfg_parse_one_filter: Error parsing domain constraint");
          *rc = TR_CFG_NOPARSE;
          goto cleanup;
        }
      }
    }

    /*For each filter spec within the filter line... */
    tr_debug("tr_cfg_parse_one_filter: Filter line has %d spec(s)", json_array_size(jfspecs));
    json_array_foreach(jfspecs, j, this_jfspec) {
      if ((NULL == (jfield = json_object_get(this_jfspec, "field"))) ||
          (!json_is_string(jfield))) {
        tr_debug("tr_cfg_parse_one_filter: Error parsing filter: missing field for filer spec %d, filter line %d.", i,
                 j);
        *rc = TR_CFG_NOPARSE;
        goto cleanup;
      }

      /* check that we have a match attribute */
      if (NULL == (jmatch = json_object_get(this_jfspec, "match"))) {
        tr_debug("tr_cfg_parse_one_filter: Error parsing filter: missing match for filer spec %d, filter line %d.", i,
                 j);
        *rc = TR_CFG_NOPARSE;
        goto cleanup;
      }

      /* check that match is a string or an array */
      if ((!json_is_string(jmatch)) && (!json_is_array(jmatch))) {
        tr_debug(
            "tr_cfg_parse_one_filter: Error parsing filter: match not a string or array for filter spec %d, filter line %d.",
            i, j);
        *rc = TR_CFG_NOPARSE;
        goto cleanup;
      }

      /* allocate the filter spec */
      if (NULL == (fspec = tr_fspec_new(fline))) {
        tr_debug("tr_cfg_parse_one_filter: Out of memory.");
        *rc = TR_CFG_NOMEM;
        goto cleanup;
      }

      /* fill in the field */
      if (NULL == (fspec->field = tr_new_name(json_string_value(jfield)))) {
        tr_debug("tr_cfg_parse_one_filter: Out of memory.");
        *rc = TR_CFG_NOMEM;
        goto cleanup;
      }

      /* fill in the matches */
      if (json_is_string(jmatch)) {
        if (NULL == (name = tr_new_name(json_string_value(jmatch)))) {
          tr_debug("tr_cfg_parse_one_filter: Out of memory.");
          *rc = TR_CFG_NOMEM;
          goto cleanup;
        }
        tr_fspec_add_match(fspec, name);
      } else {
        /* jmatch is an array (we checked earlier) */
        json_array_foreach(jmatch, k, this_jmatch) {
          if (NULL == (name = tr_new_name(json_string_value(this_jmatch)))) {
            tr_debug("tr_cfg_parse_one_filter: Out of memory.");
            *rc = TR_CFG_NOMEM;
            goto cleanup;
          }
          tr_fspec_add_match(fspec, name);
        }
      }
      if (!tr_filter_validate_spec_field(ftype, fspec)) {
        tr_debug("tr_cfg_parse_one_filter: Invalid filter field \"%.*s\" for %s filter, spec %d, filter %d.",
                 fspec->field->len,
                 fspec->field->buf,
                 tr_filter_type_to_string(filt->type),
                 i, j);
        *rc = TR_CFG_ERROR;
        goto cleanup;
      }

      if (tr_fline_add_spec(fline, fspec) == NULL) {
        tr_debug("tr_cfg_parse_one_filter: Unable to add spec %d to line %d of %s filter.",
                 j, i, tr_filter_type_to_string(filt->type));
      }
    }

    if (NULL == tr_filter_add_line(filt, fline)) {
      tr_debug("tr_cfg_parse_one_filter: Error adding line %d for %s filter",
               i, tr_filter_type_to_string(filt->type));
      *rc = TR_CFG_NOMEM;
      goto cleanup;
    }
    tr_debug("tr_cfg_parse_one_filter: Added line %d to %s filter", i, tr_filter_type_to_string(filt->type));
  }

  /* check that the filter is valid */
  if (!tr_filter_validate(filt)) {
    *rc = TR_CFG_ERROR;
  } else {
    *rc = TR_CFG_SUCCESS;
    talloc_steal(mem_ctx, filt);
  }

cleanup:
  talloc_free(tmp_ctx);
  if (*rc!=TR_CFG_SUCCESS)
    filt=NULL;
  return filt;
}

TR_FILTER_SET *tr_cfg_parse_filters(TALLOC_CTX *mem_ctx, json_t *jfilts, TR_CFG_RC *rc)
{
  TALLOC_CTX *tmp_ctx=talloc_new(NULL);
  json_t *jfilt;
  const char *filt_label=NULL;
  TR_FILTER *filt=NULL;
  TR_FILTER_SET *filt_set=NULL;
  TR_FILTER_TYPE filt_type=TR_FILTER_TYPE_UNKNOWN;

  *rc=TR_CFG_ERROR;

  /* no filters */
  if (jfilts==NULL) {
    *rc=TR_CFG_SUCCESS;
    goto cleanup;
  }

  filt_set=tr_filter_set_new(tmp_ctx);
  if (filt_set==NULL) {
    tr_debug("tr_cfg_parse_filters: Unable to allocate filter set.");
    *rc = TR_CFG_NOMEM;
    goto cleanup;
  }

  json_object_foreach(jfilts, filt_label, jfilt) {
    /* check that we got a filter */
    if (jfilt == NULL) {
      tr_debug("tr_cfg_parse_filters: Definition for %s filter is missing.", filt_label);
      *rc = TR_CFG_NOPARSE;
      goto cleanup;
    }

    /* check that we recognize the filter type */
    filt_type=tr_filter_type_from_string(filt_label);
    if (filt_type==TR_FILTER_TYPE_UNKNOWN) {
      tr_debug("tr_cfg_parse_filters: Unrecognized filter (%s) defined.", filt_label);
      *rc = TR_CFG_NOPARSE;
      goto cleanup;
    }

    /* finally, parse the filter */
    tr_debug("tr_cfg_parse_filters: Found %s filter.", filt_label);
    filt = tr_cfg_parse_one_filter(tmp_ctx, jfilt, filt_type, rc);
    if (*rc != TR_CFG_SUCCESS) {
      tr_debug("tr_cfg_parse_filters: Error parsing %s filter.", filt_label);
      *rc = TR_CFG_NOPARSE;
      goto cleanup;
    }
    if (tr_filter_set_add(filt_set, filt) != 0) {
      tr_debug("tr_cfg_parse_filters: Error adding %s filter to filter set.", filt_label);
      *rc = TR_CFG_NOPARSE;
      goto cleanup;
    }
  }

  *rc=TR_CFG_SUCCESS;

cleanup:
  if (*rc==TR_CFG_SUCCESS)
    talloc_steal(mem_ctx, filt_set);
  else if (filt_set!=NULL) {
    talloc_free(filt_set);
    filt_set=NULL;
  }

  talloc_free(tmp_ctx);
  return filt_set;
}
