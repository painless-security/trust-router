#include <talloc.h>

#include <tr_name.h>
#include <trp_internal.h>
#include <tr_debug.h>


/* static prototypes */
static void *trp_msg_body_update_new(TALLOC_CTX *mem_ctx);
static TRP_RC trp_parse_update(TRP_MSG *msg, json_t *jmsg);

static void *trp_msg_body_route_req_new(TALLOC_CTX *mem_ctx);
static TRP_RC trp_parse_route_req(TRP_MSG *msg, json_t *jmsg);

static void *trp_msg_info_route_new(TALLOC_CTX *mem_ctx);
static TRP_RC trp_parse_info_route(TRP_MSG *msg, json_t *jmsg);


/* table of string names for TMT_MSG_TYPE codes */
struct trp_msg_type_entry {
  const char *name;
  TRP_MSG_TYPE type;
  void *(*allocator)(TALLOC_CTX *);
  TRP_RC (*parser)(TRP_MSG *, json_t *);
};
static struct trp_msg_type_entry trp_msg_type_table[] = {
  { "update", TRP_MSG_TYPE_UPDATE, trp_msg_body_update_new, trp_parse_update },
  { "route_req", TRP_MSG_TYPE_ROUTE_REQ, trp_msg_body_route_req_new, trp_parse_route_req },
  { NULL, TRP_MSG_TYPE_UNKNOWN, NULL, NULL } /* must be the last entry */
};

struct trp_msg_info_type_entry {
  const char *name;
  TRP_MSG_INFO_TYPE type;
};
static struct trp_msg_info_type_entry trp_msg_info_type_table[] = {
  { "route_info", TRP_MSG_INFO_TYPE_ROUTE },
  { "comm_info", TRP_MSG_INFO_TYPE_COMMUNITY },
  { NULL, TRP_MSG_INFO_TYPE_UNKNOWN } /* must be the last entry */
};

/* Use talloc's dynamic type checking to verify type.
 * By default, this will cause program abort, but can be overridden
 * via talloc_set_abort_fn() if more graceful handling is needed. */
static void msg_body_type_check(TRP_MSG_TYPE msgtype, void *p)
{
  switch (msgtype) {
  case TRP_MSG_TYPE_UPDATE:
    talloc_get_type_abort(p, TRP_MSG_BODY_UPDATE);
    break;

  case TRP_MSG_TYPE_ROUTE_REQ:
    talloc_get_type_abort(p, TRP_MSG_BODY_ROUTE_REQ);
    break;

  default:
    break;
  }
}

/* look up an entry in the trp_msg_type_table */
static struct trp_msg_type_entry *get_trp_msg_type_entry(TRP_MSG_TYPE msgtype)
{
  struct trp_msg_type_entry *entry=trp_msg_type_table;

  while ((entry->type != TRP_MSG_TYPE_UNKNOWN)
        && (entry->type != msgtype)) {
    entry ++;
  }
  return entry;
}

/* look up an entry in the trp_msg_info_type_table */
static struct trp_msg_info_type_entry *get_trp_msg_info_type_entry(TRP_MSG_INFO_TYPE msgtype)
{
  struct trp_msg_info_type_entry *entry=trp_msg_info_type_table;

  while ((entry->type != TRP_MSG_INFO_TYPE_UNKNOWN)
        && (entry->type != msgtype)) {
    entry ++;
  }
  return entry;
}

/* translate strings to codes */
TRP_MSG_TYPE trp_msg_type_from_string(const char *s)
{
  struct trp_msg_type_entry *entry=trp_msg_type_table;

  while ((entry->type != TRP_MSG_TYPE_UNKNOWN)
        && (strcmp(s, entry->name)!=0)) {
    entry++;
  }
  return entry->type;
}
/* translate codes to strings (do not need to be freed) 
 * Returns NULL on an unknown code */
const char *trp_msg_type_to_string(TRP_MSG_TYPE msgtype)
{
  struct trp_msg_type_entry *entry=get_trp_msg_type_entry(msgtype);
  return entry->name;
}

/* translate strings to codes */
TRP_MSG_INFO_TYPE trp_msg_info_type_from_string(const char *s)
{
  struct trp_msg_info_type_entry *entry=trp_msg_info_type_table;

  while ((entry->type != TRP_MSG_INFO_TYPE_UNKNOWN)
        && (strcmp(s, entry->name)!=0)) {
    entry++;
  }
  return entry->type;
}
/* translate codes to strings (do not need to be freed) 
 * Returns NULL on an unknown code */
const char *trp_msg_info_type_to_string(TRP_MSG_INFO_TYPE msgtype)
{
  struct trp_msg_info_type_entry *entry=get_trp_msg_info_type_entry(msgtype);
  return entry->name;
}


TRP_MSG *trp_msg_new(TALLOC_CTX *mem_ctx)
{
  TRP_MSG *new_msg=talloc(mem_ctx, TRP_MSG);

  if (new_msg != NULL) {
    new_msg->type=TRP_MSG_INFO_TYPE_UNKNOWN;
    new_msg->body=NULL;
  }
  return new_msg;
}

void trp_msg_destroy(TRP_MSG *msg)
{
  if (msg)
    talloc_free(msg);
}


/* JSON helpers */
/* Read attribute attr from msg as an integer */
static TRP_RC trp_get_json_integer(json_t *jmsg, const char *attr, int *dest)
{
  json_error_t err;
  json_t *obj;

  obj=json_object_get(jmsg, attr);
  if (obj == NULL) {
    return TRP_NOPARSE;
  }
  /* check type */
  if (!json_is_integer(obj)) {
    return TRP_BADTYPE;
  }

  (*dest)=json_integer_value(obj);
  return TRP_SUCCESS;
}

/* Read attribute attr from msg as a string. Copies string into mem_ctx context so jmsg can
 * be destroyed safely. */
static TRP_RC trp_get_json_string(json_t *jmsg, const char *attr, char **dest, TALLOC_CTX *mem_ctx)
{
  json_error_t err;
  json_t *obj;

  obj=json_object_get(jmsg, attr);
  if (obj == NULL)
    return TRP_NOPARSE;

  /* check type */
  if (!json_is_string(obj))
    return TRP_BADTYPE;

  *dest=talloc_strdup(mem_ctx, json_string_value(obj));
  if (*dest==NULL)
    return TRP_NOMEM;

  return TRP_SUCCESS;
}


/* called by talloc when destroying an update message body */
static int trp_msg_info_route_destructor(void *object)
{
  TRP_MSG_INFO_ROUTE *body=talloc_get_type_abort(object, TRP_MSG_INFO_ROUTE);
  
  /* clean up TR_NAME data, which are not managed by talloc */
  if (body->community != NULL) {
    tr_free_name(body->community);
    body->community=NULL;
    tr_debug("trp_msg_info_route_destructor: freed community");
  }
  if (body->realm != NULL) {
    tr_free_name(body->realm);
    body->realm=NULL;
    tr_debug("trp_msg_info_route_destructor: freed realm");
  }
  if (body->trust_router != NULL) {
    tr_free_name(body->trust_router);
    body->trust_router=NULL;
    tr_debug("trp_msg_info_route_destructor: freed trust_router");
  }

  return 0;
}

static void *trp_msg_info_route_new(TALLOC_CTX *mem_ctx)
{
  TRP_MSG_INFO_ROUTE *new_rec=talloc(mem_ctx, TRP_MSG_INFO_ROUTE);

  if (new_rec != NULL) {
    new_rec->next=NULL;
    new_rec->type=TRP_MSG_INFO_TYPE_UNKNOWN;
    new_rec->community=NULL;
    new_rec->realm=NULL;
    new_rec->trust_router=NULL;
    new_rec->metric=TRP_METRIC_INFINITY;
    new_rec->interval=0;
    talloc_set_destructor((void *)new_rec, trp_msg_info_route_destructor);
  }
  return new_rec;
}

/* parse a single record */
static TRP_RC trp_parse_update_record(TRP_MSG_INFO_ROUTE *rec, json_t *jrecord)
{
  TALLOC_CTX *tmp_ctx=talloc_new(NULL);
  TRP_RC rc=TRP_ERROR;
  char *s=NULL;
  int num=0;

  rc=trp_get_json_string(jrecord, "record_type", &s, tmp_ctx);
  if (rc != TRP_SUCCESS)
    goto cleanup;
  rec->type=trp_msg_info_type_from_string(s);
  talloc_free(s); s=NULL;
  /* We only support route_info records for now*/
  if (rec->type!=TRP_MSG_INFO_TYPE_ROUTE) {
    rc=TRP_UNSUPPORTED;
    goto cleanup;
  }

  tr_debug("trp_parse_update_record: '%s' record found.", trp_msg_info_type_to_string(rec->type));

  rc=trp_get_json_string(jrecord, "community", &s, tmp_ctx);
  if (rc != TRP_SUCCESS)
    goto cleanup;
  if (NULL==(rec->community=tr_new_name(s)))
    goto cleanup;
  talloc_free(s); s=NULL;

  tr_debug("trp_parse_update_record: 'community' is '%.*s'.", rec->community->len, rec->community->buf);

  rc=trp_get_json_string(jrecord, "realm", &s, tmp_ctx);
  if (rc != TRP_SUCCESS)
    goto cleanup;
  if (NULL==(rec->realm=tr_new_name(s)))
    goto cleanup;
  talloc_free(s); s=NULL;

  tr_debug("trp_parse_update_record: 'realm' is '%.*s'.", rec->realm->len, rec->realm->buf);

  rc=trp_get_json_string(jrecord, "trust_router", &s, tmp_ctx);
  if (rc != TRP_SUCCESS)
    goto cleanup;
  if (NULL==(rec->trust_router=tr_new_name(s)))
    goto cleanup;
  talloc_free(s); s=NULL;

  tr_debug("trp_parse_update_record: 'trust_router' is '%.*s'.", rec->trust_router->len, rec->trust_router->buf);

  rc=trp_get_json_integer(jrecord, "metric", &num);
  if (rc != TRP_SUCCESS)
    goto cleanup;
  rec->metric=num;

  tr_debug("trp_parse_update_record: 'metric' is %d.", rec->metric);
  
  rc=trp_get_json_integer(jrecord, "interval", &num);
  if (rc != TRP_SUCCESS)
    goto cleanup;
  rec->interval=num;

  tr_debug("trp_parse_update_record: 'interval' is %d.", rec->interval);

  rc=TRP_SUCCESS;

cleanup:
  if (rc != TRP_SUCCESS) {
    /* clean up TR_NAME data, which is not managed by talloc */
    if (rec->community != NULL) {
      tr_free_name(rec->community);
      rec->community=NULL;
    }
    if (rec->realm != NULL) {
      tr_free_name(rec->realm);
      rec->realm=NULL;
    }
    if (rec->trust_router != NULL) {
      tr_free_name(rec->trust_router);
      rec->trust_router=NULL;
    }
  }
  
  talloc_free(tmp_ctx);
  return rc;
}



static void *trp_msg_body_update_new(TALLOC_CTX *mem_ctx)
{
  TRP_MSG_BODY_UPDATE *new_body=talloc(mem_ctx, TRP_MSG_BODY_UPDATE);

  if (new_body!=NULL) {
    new_body->records=NULL;
  }
}

/* Parse an update body. Creates a linked list of records in the msg->body talloc context.
 *
 * An error will be returned if any unparseable records are encountered. 
 *
 * TODO: clean up return codes. */
static TRP_RC trp_parse_update(TRP_MSG *msg, json_t *jbody)
{
  TALLOC_CTX *tmp_ctx=talloc_new(NULL);
  json_t *jrecords=NULL;
  size_t ii=0;
  size_t nrec=0;
  TRP_MSG_BODY_UPDATE *msg_body=NULL;
  TRP_MSG_INFO_ROUTE *new_rec=NULL;
  TRP_MSG_INFO_ROUTE *list_tail=NULL;
  TRP_RC rc=TRP_ERROR;

  if (msg->type != TRP_MSG_TYPE_UPDATE) {
    rc=TRP_BADTYPE;
    goto cleanup;
  }
  msg_body=talloc_get_type(msg->body, TRP_MSG_BODY_UPDATE);
  if (msg_body==NULL) {
    rc=TRP_BADTYPE;
    goto cleanup;
  }

  jrecords=json_object_get(jbody, "records");
  if ((jrecords==NULL) || (!json_is_array(jrecords))) {
    rc=TRP_NOPARSE;
    goto cleanup;
  }

  tr_debug("trp_parse_update: found %d records", json_array_size(jrecords));
  /* process the array */
  for (ii=0; ii<json_array_size(jrecords); ii++) {
    new_rec=trp_msg_info_route_new(tmp_ctx);
    if (new_rec==NULL) {
      rc=TRP_NOMEM;
      goto cleanup;
    }

    if (TRP_SUCCESS != trp_parse_update_record(new_rec, json_array_get(jrecords, ii))) {
      rc=TRP_NOPARSE;
      goto cleanup;
    }

    if (list_tail==NULL)
      msg_body->records=new_rec; /* first is a special case */
    else
      list_tail->next=new_rec;

    list_tail=new_rec;
  }

  /* Succeeded. Move all of our new allocations into the correct talloc context */
  for (list_tail=msg_body->records; list_tail != NULL; list_tail=list_tail->next)
    talloc_steal(msg->body, list_tail); /* all successfully parsed bodies belong to msg context */

  rc=TRP_SUCCESS;

cleanup:
  talloc_free(tmp_ctx);
  if ((rc != TRP_SUCCESS) && (msg_body != NULL))
    msg_body->records=NULL; /* don't leave this hanging */

  return rc;
}

/* pretty print */
static void trp_msg_info_route_print(void *rec_in)
{
  TRP_MSG_INFO_ROUTE *rec=talloc_get_type(rec_in, TRP_MSG_INFO_ROUTE); /* null if wrong type */

  while (rec!=NULL) {
    printf("    [record_type=%s\n     community=%.*s\n     realm=%.*s\n     trust_router=%.*s\n     metric=%d\n     interval=%d]\n",
           trp_msg_info_type_to_string(rec->type),
           rec->community->len, rec->community->buf,
           rec->realm->len, rec->realm->buf,
           rec->trust_router->len, rec->trust_router->buf,
           rec->metric, rec->interval);
    rec=rec->next;
  }
}

void trp_msg_body_update_print(void *body_in)
{
  TRP_MSG_BODY_UPDATE *body=talloc_get_type(body_in, TRP_MSG_BODY_UPDATE); /* null if wrong type */

  if (body!=NULL) {
    printf("  {records=\n");
    trp_msg_info_route_print(body->records);
    printf("  }\n");
  }
}

void trp_msg_print(TRP_MSG *msg)
{
  /* right now just assumes update */
  printf("{message_type=%s\n", trp_msg_type_to_string(msg->type));
  trp_msg_body_update_print(msg->body);
  printf("}\n");
}

static void *trp_msg_body_route_req_new(TALLOC_CTX *mem_ctx)
{
  TRP_MSG_BODY_ROUTE_REQ *new_body=talloc(mem_ctx, TRP_MSG_BODY_ROUTE_REQ);

  if (new_body != NULL) {
    new_body->community=NULL;
    new_body->realm=NULL;
  }
  return new_body;
}

static TRP_RC trp_parse_route_req(TRP_MSG *msg, json_t *jbody)
{
  return TRP_ERROR;
}



/* returns a pointer to one of the message body types, or NULL on error/unknown type */
static void *trp_msg_body_new(TALLOC_CTX *mem_ctx, TRP_MSG_TYPE msgtype)
{
  void *new_body=NULL;
  struct trp_msg_type_entry *info=get_trp_msg_type_entry(msgtype);

  if (info->type==TRP_MSG_TYPE_UNKNOWN) {
    tr_debug("trp_msg_body_new: Unknown type %d.", info->type);
    return NULL;
  }

  new_body=info->allocator(mem_ctx);
  msg_body_type_check(msgtype, new_body); /* aborts program on type violation */
  return new_body;
}

/* call the correct parser */
static TRP_RC trp_parse_msg_body(TRP_MSG *msg, json_t *jbody)
{
  struct trp_msg_type_entry *info=get_trp_msg_type_entry(msg->type);

  if (info->type==TRP_MSG_TYPE_UNKNOWN) {
    tr_debug("trp_msg_body_parse: Unknown type %d.", info->type);
    return TRP_ERROR;
  }

  return info->parser(msg, jbody);
}


TRP_RC trp_parse_msg(TALLOC_CTX *mem_ctx, const char *buf, size_t buflen, TRP_MSG **msg) 
{
  TALLOC_CTX *tmp_ctx=talloc_new(NULL);
  TRP_MSG *new_msg=NULL;
  TRP_RC msg_rc=TRP_ERROR;
  json_error_t json_err;
  json_t *jmsg=NULL; /* handle for the whole msg */
  json_t *jbody=NULL;
  char *s;

  tr_debug("trp_parse_msg: parsing %d bytes", buflen);

  jmsg=json_loadb(buf, buflen, 0, &json_err);
  if (jmsg == NULL) {
    tr_debug("trp_parse_msg: Error parsing message.");
    msg_rc=TRP_NOPARSE;
    goto cleanup;
  }

  /* parse the common part of the message */
  new_msg=trp_msg_new(tmp_ctx);
  if (new_msg == NULL) {
    tr_debug("trp_parse_msg: Error allocating message.");
    msg_rc=TRP_NOMEM;
    goto cleanup;
  }

  switch (trp_get_json_string(jmsg, "message_type", &s, new_msg)) {
  case TRP_SUCCESS:
    break;
  case TRP_NOPARSE:
    tr_debug("trp_parse_msg: required attribute 'message_type' not present.");
    msg_rc=TRP_NOPARSE;
    goto cleanup;
  case TRP_BADTYPE:
    tr_debug("trp_parse_msg: required attribute 'message_type' is not a string.");
    msg_rc=TRP_NOPARSE;
    goto cleanup;
  default:
    tr_debug("trp_parse_msg: error parsing 'message_type'.");
    msg_rc=TRP_NOPARSE;
    goto cleanup;
  }
  
  tr_debug("trp_parse_msg: 'message_type' is '%s'", s);
  new_msg->type = trp_msg_type_from_string(s);
  if (new_msg->type==TRP_MSG_TYPE_UNKNOWN) {
    tr_debug("trp_parse_msg: Parsing error, unknown message_type (%s).", s);
    msg_rc=TRP_NOPARSE;
    goto cleanup;
  }  

  new_msg->body=trp_msg_body_new(new_msg, new_msg->type);
  if (new_msg->body==NULL) {
    tr_debug("trp_parse_msg: Error allocating message body for message_type %d.", new_msg->type);
    msg_rc=TRP_NOMEM;
    goto cleanup;
  }
  jbody=json_object_get(jmsg, "body");
  if (jbody==NULL) {
    tr_debug("trp_parse_msg: Message body not found.");
    msg_rc=TRP_NOPARSE;
    goto cleanup;
  }

  switch (trp_parse_msg_body(new_msg, jbody)) {
  case TRP_SUCCESS:
    break;
  default:
    tr_debug("trp_parse_msg: Error parsing message body.");
    goto cleanup;
  }

  /* success! */
  (*msg)=new_msg;
  new_msg=NULL;
  talloc_steal(mem_ctx, *msg);
  msg_rc=TRP_SUCCESS;

cleanup:
  talloc_free(tmp_ctx);
  json_decref(jmsg);
  return msg_rc;
}
