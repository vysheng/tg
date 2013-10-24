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
  if (verbosity) {
    logprintf ("Alarm query %lld\n", q->msg_id);
  }
  tree_delete_query (queries_tree, q);
  q->ev.timeout = get_double_time () + QUERY_TIMEOUT;
  insert_event_timer (&q->ev);

  clear_packet ();
  out_int (CODE_msg_container);
  out_long (q->msg_id);
  out_int (q->seq_no);
  out_int (4 * q->data_len);
  out_ints (q->data, q->data_len);
  
  encrypt_send_message (q->session->c, packet_buffer, packet_ptr - packet_buffer, 0);
  return 0;
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
  } else {
    if (!(q->flags & QUERY_ACK_RECEIVED)) {
      remove_event_timer (&q->ev);
    }
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

  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_code_methods, 0);
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
    send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &help_get_config_methods, 0);
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

  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_code_methods, 0);
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
  out_int (CODE_auth_sign_in);
  out_string (suser);
  out_string (phone_code_hash);
  out_string (code);
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
    printf ("User #%d: ", U->id);
    print_user_name (U->id, (union user_chat *)U);
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
  int ptr = fetch_int (); // ptr
  int seq = fetch_int (); // seq
  struct message *M = q->extra;
  M->id = id;
  message_insert (M);
  logprintf ("Sent: id = %d, date = %d, ptr = %d, seq = %d\n", id, date, ptr, seq);
  return 0;
}

struct query_methods msg_send_methods = {
  .on_answer = msg_send_on_answer
};

int out_message_num;
int our_id;
void do_send_message (union user_chat *U, const char *msg, int len) {
  if (!out_message_num) {
    out_message_num = -lrand48 ();
  }
  clear_packet ();
  out_int (CODE_messages_send_message);
  struct message *M = malloc (sizeof (*M));
  memset (M, 0, sizeof (*M));
  M->from_id = our_id;
  M->to_id = U->id;
  M->unread = 1;
  if (U->id < 0) {
    out_int (CODE_input_peer_chat);
    out_int (-U->id);
  } else {
    if (U->user.access_hash) {
      out_int (CODE_input_peer_foreign);
      out_int (U->id);
      out_long (U->user.access_hash);
    } else {
      out_int (CODE_input_peer_contact);
      out_int (U->id);
    }
  }
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

void do_send_text (union user_chat *U, char *file_name) {
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
    do_send_message (U, buf, x);
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

void do_messages_mark_read (union user_chat *U, int max_id) {
  clear_packet ();
  out_int (CODE_messages_read_history);
  if (U->id < 0) {
    out_int (CODE_input_peer_chat);
    out_int (-U->id);
  } else {
    if (U->user.access_hash) {
      out_int (CODE_input_peer_foreign);
      out_int (U->id);
      out_long (U->user.access_hash);
    } else {
      out_int (CODE_input_peer_contact);
      out_int (U->id);
    }
  }
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
  if (sn > 0) {
    do_messages_mark_read (q->extra, ML[0]->id);
  }
  return 0;
}

struct query_methods get_history_methods = {
  .on_answer = get_history_on_answer,
};


void do_get_history (union user_chat *U, int limit) {
  clear_packet ();
  out_int (CODE_messages_get_history);
  if (U->id < 0) {
    out_int (CODE_input_peer_chat);
    out_int (-U->id);
  } else {
    if (U->user.access_hash) {
      out_int (CODE_input_peer_foreign);
      out_int (U->id);
      out_long (U->user.access_hash);
    } else {
      out_int (CODE_input_peer_contact);
      out_int (U->id);
    }
  }
  out_int (0);
  out_int (0);
  out_int (limit);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &get_history_methods, U);
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
  static int dlist[3 * 100];
  int dl_size = n;
  for (i = 0; i < n; i++) {
    assert (fetch_int () == CODE_dialog);
    if (i < 100) {
      dlist[3 * i + 0] = fetch_peer_id ();
      dlist[3 * i + 1] = fetch_int ();
      dlist[3 * i + 2] = fetch_int ();
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
    if (dlist[3 * i] < 0) {
      union user_chat *UC = user_chat_get (dlist[3 * i]);
      printf ("Chat ");
      print_chat_name (dlist[3 * i], UC);
      printf (": %d unread\n", dlist[3 * i + 2]);
    } else {
      union user_chat *UC = user_chat_get (dlist[3 * i]);
      printf ("User ");
      print_user_name (dlist[3 * i], UC);
      printf (": %d unread\n", dlist[3 * i + 2]);
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
  int to_id;
  int media_type;
  char *file_name;
};

void out_peer_id (int id) {
  union user_chat *U = user_chat_get (id);
  if (id < 0) {
    out_int (CODE_input_peer_chat);
    out_int (-id);
  } else {
    if (U && U->user.access_hash) {
      out_int (CODE_input_peer_foreign);
      out_int (id);
      out_long (U->user.access_hash);
    } else {
      out_int (CODE_input_peer_contact);
      out_int (id);
    }
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
    clear_packet ();
    out_int (CODE_upload_save_file_part);
    out_long (f->id);
    out_int (f->part_num ++);
    static char buf[512 << 10];
    int x = read (f->fd, buf, f->part_size);
    assert (x > 0);
    out_cstring (buf, x);
    f->offset += x;
    if (verbosity >= 2) {
      logprintf ("offset=%lld size=%lld\n", f->offset, f->size);
    }
    if (f->offset == f->size) {
      close (f->fd);
      f->fd = -1;
    }
    send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_file_part_methods, f);
  } else {
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

void do_send_photo (int type, int to_id, char *file_name) {
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

int chat_info_on_answer (struct query *q UU) {
  struct chat *C = fetch_alloc_chat_full ();
  union user_chat *U = (void *)C;
  print_start ();
  push_color (COLOR_YELLOW);
  printf ("Chat ");
  print_chat_name (U->id, U);
  printf (" members:\n");
  int i;
  for (i = 0; i < C->users_num; i++) {
    printf ("\t\t");
    print_user_name (C->users[i].user_id, user_chat_get (C->users[i].user_id));
    printf (" invited by ");
    print_user_name (C->users[i].inviter_id, user_chat_get (C->users[i].inviter_id));
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

void do_get_chat_info (union user_chat *chat) {
  clear_packet ();
  out_int (CODE_messages_get_full_chat);
  out_int (-chat->id);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &chat_info_methods, 0);
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
