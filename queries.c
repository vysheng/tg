#include <string.h>
#include <memory.h>
#include <stdlib.h>
#include <zlib.h>

#include "include.h"
#include "mtproto-client.h"
#include "queries.h"
#include "tree.h"
#include "mtproto-common.h"
#include "telegram.h"
#include "loop.h"
#include "structures.h"
#include "interface.h"

int verbosity;

#define QUERY_TIMEOUT 0.3

#define memcmp8(a,b) memcmp ((a), (b), 8)
DEFINE_TREE (query, struct query *, memcmp8, 0) ;
struct tree_query *queries_tree;

double get_double_time (void) {
  struct timespec tv;
  clock_gettime (CLOCK_REALTIME, &tv);
  return tv.tv_sec + 1e-9 * tv.tv_nsec;
}

struct query *query_get (long long id) {
  return tree_lookup_query (queries_tree, (void *)&id);
}

int alarm_query (struct query *q) {
  assert (q);
  return 0;
}

struct query *send_query (struct dc *DC, int ints, void *data, struct query_methods *methods) {
  assert (DC);
  assert (DC->auth_key_id);
  if (!DC->sessions[0]) {
    dc_create_session (DC);
  }
  if (verbosity) {
    logprintf ( "Sending query of size %d to DC (%s:%d)\n", 4 * ints, DC->ip, DC->port);
  }
  struct query *q = malloc (sizeof (*q));
  q->data_len = ints;
  q->data = malloc (4 * ints);
  memcpy (q->data, data, 4 * ints);
  q->msg_id = encrypt_send_message (DC->sessions[0]->c, data, ints, 1);
  if (verbosity) {
    logprintf ( "Msg_id is %lld\n", q->msg_id);
  }
  q->methods = methods;
  if (queries_tree) {
    logprintf ( "%lld %lld\n", q->msg_id, queries_tree->x->msg_id);
  }
  queries_tree = tree_insert_query (queries_tree, q, lrand48 ());

  q->ev.alarm = (void *)alarm_query;
  q->ev.timeout = get_double_time () + QUERY_TIMEOUT;
  q->ev.self = (void *)q;
  insert_event_timer (&q->ev);
  return q;
}

void query_ack (long long id) {
  struct query *q = query_get (id);
  if (q) { q->flags |= QUERY_ACK_RECEIVED; }
}

void query_error (long long id) {
  assert (fetch_int () == CODE_rpc_error);
  int error_code = fetch_int ();
  int error_len = prefetch_strlen ();
  char *error = fetch_str (error_len);
  if (verbosity) {
    logprintf ( "error for query #%lld: #%d :%.*s\n", id, error_code, error_len, error);
  }
  struct query *q = query_get (id);
  if (!q) {
    if (verbosity) {
      logprintf ( "No such query\n");
    }
  } else {
    remove_event_timer (&q->ev);
    queries_tree = tree_delete_query (queries_tree, q);
    if (q->methods && q->methods->on_error) {
      q->methods->on_error (q, error_code, error_len, error);
    }
    free (q->data);
    free (q);
  }
}

#define MAX_PACKED_SIZE (1 << 20)
static int packed_buffer[MAX_PACKED_SIZE / 4];

void query_result (long long id UU) {
  if (verbosity) {
    logprintf ( "result for query #%lld\n", id);
  }
  if (verbosity  >= 4) {
    logprintf ( "result: ");
    hexdump_in ();
  }
  int op = prefetch_int ();
  int *end = 0;
  int *eend = 0;
  if (op == CODE_gzip_packed) {
    fetch_int ();
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    size_t dl = MAX_PACKED_SIZE;

    z_stream strm = {0};
    assert (inflateInit2 (&strm, 16 + MAX_WBITS) == Z_OK);
    strm.avail_in = l;
    strm.next_in = (void *)s;
    strm.avail_out = MAX_PACKED_SIZE;
    strm.next_out = (void *)packed_buffer;

    int err = inflate (&strm, Z_FINISH);
    if (verbosity) {
      logprintf ( "inflate error = %d\n", err);
      logprintf ( "inflated %d bytes\n", (int)strm.total_out);
    }
    end = in_ptr;
    eend = in_end;
    assert (dl % 4 == 0);
    in_ptr = packed_buffer;
    in_end = in_ptr + strm.total_out / 4;
    if (verbosity >= 4) {
      logprintf ( "Unzipped data: ");
      hexdump_in ();
    }
  }
  struct query *q = query_get (id);
  if (!q) {
    if (verbosity) {
      logprintf ( "No such query\n");
    }
  } else {
    remove_event_timer (&q->ev);
    queries_tree = tree_delete_query (queries_tree, q);
    if (q->methods && q->methods->on_answer) {
      q->methods->on_answer (q);
    }
    free (q->data);
    free (q);
  }
  if (end) {
    in_ptr = end;
    in_end = eend;
  }
} 

#define event_timer_cmp(a,b) ((a)->timeout > (b)->timeout ? 1 : ((a)->timeout < (b)->timeout ? -1 : (memcmp (a, b, sizeof (struct event_timer)))))
DEFINE_TREE (timer, struct event_timer *, event_timer_cmp, 0)
struct tree_timer *timer_tree;

void insert_event_timer (struct event_timer *ev) {
  if (verbosity > 2) {
    logprintf ( "INSERT: %lf %p %p\n", ev->timeout, ev->self, ev->alarm);
  }
  tree_check_timer (timer_tree);
  timer_tree = tree_insert_timer (timer_tree, ev, lrand48 ());
  tree_check_timer (timer_tree);
}

void remove_event_timer (struct event_timer *ev) {
  if (verbosity > 2) {
    logprintf ( "REMOVE: %lf %p %p\n", ev->timeout, ev->self, ev->alarm);
  }
  tree_check_timer (timer_tree);
  timer_tree = tree_delete_timer (timer_tree, ev);
  tree_check_timer (timer_tree);
}

double next_timer_in (void) {
  if (!timer_tree) { return 1e100; }
  return tree_get_min_timer (timer_tree)->timeout;
}

void work_timers (void) {
  double t = get_double_time ();
  while (timer_tree) {
    struct event_timer *ev = tree_get_min_timer (timer_tree);
    assert (ev);
    if (ev->timeout > t) { break; }
    remove_event_timer (ev);
    ev->alarm (ev->self);
  }
}

int max_chat_size;
int want_dc_num;
extern struct dc *DC_list[];
extern struct dc *DC_working;

int help_get_config_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_config);
  fetch_int ();

  unsigned test_mode = fetch_int ();
  assert (test_mode == CODE_bool_true || test_mode == CODE_bool_false);
  assert (test_mode == CODE_bool_false);
  int this_dc = fetch_int ();
  if (verbosity) {
    logprintf ( "this_dc = %d\n", this_dc);
  }
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  assert (n <= 10);
  int i;
  for (i = 0; i < n; i++) {
    assert (fetch_int () == CODE_dc_option);
    int id = fetch_int ();
    int l1 = prefetch_strlen ();
    char *name = fetch_str (l1);
    int l2 = prefetch_strlen ();
    char *ip = fetch_str (l2);
    int port = fetch_int ();
    if (verbosity) {
      logprintf ( "id = %d, name = %.*s ip = %.*s port = %d\n", id, l1, name, l2, ip, port);
    }
    if (!DC_list[id]) {
      alloc_dc (id, strndup (ip, l2), port);
    }
  }
  max_chat_size = fetch_int ();
  if (verbosity >= 2) {
    logprintf ( "chat_size = %d\n", max_chat_size);
  }
  return 0;
}

struct query_methods help_get_config_methods  = {
  .on_answer = help_get_config_on_answer
};

char *phone_code_hash;
int send_code_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_auth_sent_code);
  assert (fetch_int () == (int)CODE_bool_true);
  int l = prefetch_strlen ();
  char *s = fetch_str (l);
  if (phone_code_hash) {
    free (phone_code_hash);
  }
  phone_code_hash = strndup (s, l);
  want_dc_num = -1;
  return 0;
}

int send_code_on_error (struct query *q UU, int error_code, int l, char *error) {
  int s = strlen ("PHONE_MIGRATE_");
  if (l >= s && !memcmp (error, "PHONE_MIGRATE_", s)) {
    int i = error[s] - '0';
    want_dc_num = i;
  } else {
    logprintf ( "error_code = %d, error = %.*s\n", error_code, l, error);
    assert (0);
  }
  return 0;
}

struct query_methods send_code_methods  = {
  .on_answer = send_code_on_answer,
  .on_error = send_code_on_error
};

int code_is_sent (void) {
  return want_dc_num;
}

int config_got (void) {
  return DC_list[want_dc_num] != 0;
}

char *suser;
extern int dc_working_num;
void do_send_code (const char *user) {
  suser = strdup (user);
  want_dc_num = 0;
  clear_packet ();
  out_int (CODE_auth_send_code);
  out_string (user);
  out_int (0);
  out_int (TG_APP_ID);
  out_string (TG_APP_HASH);

  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_code_methods);
  net_loop (0, code_is_sent);
  if (want_dc_num == -1) { return; }

  if (DC_list[want_dc_num]) {
    DC_working = DC_list[want_dc_num];
    if (!DC_working->auth_key_id) {
      dc_authorize (DC_working);
    }
    if (!DC_working->sessions[0]) {
      dc_create_session (DC_working);
    }
    dc_working_num = want_dc_num;
  } else {
    clear_packet ();
    out_int (CODE_help_get_config);
    send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &help_get_config_methods);
    net_loop (0, config_got);
    DC_working = DC_list[want_dc_num];
    if (!DC_working->auth_key_id) {
      dc_authorize (DC_working);
    }
    if (!DC_working->sessions[0]) {
      dc_create_session (DC_working);
    }
    dc_working_num = want_dc_num;
  }
  want_dc_num = 0;
  clear_packet ();
  out_int (CODE_auth_send_code);
  out_string (user);
  out_int (0);
  out_int (TG_APP_ID);
  out_string (TG_APP_HASH);

  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_code_methods);
  net_loop (0, code_is_sent);
  assert (want_dc_num == -1);
}

int sign_in_ok;
int sign_in_is_ok (void) {
  return sign_in_ok;
}

struct user User;

int sign_in_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_auth_authorization);
  int expires = fetch_int ();
  fetch_user (&User);
  sign_in_ok = 1;
  if (verbosity) {
    logprintf ( "authorized successfully: name = '%s %s', phone = '%s', expires = %d\n", User.first_name, User.last_name, User.phone, (int)(expires - get_double_time ()));
  }
  return 0;
}

int sign_in_on_error (struct query *q UU, int error_code, int l, char *error) {
  logprintf ( "error_code = %d, error = %.*s\n", error_code, l, error);
  sign_in_ok = -1;
  return 0;
}

struct query_methods sign_in_methods  = {
  .on_answer = sign_in_on_answer,
  .on_error = sign_in_on_error
};

int do_send_code_result (const char *code) {
  clear_packet ();
  out_int (CODE_sign_in);
  out_string (suser);
  out_string (phone_code_hash);
  out_string (code);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &sign_in_methods);
  sign_in_ok = 0;
  net_loop (0, sign_in_is_ok);
  return sign_in_ok;
}

extern char *user_list[];

int get_contacts_on_answer (struct query *q UU) {
  int i;
  assert (fetch_int () == (int)CODE_contacts_contacts);
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  for (i = 0; i < n; i++) {
    assert (fetch_int () == (int)CODE_contact);
    fetch_int (); // id
    fetch_int (); // mutual
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    struct user *U = fetch_alloc_user ();
    rprintf ("User #%d: " COLOR_RED "%s %s" COLOR_NORMAL " (" COLOR_GREEN "%s" COLOR_NORMAL ")\n", U->id, U->first_name, U->last_name, U->print_name);
  }
  return 0;
}

struct query_methods get_contacts_methods = {
  .on_answer = get_contacts_on_answer,
};


void do_update_contact_list (void) {
  clear_packet ();
  out_int (CODE_contacts_get_contacts);
  out_string ("");
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &get_contacts_methods);
}
