/*
    This file is part of telegram-client.

    Telegram-client is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Telegram-client is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this telegram-client.  If not, see <http://www.gnu.org/licenses/>.

    Copyright Vitaly Valtman 2013
*/
#define _FILE_OFFSET_BITS 64
#include <string.h>
#include <memory.h>
#include <stdlib.h>
#include <zlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "include.h"
#include "mtproto-client.h"
#include "queries.h"
#include "tree.h"
#include "mtproto-common.h"
#include "telegram.h"
#include "loop.h"
#include "structures.h"
#include "interface.h"

char *get_downloads_directory (void);
int verbosity;

long long cur_uploading_bytes;
long long cur_uploaded_bytes;
long long cur_downloading_bytes;
long long cur_downloaded_bytes;

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
  if (verbosity >= 1) {
    logprintf ("Alarm query %lld\n", q->msg_id);
  }
  q->ev.timeout = get_double_time () + QUERY_TIMEOUT;
  insert_event_timer (&q->ev);

  clear_packet ();
  out_int (CODE_msg_container);
  out_int (1);
  out_long (q->msg_id);
  out_int (q->seq_no);
  out_int (4 * q->data_len);
  out_ints (q->data, q->data_len);
  
  encrypt_send_message (q->session->c, packet_buffer, packet_ptr - packet_buffer, 0);
  return 0;
}

void query_restart (long long id) {
  struct query *q = query_get (id);
  if (q) {
    remove_event_timer (&q->ev);
    alarm_query (q);
  }
}

struct query *send_query (struct dc *DC, int ints, void *data, struct query_methods *methods, void *extra) {
  assert (DC);
  assert (DC->auth_key_id);
  if (!DC->sessions[0]) {
    dc_create_session (DC);
  }
  if (verbosity) {
    logprintf ( "Sending query of size %d to DC (%s:%d)\n", 4 * ints, DC->ip, DC->port);
  }
  struct query *q = malloc (sizeof (*q));
  memset (q, 0, sizeof (*q));
  q->data_len = ints;
  q->data = malloc (4 * ints);
  memcpy (q->data, data, 4 * ints);
  q->msg_id = encrypt_send_message (DC->sessions[0]->c, data, ints, 1);
  q->session = DC->sessions[0];
  q->seq_no = DC->sessions[0]->seq_no - 1; 
  if (verbosity) {
    logprintf ( "Msg_id is %lld %p\n", q->msg_id, q);
  }
  q->methods = methods;
  q->DC = DC;
  if (queries_tree) {
    if (verbosity >= 2) {
      logprintf ( "%lld %lld\n", q->msg_id, queries_tree->x->msg_id);
    }
  }
  queries_tree = tree_insert_query (queries_tree, q, lrand48 ());

  q->ev.alarm = (void *)alarm_query;
  q->ev.timeout = get_double_time () + QUERY_TIMEOUT;
  q->ev.self = (void *)q;
  insert_event_timer (&q->ev);

  q->extra = extra;
  return q;
}

void query_ack (long long id) {
  struct query *q = query_get (id);
  if (q && !(q->flags & QUERY_ACK_RECEIVED)) { 
    assert (q->msg_id == id);
    q->flags |= QUERY_ACK_RECEIVED; 
    remove_event_timer (&q->ev);
  }
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
    if (!(q->flags & QUERY_ACK_RECEIVED)) {
      remove_event_timer (&q->ev);
    }
    queries_tree = tree_delete_query (queries_tree, q);
    if (q->methods && q->methods->on_error) {
      q->methods->on_error (q, error_code, error_len, error);
    } else {
      logprintf ( "error for query #%lld: #%d :%.*s\n", id, error_code, error_len, error);
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

    z_stream strm;
    memset (&strm, 0, sizeof (strm));
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
    in_ptr = in_end;
  } else {
    if (!(q->flags & QUERY_ACK_RECEIVED)) {
      remove_event_timer (&q->ev);
    }
    queries_tree = tree_delete_query (queries_tree, q);
    if (q->methods && q->methods->on_answer) {
      q->methods->on_answer (q);
      assert (in_ptr == in_end);
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
  timer_tree = tree_insert_timer (timer_tree, ev, lrand48 ());
}

void remove_event_timer (struct event_timer *ev) {
  if (verbosity > 2) {
    logprintf ( "REMOVE: %lf %p %p\n", ev->timeout, ev->self, ev->alarm);
  }
  timer_tree = tree_delete_timer (timer_tree, ev);
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
    assert (ev->alarm);
    if (verbosity) {
      logprintf ("Alarm\n");
    }
    ev->alarm (ev->self);
  }
}

int max_chat_size;
int want_dc_num;
int new_dc_num;
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
      new_dc_num ++;
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

void do_help_get_config (void) {
  clear_packet ();  
  out_int (CODE_help_get_config);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &help_get_config_methods, 0);
}

char *phone_code_hash;
int send_code_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_auth_sent_code);
  assert (fetch_bool ());
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
  out_string ("en");

  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_code_methods, 0);
  net_loop (0, code_is_sent);
  if (want_dc_num == -1) { return; }

  DC_working = DC_list[want_dc_num];
  if (!DC_working->sessions[0]) {
    dc_create_session (DC_working);
  }
  dc_working_num = want_dc_num;
  want_dc_num = 0;
  clear_packet ();
  out_int (CODE_auth_send_code);
  out_string (user);
  out_int (0);
  out_int (TG_APP_ID);
  out_string (TG_APP_HASH);
  out_string ("en");

  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_code_methods, 0);
  net_loop (0, code_is_sent);
  assert (want_dc_num == -1);
}


int check_phone_result;
int cr_f (void) {
  return check_phone_result >= 0;
}

int check_phone_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_auth_checked_phone);
  check_phone_result = fetch_bool ();
  fetch_bool ();
  return 0;
}

int check_phone_on_error (struct query *q UU, int error_code, int l, char *error) {
  int s = strlen ("PHONE_MIGRATE_");
  int s2 = strlen ("NETWORK_MIGRATE_");
  if (l >= s && !memcmp (error, "PHONE_MIGRATE_", s)) {
    int i = error[s] - '0';
    assert (DC_list[i]);
    dc_working_num = i;
    DC_working = DC_list[i];
    write_auth_file ();
    check_phone_result = 1;
  } else if (l >= s2 && !memcmp (error, "NETWORK_MIGRATE_", s)) {
    int i = error[s2] - '0';
    assert (DC_list[i]);
    dc_working_num = i;
    DC_working = DC_list[i];
    write_auth_file ();
    check_phone_result = 1;
  } else {
    logprintf ( "error_code = %d, error = %.*s\n", error_code, l, error);
    assert (0);
  }
  return 0;
}

struct query_methods check_phone_methods = {
  .on_answer = check_phone_on_answer,
  .on_error = check_phone_on_error
};

int do_auth_check_phone (const char *user) {
  suser = strdup (user);
  clear_packet ();
  out_int (CODE_auth_check_phone);
  out_string (user);
  check_phone_result = -1;
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &check_phone_methods, 0);
  net_loop (0, cr_f);
  return check_phone_result;
}

int nearest_dc_num;
int nr_f (void) {
  return nearest_dc_num >= 0;
}

int nearest_dc_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_nearest_dc);
  char *country = fetch_str_dup ();
  if (verbosity > 0) {
    logprintf ("Server thinks that you are in %s\n", country);
  }
  fetch_int (); // this_dc
  nearest_dc_num = fetch_int ();
  assert (nearest_dc_num >= 0);
  return 0;
}

int fail_on_error (struct query *q UU, int error_code UU, int l UU, char *error UU) {
  fprintf (stderr, "error #%d: %.*s\n", error_code, l, error);
  assert (0);
  return 0;
}

struct query_methods nearest_dc_methods = {
  .on_answer = nearest_dc_on_answer,
  .on_error = fail_on_error
};

int do_get_nearest_dc (void) {
  clear_packet ();
  out_int (CODE_help_get_nearest_dc);
  nearest_dc_num = -1;
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &nearest_dc_methods, 0);
  net_loop (0, nr_f);
  return nearest_dc_num;
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
  DC_working->has_auth = 1;
  return 0;
}

int sign_in_on_error (struct query *q UU, int error_code, int l, char *error) {
  logprintf ( "error_code = %d, error = %.*s\n", error_code, l, error);
  sign_in_ok = -1;
  assert (0);
  return 0;
}

struct query_methods sign_in_methods  = {
  .on_answer = sign_in_on_answer,
  .on_error = sign_in_on_error
};

int do_send_code_result (const char *code) {
  clear_packet ();
  out_int (CODE_auth_sign_in);
  out_string (suser);
  out_string (phone_code_hash);
  out_string (code);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &sign_in_methods, 0);
  sign_in_ok = 0;
  net_loop (0, sign_in_is_ok);
  return sign_in_ok;
}

int do_send_code_result_auth (const char *code, const char *first_name, const char *last_name) {
  clear_packet ();
  out_int (CODE_auth_sign_up);
  out_string (suser);
  out_string (phone_code_hash);
  out_string (code);
  out_string (first_name);
  out_string (last_name);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &sign_in_methods, 0);
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
    print_start ();
    push_color (COLOR_YELLOW);
    printf ("User #%d: ", get_peer_id (U->id));
    print_user_name (U->id, (peer_t *)U);
    push_color (COLOR_GREEN);
    printf (" (");
    printf ("%s", U->print_name);
    if (U->phone) {
      printf (" ");
      printf ("%s", U->phone);
    }
    printf (") ");
    pop_color ();
    if (U->status.online > 0) {
      printf ("online\n");
    } else {
      if (U->status.online < 0) {
        printf ("offline. Was online ");
        print_date_full (U->status.when);
      } else {
        printf ("offline permanent");
      }
      printf ("\n");
    }
    pop_color ();
    print_end ();
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
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &get_contacts_methods, 0);
}


int msg_send_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_messages_sent_message);
  int id = fetch_int (); // id
  int date = fetch_int (); // date
  fetch_pts ();
  int seq = fetch_int (); // seq
  struct message *M = q->extra;
  M->id = id;
  message_insert (M);
  logprintf ("Sent: id = %d, date = %d, seq = %d\n", id, date, seq);
  return 0;
}

struct query_methods msg_send_methods = {
  .on_answer = msg_send_on_answer
};

int out_message_num;
int our_id;
void out_peer_id (peer_id_t id);
void do_send_message (peer_id_t id, const char *msg, int len) {
  if (!out_message_num) {
    out_message_num = -lrand48 ();
  }
  clear_packet ();
  out_int (CODE_messages_send_message);
  struct message *M = malloc (sizeof (*M));
  memset (M, 0, sizeof (*M));
  M->from_id = MK_USER (our_id);
  M->to_id = id;
  M->unread = 1;
  out_peer_id (id);
  M->message = malloc (len + 1);
  memcpy (M->message, msg, len);
  M->message[len] = 0;
  M->message_len = len;
  M->out = 1;
  M->media.type = CODE_message_media_empty;
  M->id = out_message_num;
  M->date = time (0);
  out_cstring (msg, len);
  out_long ((--out_message_num) - (1ll << 32));
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &msg_send_methods, M);
  print_message (M);
}

void do_send_text (peer_id_t id, char *file_name) {
  int fd = open (file_name, O_RDONLY);
  if (fd < 0) {
    rprintf ("No such file '%s'\n", file_name);
    free (file_name);
    return;
  }
  static char buf[(1 << 20) + 1];
  int x = read (fd, buf, (1 << 20) + 1);
  assert (x >= 0);
  if (x == (1 << 20) + 1) {
    rprintf ("Too big file '%s'\n", file_name);
    free (file_name);
    close (fd);
  } else {
    buf[x] = 0;
    do_send_message (id, buf, x);
    free (file_name);
    close (fd);
  }
}

int mark_read_on_receive (struct query *q UU) {
  assert (fetch_int () == (int)CODE_messages_affected_history);
  fetch_int (); // pts
  fetch_int (); // seq
  fetch_int (); // offset
  return 0;
}

struct query_methods mark_read_methods = {
  .on_answer = mark_read_on_receive
};

void do_messages_mark_read (peer_id_t id, int max_id) {
  clear_packet ();
  out_int (CODE_messages_read_history);
  out_peer_id (id);
  out_int (max_id);
  out_int (0);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &mark_read_methods, 0);
}

int get_history_on_answer (struct query *q UU) {
  static struct message *ML[10000];
  int i;
  int x = fetch_int ();
  if (x == (int)CODE_messages_messages_slice) {
    fetch_int ();
    rprintf ("...\n");
  } else {
    assert (x == (int)CODE_messages_messages);
  }
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  for (i = 0; i < n; i++) {
    struct message *M = fetch_alloc_message ();
    if (i <= 9999) {
      ML[i] = M;
    }
  }
  if (n > 10000) { n = 10000; }
  int sn = n;
  for (i = n - 1; i >= 0; i--) {
    print_message (ML[i]);
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_chat ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_user ();
  }
  if (sn > 0 && q->extra) {
    do_messages_mark_read (*(peer_id_t *)&(q->extra), ML[0]->id);
  }
  return 0;
}

struct query_methods get_history_methods = {
  .on_answer = get_history_on_answer,
};


void do_get_history (peer_id_t id, int limit) {
  clear_packet ();
  out_int (CODE_messages_get_history);
  out_peer_id (id);
  out_int (0);
  out_int (0);
  out_int (limit);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &get_history_methods, (void *)*(long *)&id);
}

int get_dialogs_on_answer (struct query *q UU) {
  unsigned x = fetch_int (); 
  assert (x == CODE_messages_dialogs || x == CODE_messages_dialogs_slice);
  if (x == CODE_messages_dialogs_slice) {
    fetch_int (); // total_count
  }
  assert (fetch_int () == CODE_vector);
  int n, i;
  n = fetch_int ();
  static int dlist[2 * 100];
  static peer_id_t plist[100];
  int dl_size = n;
  for (i = 0; i < n; i++) {
    assert (fetch_int () == CODE_dialog);
    if (i < 100) {
      plist[i] = fetch_peer_id ();
      dlist[2 * i + 0] = fetch_int ();
      dlist[2 * i + 1] = fetch_int ();
    } else {
      fetch_peer_id ();
      fetch_int ();
      fetch_int ();
    }
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_message ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_chat ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_user ();
  }
  print_start ();
  push_color (COLOR_YELLOW);
  for (i = dl_size - 1; i >= 0; i--) {
    peer_t *UC;
    switch (get_peer_type (plist[i])) {
    case PEER_USER:
      UC = user_chat_get (plist[i]);
      printf ("User ");
      print_user_name (plist[i], UC);
      printf (": %d unread\n", dlist[2 * i + 1]);
      break;
    case PEER_CHAT:
      UC = user_chat_get (plist[i]);
      printf ("Chat ");
      print_chat_name (plist[i], UC);
      printf (": %d unread\n", dlist[2 * i + 1]);
      break;
    }
  }
  pop_color ();
  print_end ();
  return 0;
}

struct query_methods get_dialogs_methods = {
  .on_answer = get_dialogs_on_answer,
};


void do_get_dialog_list (void) {
  clear_packet ();
  out_int (CODE_messages_get_dialogs);
  out_int (0);
  out_int (0);
  out_int (1000);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &get_dialogs_methods, 0);
}

struct send_file {
  int fd;
  long long size;
  long long offset;
  int part_num;
  int part_size;
  long long id;
  peer_id_t to_id;
  int media_type;
  char *file_name;
};

void out_peer_id (peer_id_t id) {
  peer_t *U;
  switch (get_peer_type (id)) {
  case PEER_CHAT:
    out_int (CODE_input_peer_chat);
    out_int (get_peer_id (id));
    break;
  case PEER_USER:
    U = user_chat_get (id);
    if (U && U->user.access_hash) {
      out_int (CODE_input_peer_foreign);
      out_int (get_peer_id (id));
      out_long (U->user.access_hash);
    } else {
      out_int (CODE_input_peer_contact);
      out_int (get_peer_id (id));
    }
    break;
  default:
    assert (0);
  }
}

void send_part (struct send_file *f);
int send_file_part_on_answer (struct query *q) {
  assert (fetch_int () == (int)CODE_bool_true);
  send_part (q->extra);
  return 0;
}

int send_file_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_messages_stated_message);
  struct message *M = fetch_alloc_message ();
  assert (fetch_int () == CODE_vector);
  int n, i;
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_chat ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_user ();
  }
  fetch_int (); // pts
  fetch_int (); // seq
  print_message (M);
  return 0;
}

struct query_methods send_file_part_methods = {
  .on_answer = send_file_part_on_answer
};

struct query_methods send_file_methods = {
  .on_answer = send_file_on_answer
};

void send_part (struct send_file *f) {
  if (f->fd >= 0) {
    if (!f->part_num) {
      cur_uploading_bytes += f->size;
    }
    clear_packet ();
    out_int (CODE_upload_save_file_part);
    out_long (f->id);
    out_int (f->part_num ++);
    static char buf[512 << 10];
    int x = read (f->fd, buf, f->part_size);
    assert (x > 0);
    out_cstring (buf, x);
    f->offset += x;
    cur_uploaded_bytes += x;
    if (verbosity >= 2) {
      logprintf ("offset=%lld size=%lld\n", f->offset, f->size);
    }
    if (f->offset == f->size) {
      close (f->fd);
      f->fd = -1;
    }
    send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_file_part_methods, f);
  } else {
    cur_uploaded_bytes -= f->size;
    cur_uploading_bytes -= f->size;
    clear_packet ();
    out_int (CODE_messages_send_media);
    out_peer_id (f->to_id);
    assert (f->media_type == CODE_input_media_uploaded_photo || f->media_type == CODE_input_media_uploaded_video);
    out_int (f->media_type);
    out_int (CODE_input_file);
    out_long (f->id);
    out_int (f->part_num);
    char *s = f->file_name + strlen (f->file_name);
    while (s >= f->file_name && *s != '/') { s --;}
    out_string (s + 1);
    out_string ("");
    if (f->media_type == CODE_input_media_uploaded_video) {
      out_int (100);
      out_int (100);
      out_int (100);
    }
    out_long (-lrand48 () * (1ll << 32) - lrand48 ());
    send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_file_methods, 0);
    free (f->file_name);
    free (f);
  }
}

void do_send_photo (int type, peer_id_t to_id, char *file_name) {
  int fd = open (file_name, O_RDONLY);
  if (fd < 0) {
    rprintf ("No such file '%s'\n", file_name);
    return;
  }
  struct stat buf;
  fstat (fd, &buf);
  long long size = buf.st_size;
  if (size <= 0) {
    rprintf ("File has zero length\n");
    close (fd);
    return;
  }
  struct send_file *f = malloc (sizeof (*f));
  f->fd = fd;
  f->size = size;
  f->offset = 0;
  f->part_num = 0;
  f->part_size = ((size + 999) / 1000 + 0x3ff) & ~0x3ff;
  f->id = lrand48 () * (1ll << 32) + lrand48 ();
  f->to_id = to_id;
  f->media_type = type;
  f->file_name = file_name;
  if (f->part_size > (512 << 10)) {
    close (fd);
    rprintf ("Too big file. Maximal supported size is %d", (512 << 10) * 1000);
    return;
  }
  send_part (f);
}

int fwd_msg_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_messages_stated_message);
  struct message *M = fetch_alloc_message ();
  assert (fetch_int () == CODE_vector);
  int n, i;
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_chat ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_user ();
  }
  fetch_int (); // pts
  fetch_int (); // seq
  print_message (M);
  return 0;
}

struct query_methods fwd_msg_methods = {
  .on_answer = fwd_msg_on_answer
};

void do_forward_message (peer_id_t id, int n) {
  clear_packet ();
  out_int (CODE_invoke_with_layer3);
  out_int (CODE_messages_forward_message);
  out_peer_id (id);
  out_int (n);
  out_long (lrand48 () * (1ll << 32) + lrand48 ());
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &fwd_msg_methods, 0);
}

int rename_chat_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_messages_stated_message);
  struct message *M = fetch_alloc_message ();
  assert (fetch_int () == CODE_vector);
  int n, i;
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_chat ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_user ();
  }
  fetch_int (); // pts
  fetch_int (); // seq
  print_message (M);
  return 0;
}

struct query_methods rename_chat_methods = {
  .on_answer = rename_chat_on_answer
};

void do_rename_chat (peer_id_t id, char *name) {
  clear_packet ();
  out_int (CODE_messages_edit_chat_title);
  assert (get_peer_type (id) == PEER_CHAT);
  out_int (get_peer_id (id));
  out_string (name);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &rename_chat_methods, 0);
}

int chat_info_on_answer (struct query *q UU) {
  struct chat *C = fetch_alloc_chat_full ();
  peer_t *U = (void *)C;
  print_start ();
  push_color (COLOR_YELLOW);
  printf ("Chat ");
  print_chat_name (U->id, U);
  printf (" members:\n");
  int i;
  for (i = 0; i < C->users_num; i++) {
    printf ("\t\t");
    print_user_name (MK_USER (C->users[i].user_id), user_chat_get (MK_USER (C->users[i].user_id)));
    printf (" invited by ");
    print_user_name (MK_USER (C->users[i].inviter_id), user_chat_get (MK_USER (C->users[i].inviter_id)));
    printf (" at ");
    print_date_full (C->users[i].date);
    if (C->users[i].user_id == C->admin_id) {
      printf (" admin");
    }
    printf ("\n");
  }
  pop_color ();
  print_end ();
  return 0;
}

struct query_methods chat_info_methods = {
  .on_answer = chat_info_on_answer
};

void do_get_chat_info (peer_id_t id) {
  clear_packet ();
  out_int (CODE_messages_get_full_chat);
  assert (get_peer_type (id) == PEER_CHAT);
  out_int (get_peer_id (id));
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &chat_info_methods, 0);
}

int user_info_on_answer (struct query *q UU) {
  struct user *U = fetch_alloc_user_full ();
  peer_t *C = (void *)U;
  print_start ();
  push_color (COLOR_YELLOW);
  printf ("User ");
  print_user_name (U->id, C);
  printf (":\n");
  printf ("\treal name: %s %s\n", U->real_first_name, U->real_last_name);
  printf ("\tphone: %s\n", U->phone);
  if (U->status.online > 0) {
    printf ("\tonline\n");
  } else {
    printf ("\toffline (was online ");
    print_date_full (U->status.when);
    printf (")\n");
  }
  pop_color ();
  print_end ();
  return 0;
}

struct query_methods user_info_methods = {
  .on_answer = user_info_on_answer
};

void do_get_user_info (peer_id_t id) {
  clear_packet ();
  out_int (CODE_users_get_full_user);
  assert (get_peer_type (id) == PEER_USER);
  peer_t *U = user_chat_get (id);
  if (U && U->user.access_hash) {
    out_int (CODE_input_user_foreign);
    out_int (get_peer_id (id));
    out_long (U->user.access_hash);
  } else {
    out_int (CODE_input_user_contact);
    out_int (get_peer_id (id));
  }
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &user_info_methods, 0);
}

int user_list_info_silent_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  int i;
  for (i = 0; i < n; i++) {
    fetch_alloc_user ();
  }
  return 0;
}

struct query_methods user_list_info_silent_methods = {
  .on_answer = user_list_info_silent_on_answer
};

void do_get_user_list_info_silent (int num, int *list) {
  clear_packet ();
  out_int (CODE_users_get_users);
  out_int (CODE_vector);
  out_int (num);
  int i;
  for (i = 0; i < num; i++) {
    out_int (CODE_input_user_contact);
    out_int (list[i]);
    //out_long (0);
  }
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &user_list_info_silent_methods, 0);
}

struct download {
  int offset;
  int size;
  long long volume;
  long long secret;
  long long access_hash;
  int local_id;
  int dc;
  int next;
  int fd;
  char *name;
  long long id;
};


void end_load (struct download *D) {
  cur_downloading_bytes -= D->size;
  cur_downloaded_bytes -= D->size;
  update_prompt ();
  close (D->fd);
  if (D->next == 1) {
    logprintf ("Done: %s\n", D->name);
  } else if (D->next == 2) {
    static char buf[1000];
    sprintf (buf, "xdg-open %s", D->name);
    int x = system (buf);
    if (x < 0) {
      logprintf ("Can not open image viewer: %m\n");
      logprintf ("Image is at %s\n", D->name);
    }
  }
  free (D->name);
  free (D);
}

void load_next_part (struct download *D);
int download_on_answer (struct query *q) {
  assert (fetch_int () == (int)CODE_upload_file);
  unsigned x = fetch_int ();
  struct download *D = q->extra;
  if (D->fd == -1) {
    static char buf[100];
    sprintf (buf, "%s/tmp_%ld_%ld", get_downloads_directory (), lrand48 (), lrand48 ());
    int l = strlen (buf);
    switch (x) {
    case CODE_storage_file_unknown:
      break;
    case CODE_storage_file_jpeg:
      sprintf (buf + l, "%s", ".jpg");
      break;
    case CODE_storage_file_gif:
      sprintf (buf + l, "%s", ".gif");
      break;
    case CODE_storage_file_png:
      sprintf (buf + l, "%s", ".png");
      break;
    case CODE_storage_file_mp3:
      sprintf (buf + l, "%s", ".mp3");
      break;
    case CODE_storage_file_mov:
      sprintf (buf + l, "%s", ".mov");
      break;
    case CODE_storage_file_partial:
      sprintf (buf + l, "%s", ".part");
      break;
    case CODE_storage_file_mp4:
      sprintf (buf + l, "%s", ".mp4");
      break;
    case CODE_storage_file_webp:
      sprintf (buf + l, "%s", ".webp");
      break;
    }
    D->name = strdup (buf);
    D->fd = open (D->name, O_CREAT | O_WRONLY, 0640);
  }
  fetch_int (); // mtime
  int len = prefetch_strlen ();
  assert (len >= 0);
  cur_downloaded_bytes += len;
  update_prompt ();
  assert (write (D->fd, fetch_str (len), len) == len);
  D->offset += len;
  if (D->offset < D->size) {
    load_next_part (D);
    return 0;
  } else {
    end_load (D);
    return 0;
  }
}

struct query_methods download_methods = {
  .on_answer = download_on_answer
};

void load_next_part (struct download *D) {
  if (!D->offset) {
    cur_downloading_bytes += D->size;
    update_prompt ();
  }
  clear_packet ();
  out_int (CODE_upload_get_file);
  if (!D->id) {
    out_int (CODE_input_file_location);
    out_long (D->volume);
    out_int (D->local_id);
    out_long (D->secret);
  } else {
    out_int (CODE_input_video_file_location);
    out_long (D->id);
    out_long (D->access_hash);
  }
  out_int (D->offset);
  out_int (1 << 14);
  send_query (DC_list[D->dc], packet_ptr - packet_buffer, packet_buffer, &download_methods, D);
  //send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &download_methods, D);
}

void do_load_photo_size (struct photo_size *P, int next) {
  assert (P);
  assert (next);
  struct download *D = malloc (sizeof (*D));
  D->id = 0;
  D->offset = 0;
  D->size = P->size;
  D->volume = P->loc.volume;
  D->dc = P->loc.dc;
  D->local_id = P->loc.local_id;
  D->secret = P->loc.secret;
  D->next = next;
  D->name = 0;
  D->fd = -1;
  load_next_part (D);
}

void do_load_photo (struct photo *photo, int next) {
  if (!photo->sizes_num) { return; }
  int max = -1;
  int maxi = 0;
  int i;
  for (i = 0; i < photo->sizes_num; i++) {
    if (photo->sizes[i].w + photo->sizes[i].h > max) {
      max = photo->sizes[i].w + photo->sizes[i].h;
      maxi = i;
    }
  }
  do_load_photo_size (&photo->sizes[maxi], next);
}

void do_load_video_thumb (struct video *video, int next) {
  do_load_photo_size (&video->thumb, next);
}

void do_load_video (struct video *V, int next) {
  assert (V);
  assert (next);
  struct download *D = malloc (sizeof (*D));
  D->offset = 0;
  D->size = V->size;
  D->id = V->id;
  D->access_hash = V->access_hash;
  D->dc = V->dc_id;
  D->next = next;
  D->name = 0;
  D->fd = -1;
  load_next_part (D);
}

char *export_auth_str;
int export_auth_str_len;
int is_export_auth_str (void) {
  return export_auth_str != 0;
}
int isn_export_auth_str (void) {
  return export_auth_str == 0;
}

int export_auth_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_auth_exported_authorization);
  int l = fetch_int ();
  if (!our_id) {
    our_id = l;
  } else {
    assert (our_id == l);
  }
  l = prefetch_strlen ();
  char *s = malloc (l);
  memcpy (s, fetch_str (l), l);
  export_auth_str_len = l;
  export_auth_str = s;
  return 0;
}

struct query_methods export_auth_methods = {
  .on_answer = export_auth_on_answer,
  .on_error = fail_on_error
};

void do_export_auth (int num) {
  export_auth_str = 0;
  clear_packet ();
  out_int (CODE_auth_export_authorization);
  out_int (num);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &export_auth_methods, 0);
  net_loop (0, is_export_auth_str);
}

int import_auth_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_auth_authorization);
  fetch_int (); // expires
  fetch_alloc_user ();
  free (export_auth_str);
  export_auth_str = 0;
  return 0;
}

struct query_methods import_auth_methods = {
  .on_answer = import_auth_on_answer,
  .on_error = fail_on_error
};

void do_import_auth (int num) {
  clear_packet ();
  out_int (CODE_auth_import_authorization);
  out_int (our_id);
  out_cstring (export_auth_str, export_auth_str_len);
  send_query (DC_list[num], packet_ptr - packet_buffer, packet_buffer, &import_auth_methods, 0);
  net_loop (0, isn_export_auth_str);
}

int add_contact_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_contacts_imported_contacts);
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  if (n > 0) {
    logprintf ("Added successfully");
  } else {
    logprintf ("Not added");
  }
  int i;
  for (i = 0; i < n ; i++) {
    assert (fetch_int () == (int)CODE_imported_contact);
    fetch_int (); // uid
    fetch_long (); // client_id
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n ; i++) {
    struct user *U = fetch_alloc_user ();
    print_start ();
    push_color (COLOR_YELLOW);
    printf ("User #%d: ", get_peer_id (U->id));
    print_user_name (U->id, (peer_t *)U);
    push_color (COLOR_GREEN);
    printf (" (");
    printf ("%s", U->print_name);
    if (U->phone) {
      printf (" ");
      printf ("%s", U->phone);
    }
    printf (") ");
    pop_color ();
    if (U->status.online > 0) {
      printf ("online\n");
    } else {
      if (U->status.online < 0) {
        printf ("offline. Was online ");
        print_date_full (U->status.when);
      } else {
        printf ("offline permanent");
      }
      printf ("\n");
    }
    pop_color ();
    print_end ();

  }
  return 0;
}

struct query_methods add_contact_methods = {
  .on_answer = add_contact_on_answer,
};

void do_add_contact (const char *phone, int phone_len, const char *first_name, int first_name_len, const char *last_name, int last_name_len, int force) {
  clear_packet ();
  out_int (CODE_contacts_import_contacts);
  out_int (CODE_vector);
  out_int (1);
  out_int (CODE_input_phone_contact);
  out_long (lrand48 () * (1ll << 32) + lrand48 ());
  out_cstring (phone, phone_len);
  out_cstring (first_name, first_name_len);
  out_cstring (last_name, last_name_len);
  out_int (force ? CODE_bool_true : CODE_bool_false);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &add_contact_methods, 0);
}

int msg_search_on_answer (struct query *q UU) {
  return get_history_on_answer (q);
}

struct query_methods msg_search_methods = {
  .on_answer = msg_search_on_answer
};

void do_msg_search (peer_id_t id, int from, int to, int limit, const char *s) {
  clear_packet ();
  out_int (CODE_messages_search);
  out_peer_id (id);
  out_string (s);
  out_int (CODE_input_messages_filter_empty);
  out_int (from);
  out_int (to);
  out_int (0); // offset
  out_int (0); // max_id
  out_int (limit);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &msg_search_methods, 0);
}
