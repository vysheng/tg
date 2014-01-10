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
#include <sys/utsname.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "include.h"
#include "mtproto-client.h"
#include "queries.h"
#include "tree.h"
#include "mtproto-common.h"
#include "telegram.h"
#include "loop.h"
#include "structures.h"
#include "interface.h"
#include "net.h"
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

#include "no-preview.h"
#include "binlog.h"

#define sha1 SHA1

#ifdef __APPLE__
#define OPEN_BIN "open %s"
#else
#define OPEN_BIN "xdg-open %s"
#endif

char *get_downloads_directory (void);
int verbosity;
extern int offline_mode;

long long cur_uploading_bytes;
long long cur_uploaded_bytes;
long long cur_downloading_bytes;
long long cur_downloaded_bytes;

extern int binlog_enabled;
extern int sync_from_start;

int queries_num;

void out_peer_id (peer_id_t id);
#define QUERY_TIMEOUT 6.0

#define memcmp8(a,b) memcmp ((a), (b), 8)
DEFINE_TREE (query, struct query *, memcmp8, 0) ;
struct tree_query *queries_tree;

double get_double_time (void) {
  struct timespec tv;
  my_clock_gettime (CLOCK_REALTIME, &tv);
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

  if (q->session->c->out_bytes >= 100000) {
    return 0;
  }

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
  struct query *q = talloc0 (sizeof (*q));
  q->data_len = ints;
  q->data = talloc (4 * ints);
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
  queries_num ++;
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
    tfree (q->data, q->data_len * 4);
    tfree (q, sizeof (*q));
  }
  queries_num --;
}

#define MAX_PACKED_SIZE (1 << 24)
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
    tfree (q->data, 4 * q->data_len);
    tfree (q, sizeof (*q));
  }
  if (end) {
    in_ptr = end;
    in_end = eend;
  }
  queries_num --;
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

void out_random (int n) {
  assert (n <= 32);
  static char buf[32];
  secure_random (buf, n);
  out_cstring (buf, n);
}

int allow_send_linux_version;
void do_insert_header (void) {
  out_int (CODE_invoke_with_layer11);  
  out_int (CODE_init_connection);
  out_int (TG_APP_ID);
  if (allow_send_linux_version) {
    struct utsname st;
    uname (&st);
    out_string (st.machine);
    static char buf[65536];
    tsnprintf (buf, 65535, "%999s %999s %999s", st.sysname, st.release, st.version);
    out_string (buf);
    out_string (TG_VERSION " (build " TG_BUILD ")");
    out_string ("En");
  } else { 
    out_string ("x86");
    out_string ("Linux");
    out_string (TG_VERSION);
    out_string ("en");
  }
}

/* {{{ Get config */

void fetch_dc_option (void) {
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

  bl_do_dc_option (id, l1, name, l2, ip, port);
}

int help_get_config_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_config);
  fetch_int ();

  unsigned test_mode = fetch_int ();
  assert (test_mode == CODE_bool_true || test_mode == CODE_bool_false);
  assert (test_mode == CODE_bool_false || test_mode == CODE_bool_true);
  int this_dc = fetch_int ();
  if (verbosity) {
    logprintf ( "this_dc = %d\n", this_dc);
  }
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  assert (n <= 10);
  int i;
  for (i = 0; i < n; i++) {
    fetch_dc_option ();
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
/* }}} */

/* {{{ Send code */
char *phone_code_hash;
int send_code_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_auth_sent_code);
  fetch_bool ();
  int l = prefetch_strlen ();
  char *s = fetch_str (l);
  if (phone_code_hash) {
    tfree_str (phone_code_hash);
  }
  phone_code_hash = strndup (s, l);
  want_dc_num = -1;
  return 0;
}

int send_code_on_error (struct query *q UU, int error_code, int l, char *error) {
  int s = strlen ("PHONE_MIGRATE_");
  int s2 = strlen ("NETWORK_MIGRATE_");
  if (l >= s && !memcmp (error, "PHONE_MIGRATE_", s)) {
    int i = error[s] - '0';
    want_dc_num = i;
  } else if (l >= s2 && !memcmp (error, "NETWORK_MIGRATE_", s2)) {
    int i = error[s2] - '0';
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
  logprintf ("sending code\n");
  suser = tstrdup (user);
  want_dc_num = 0;
  clear_packet ();
  do_insert_header ();
  out_int (CODE_auth_send_code);
  out_string (user);
  out_int (0);
  out_int (TG_APP_ID);
  out_string (TG_APP_HASH);
  out_string ("en");

  logprintf ("send_code: dc_num = %d\n", dc_working_num);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_code_methods, 0);
  net_loop (0, code_is_sent);
  if (want_dc_num == -1) { return; }

  DC_working = DC_list[want_dc_num];
  if (!DC_working->sessions[0]) {
    dc_create_session (DC_working);
  }
  dc_working_num = want_dc_num;

  bl_do_set_working_dc (dc_working_num);

  logprintf ("send_code: dc_num = %d\n", dc_working_num);
  want_dc_num = 0;
  clear_packet ();
  do_insert_header ();
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
/* }}} */

/* {{{ Check phone */
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

    bl_do_set_working_dc (i);

    check_phone_result = 1;
  } else if (l >= s2 && !memcmp (error, "NETWORK_MIGRATE_", s2)) {
    int i = error[s2] - '0';
    assert (DC_list[i]);
    dc_working_num = i;

    bl_do_set_working_dc (i);

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
  suser = tstrdup (user);
  clear_packet ();
  out_int (CODE_auth_check_phone);
  out_string (user);
  check_phone_result = -1;
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &check_phone_methods, 0);
  net_loop (0, cr_f);
  return check_phone_result;
}
/* }}} */

/* {{{ Nearest DC */
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
/* }}} */

/* {{{ Sign in / Sign up */
int sign_in_ok;
int our_id;
int sign_in_is_ok (void) {
  return sign_in_ok;
}

struct user User;

int sign_in_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_auth_authorization);
  int expires = fetch_int ();
  fetch_user (&User);
  if (!our_id) {
    our_id = get_peer_id (User.id);
      
    bl_do_set_our_id (our_id);
  }
  sign_in_ok = 1;
  if (verbosity) {
    logprintf ( "authorized successfully: name = '%s %s', phone = '%s', expires = %d\n", User.first_name, User.last_name, User.phone, (int)(expires - get_double_time ()));
  }
  DC_working->has_auth = 1;

  bl_do_dc_signed (DC_working->id);

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
/* }}} */

/* {{{ Get contacts */
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
/* }}} */

/* {{{ Encrypt decrypted */
int *encr_extra;
int *encr_ptr;
int *encr_end;

char *encrypt_decrypted_message (struct secret_chat *E) {
  static int msg_key[4];
  static unsigned char sha1a_buffer[20];
  static unsigned char sha1b_buffer[20];
  static unsigned char sha1c_buffer[20];
  static unsigned char sha1d_buffer[20];
  int x = *(encr_ptr);  
  assert (x >= 0 && !(x & 3));
  sha1 ((void *)encr_ptr, 4 + x, sha1a_buffer);
  memcpy (msg_key, sha1a_buffer + 4, 16);
 
  static unsigned char buf[64];
  memcpy (buf, msg_key, 16);
  memcpy (buf + 16, E->key, 32);
  sha1 (buf, 48, sha1a_buffer);
  
  memcpy (buf, E->key + 8, 16);
  memcpy (buf + 16, msg_key, 16);
  memcpy (buf + 32, E->key + 12, 16);
  sha1 (buf, 48, sha1b_buffer);
  
  memcpy (buf, E->key + 16, 32);
  memcpy (buf + 32, msg_key, 16);
  sha1 (buf, 48, sha1c_buffer);
  
  memcpy (buf, msg_key, 16);
  memcpy (buf + 16, E->key + 24, 32);
  sha1 (buf, 48, sha1d_buffer);

  static unsigned char key[32];
  memcpy (key, sha1a_buffer + 0, 8);
  memcpy (key + 8, sha1b_buffer + 8, 12);
  memcpy (key + 20, sha1c_buffer + 4, 12);

  static unsigned char iv[32];
  memcpy (iv, sha1a_buffer + 8, 12);
  memcpy (iv + 12, sha1b_buffer + 0, 8);
  memcpy (iv + 20, sha1c_buffer + 16, 4);
  memcpy (iv + 24, sha1d_buffer + 0, 8);

  AES_KEY aes_key;
  AES_set_encrypt_key (key, 256, &aes_key);
  AES_ige_encrypt ((void *)encr_ptr, (void *)encr_ptr, 4 * (encr_end - encr_ptr), &aes_key, iv, 1);

  return (void *)msg_key;
}

void encr_start (void) {
  encr_extra = packet_ptr;
  packet_ptr += 1; // str len
  packet_ptr += 2; // fingerprint
  packet_ptr += 4; // msg_key
  packet_ptr += 1; // len
}


void encr_finish (struct secret_chat *E) {
  int l = packet_ptr - (encr_extra +  8);
  while (((packet_ptr - encr_extra) - 3) & 3) {  
    int t;
    secure_random (&t, 4);
    out_int (t);
  }

  *encr_extra = ((packet_ptr - encr_extra) - 1) * 4 * 256 + 0xfe;
  encr_extra ++;
  *(long long *)encr_extra = E->key_fingerprint;
  encr_extra += 2;
  encr_extra[4] = l * 4;
  encr_ptr = encr_extra + 4;
  encr_end = packet_ptr;
  memcpy (encr_extra, encrypt_decrypted_message (E), 16);
}
/* }}} */

/* {{{ Seng msg (plain text) */
int msg_send_encr_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_messages_sent_encrypted_message);
  rprintf ("Sent\n");
  struct message *M = q->extra;
  //M->date = fetch_int ();
  fetch_int ();
  bl_do_set_message_sent (M);
  return 0;
}

int msg_send_on_answer (struct query *q UU) {
  unsigned x = fetch_int ();
  assert (x == CODE_messages_sent_message || x == CODE_messages_sent_message_link);
  int id = fetch_int (); // id
  struct message *M = q->extra;
  bl_do_set_msg_id (M, id);
  fetch_date ();
  fetch_pts ();
  fetch_seq ();
  if (x == CODE_messages_sent_message_link) {
    assert (fetch_int () == CODE_vector);
    int n = fetch_int ();
    int i;
    unsigned a, b;
    for (i = 0; i < n; i++) {
      assert (fetch_int () == (int)CODE_contacts_link);
      a = fetch_int ();
      assert (a == CODE_contacts_my_link_empty || a == CODE_contacts_my_link_requested || a == CODE_contacts_my_link_contact);
      if (a == CODE_contacts_my_link_requested) {
        fetch_bool ();
      }
      b = fetch_int ();
      assert (b == CODE_contacts_foreign_link_unknown || b == CODE_contacts_foreign_link_requested || b == CODE_contacts_foreign_link_mutual);
      if (b == CODE_contacts_foreign_link_requested) {
        fetch_bool ();
      }
      struct user *U = fetch_alloc_user ();
  
      U->flags &= ~(FLAG_USER_IN_CONTACT | FLAG_USER_OUT_CONTACT);
      if (a == CODE_contacts_my_link_contact) {
        U->flags |= FLAG_USER_IN_CONTACT; 
      }
      U->flags &= ~(FLAG_USER_IN_CONTACT | FLAG_USER_OUT_CONTACT);
      if (b == CODE_contacts_foreign_link_mutual) {
        U->flags |= FLAG_USER_IN_CONTACT | FLAG_USER_OUT_CONTACT; 
      }
      if (b == CODE_contacts_foreign_link_requested) {
        U->flags |= FLAG_USER_OUT_CONTACT;
      }
      print_start ();
      push_color (COLOR_YELLOW);
      printf ("Link with user ");
      print_user_name (U->id, (void *)U);
      printf (" changed\n");
      pop_color ();
      print_end ();
    }
  }
  rprintf ("Sent: id = %d\n", id);
  bl_do_set_message_sent (M);
  return 0;
}

int msg_send_on_error (struct query *q, int error_code, int error_len, char *error) {
  logprintf ( "error for query #%lld: #%d :%.*s\n", q->msg_id, error_code, error_len, error);
  struct message *M = q->extra;
  bl_do_delete_msg (M);
  return 0;
}

struct query_methods msg_send_methods = {
  .on_answer = msg_send_on_answer,
  .on_error = msg_send_on_error
};

struct query_methods msg_send_encr_methods = {
  .on_answer = msg_send_encr_on_answer
};

int out_message_num;
int our_id;

void do_send_encr_msg (struct message *M) {
  peer_t *P = user_chat_get (M->to_id);
  if (!P || P->encr_chat.state != sc_ok) { return; }
  
  clear_packet ();
  out_int (CODE_messages_send_encrypted);
  out_int (CODE_input_encrypted_chat);
  out_int (get_peer_id (M->to_id));
  out_long (P->encr_chat.access_hash);
  out_long (M->id);
  encr_start ();
  out_int (CODE_decrypted_message);
  out_long (M->id);
  static int buf[4];
  secure_random (buf, 16);
  out_cstring ((void *)buf, 16);
  out_cstring ((void *)M->message, M->message_len);
  out_int (CODE_decrypted_message_media_empty);
  encr_finish (&P->encr_chat);
  
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &msg_send_encr_methods, M);
}

void do_send_msg (struct message *M) {
  if (get_peer_type (M->to_id) == PEER_ENCR_CHAT) {
    do_send_encr_msg (M);
    return;
  }
  clear_packet ();
  out_int (CODE_messages_send_message);
  out_peer_id (M->to_id);
  out_cstring (M->message, M->message_len);
  out_long (M->id);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &msg_send_methods, M);
}

void do_send_message (peer_id_t id, const char *msg, int len) {
  if (get_peer_type (id) == PEER_ENCR_CHAT) {
    peer_t *P = user_chat_get (id);
    if (!P) {
      logprintf ("Can not send to unknown encrypted chat\n");
      return;
    }
    if (P->encr_chat.state != sc_ok) {
      logprintf ("Chat is not yet initialized\n");
      return;
    }
  }
  long long t = -lrand48 () * (1ll << 32) - lrand48 ();
  bl_do_send_message_text (t, our_id, get_peer_type (id), get_peer_id (id), time (0), len, msg);
  struct message *M = message_get (t);
  assert (M);
  do_send_msg (M);
  print_message (M);
}
/* }}} */

/* {{{ Send text file */
void do_send_text (peer_id_t id, char *file_name) {
  int fd = open (file_name, O_RDONLY);
  if (fd < 0) {
    rprintf ("No such file '%s'\n", file_name);
    tfree_str (file_name);
    return;
  }
  static char buf[(1 << 20) + 1];
  int x = read (fd, buf, (1 << 20) + 1);
  assert (x >= 0);
  if (x == (1 << 20) + 1) {
    rprintf ("Too big file '%s'\n", file_name);
    tfree_str (file_name);
    close (fd);
  } else {
    buf[x] = 0;
    do_send_message (id, buf, x);
    tfree_str (file_name);
    close (fd);
  }
}
/* }}} */

/* {{{ Mark read */
int mark_read_on_receive (struct query *q UU) {
  assert (fetch_int () == (int)CODE_messages_affected_history);
  fetch_pts ();
  fetch_seq ();
  fetch_int (); // offset
  return 0;
}

int mark_read_encr_on_receive (struct query *q UU) {
  fetch_bool ();
  return 0;
}

struct query_methods mark_read_methods = {
  .on_answer = mark_read_on_receive
};

struct query_methods mark_read_encr_methods = {
  .on_answer = mark_read_encr_on_receive
};

void do_messages_mark_read (peer_id_t id, int max_id) {
  clear_packet ();
  out_int (CODE_messages_read_history);
  out_peer_id (id);
  out_int (max_id);
  out_int (0);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &mark_read_methods, 0);
}

void do_messages_mark_read_encr (peer_id_t id, long long access_hash, int last_time) {
  clear_packet ();
  out_int (CODE_messages_read_encrypted_history);
  out_int (CODE_input_encrypted_chat);
  out_int (get_peer_id (id));
  out_long (access_hash);
  out_int (last_time);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &mark_read_encr_methods, 0);
}

void do_mark_read (peer_id_t id) {
  peer_t *P = user_chat_get (id);
  if (!P) {
    rprintf ("Unknown peer\n");
    return;
  }
  if (get_peer_type (id) == PEER_USER || get_peer_type (id) == PEER_CHAT) {
    if (!P->last) {
      rprintf ("Unknown last peer message\n");
      return;
    }
    do_messages_mark_read (id, P->last->id);
    return;
  }
  assert (get_peer_type (id) == PEER_ENCR_CHAT);
  if (P->last) {
    do_messages_mark_read_encr (id, P->encr_chat.access_hash, P->last->date);
  } else {
    do_messages_mark_read_encr (id, P->encr_chat.access_hash, time (0) - 10);
    
  }
}
/* }}} */

/* {{{ Get history */
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

void do_get_local_history (peer_id_t id, int limit) {
  peer_t *P = user_chat_get (id);
  if (!P || !P->last) { return; }
  struct message *M = P->last;
  int count = 1;
  assert (!M->prev);
  while (count < limit && M->next) {
    M = M->next;
    count ++;
  }
  while (M) {
    print_message (M);
    M = M->prev;
  }
}

void do_get_history (peer_id_t id, int limit) {
  if (get_peer_type (id) == PEER_ENCR_CHAT || offline_mode) {
    do_get_local_history (id, limit);
    do_mark_read (id);
    return;
  }
  clear_packet ();
  out_int (CODE_messages_get_history);
  out_peer_id (id);
  out_int (0);
  out_int (0);
  out_int (limit);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &get_history_methods, (void *)*(long *)&id);
}
/* }}} */

/* {{{ Get dialogs */
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
/* }}} */

int allow_send_linux_version = 1;

/* {{{ Send photo/video file */
struct send_file {
  int fd;
  long long size;
  long long offset;
  int part_num;
  int part_size;
  long long id;
  long long thumb_id;
  peer_id_t to_id;
  unsigned media_type;
  char *file_name;
  int encr;
  unsigned char *iv;
  unsigned char *init_iv;
  unsigned char *key;
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
  fetch_pts ();
  fetch_seq ();
  print_message (M);
  return 0;
}

int send_encr_file_on_answer (struct query *q UU) {
  if (prefetch_int () != (int)CODE_messages_sent_encrypted_file) {
    hexdump_in ();
  }
  assert (fetch_int () == (int)CODE_messages_sent_encrypted_file);
  struct message *M = q->extra;
  M->date = fetch_int ();
  assert (fetch_int () == CODE_encrypted_file);
  M->media.encr_photo.id = fetch_long ();
  M->media.encr_photo.access_hash = fetch_long ();
  //M->media.encr_photo.size = fetch_int ();
  fetch_int ();
  M->media.encr_photo.dc_id = fetch_int ();
  assert (fetch_int () == M->media.encr_photo.key_fingerprint);
  print_message (M);
  message_insert (M);
  return 0;
}

struct query_methods send_file_part_methods = {
  .on_answer = send_file_part_on_answer
};

struct query_methods send_file_methods = {
  .on_answer = send_file_on_answer
};

struct query_methods send_encr_file_methods = {
  .on_answer = send_encr_file_on_answer
};

void send_part (struct send_file *f) {
  if (f->fd >= 0) {
    if (!f->part_num) {
      cur_uploading_bytes += f->size;
    }
    clear_packet ();
    if (f->size < (16 << 20)) {
      out_int (CODE_upload_save_file_part);      
      out_long (f->id);
      out_int (f->part_num ++);
    } else {
      out_int (CODE_upload_save_big_file_part);      
      out_long (f->id);
      out_int (f->part_num ++);
      out_int ((f->size + f->part_size - 1) / f->part_size);
    }
    static char buf[512 << 10];
    int x = read (f->fd, buf, f->part_size);
    assert (x > 0);
    f->offset += x;
    cur_uploaded_bytes += x;
    
    if (f->encr) {
      if (x & 15) {
        assert (f->offset == f->size);
        if (x & 15) {
          secure_random (buf + x, (-x) & 15);
          x = (x + 15) & ~15;
        }
      }
      
      AES_KEY aes_key;
      AES_set_encrypt_key (f->key, 256, &aes_key);
      AES_ige_encrypt ((void *)buf, (void *)buf, x, &aes_key, f->iv, 1);
    }
    out_cstring (buf, x);
    if (verbosity >= 2) {
      logprintf ("offset=%lld size=%lld\n", f->offset, f->size);
    }
    if (f->offset == f->size) {
      close (f->fd);
      f->fd = -1;
    } else {
      assert (f->part_size == x);
    }
    update_prompt ();
    send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_file_part_methods, f);
  } else {
    cur_uploaded_bytes -= f->size;
    cur_uploading_bytes -= f->size;
    update_prompt ();
    clear_packet ();
    assert (f->media_type == CODE_input_media_uploaded_photo || f->media_type == CODE_input_media_uploaded_video || f->media_type == CODE_input_media_uploaded_thumb_video || f->media_type == CODE_input_media_uploaded_audio || f->media_type == CODE_input_media_uploaded_document || f->media_type == CODE_input_media_uploaded_thumb_document);
    if (!f->encr) {
      out_int (CODE_messages_send_media);
      out_peer_id (f->to_id);
      out_int (f->media_type);
      if (f->size < (16 << 20)) {
        out_int (CODE_input_file);
      } else {
        out_int (CODE_input_file_big);
      }
      out_long (f->id);
      out_int (f->part_num);
      char *s = f->file_name + strlen (f->file_name);
      while (s >= f->file_name && *s != '/') { s --;}
      out_string (s + 1);
      if (f->size < (16 << 20)) {
        out_string ("");
      }
      if (f->media_type == CODE_input_media_uploaded_thumb_video || f->media_type == CODE_input_media_uploaded_thumb_document) {
        out_int (CODE_input_file);
        out_long (f->thumb_id);
        out_int (1);
        out_string ("thumb.jpg");
        out_string ("");
      }
      if (f->media_type == CODE_input_media_uploaded_video || f->media_type == CODE_input_media_uploaded_thumb_video) {
        out_int (100);
        out_int (100);
        out_int (100);
      }
      if (f->media_type == CODE_input_media_uploaded_document || f->media_type == CODE_input_media_uploaded_thumb_document) {
        out_string (s + 1);
        out_string ("text");
      }
      if (f->media_type == CODE_input_media_uploaded_audio) {
        out_int (60);
      }

      out_long (-lrand48 () * (1ll << 32) - lrand48 ());
      send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_file_methods, 0);
    } else {
      struct message *M = talloc0 (sizeof (*M));

      out_int (CODE_messages_send_encrypted_file);
      out_int (CODE_input_encrypted_chat);
      out_int (get_peer_id (f->to_id));
      peer_t *P = user_chat_get (f->to_id);
      assert (P);
      out_long (P->encr_chat.access_hash);
      long long r = -lrand48 () * (1ll << 32) - lrand48 (); 
      out_long (r);
      encr_start ();
      out_int (CODE_decrypted_message);
      out_long (r);
      out_random (15 + 4 * (lrand48 () % 3));
      out_string ("");
      if (f->media_type == CODE_input_media_uploaded_photo) {
        out_int (CODE_decrypted_message_media_photo);
        M->media.type = CODE_decrypted_message_media_photo;
      } else if (f->media_type == CODE_input_media_uploaded_video) {
        out_int (CODE_decrypted_message_media_video);
        M->media.type = CODE_decrypted_message_media_video;
      } else if (f->media_type == CODE_input_media_uploaded_audio) {
        out_int (CODE_decrypted_message_media_audio);
        M->media.type = CODE_decrypted_message_media_audio;
      } else if (f->media_type == CODE_input_media_uploaded_document) {
        out_int (CODE_decrypted_message_media_document);
        M->media.type = CODE_decrypted_message_media_document;;
      } else {
        assert (0);
      }
      if (f->media_type != CODE_input_media_uploaded_audio) {
        out_cstring ((void *)thumb_file, thumb_file_size);
        out_int (90);
        out_int (90);
      }
      if (f->media_type == CODE_input_media_uploaded_video) {
        out_int (0);
      }
      if (f->media_type == CODE_input_media_uploaded_document) {
        out_string (f->file_name);
        out_string ("text");
      }
      if (f->media_type == CODE_input_media_uploaded_audio) {
        out_int (60);
      }
      if (f->media_type == CODE_input_media_uploaded_video || f->media_type == CODE_input_media_uploaded_photo) {
        out_int (100);
        out_int (100);
      }
      out_int (f->size);
      out_cstring ((void *)f->key, 32);
      out_cstring ((void *)f->init_iv, 32);
      encr_finish (&P->encr_chat);
      if (f->size < (16 << 20)) {
        out_int (CODE_input_encrypted_file_uploaded);
      } else {
        out_int (CODE_input_encrypted_file_big_uploaded);
      }
      out_long (f->id);
      out_int (f->part_num);
      if (f->size < (16 << 20)) {
        out_string ("");
      }
 
      unsigned char md5[16];
      unsigned char str[64];
      memcpy (str, f->key, 32);
      memcpy (str + 32, f->init_iv, 32);
      MD5 (str, 64, md5);
      out_int ((*(int *)md5) ^ (*(int *)(md5 + 4)));

      tfree_secure (f->iv, 32);
      
      M->media.encr_photo.key = f->key;
      M->media.encr_photo.iv = f->init_iv;
      M->media.encr_photo.key_fingerprint = (*(int *)md5) ^ (*(int *)(md5 + 4)); 
      M->media.encr_photo.size = f->size;
  
      M->flags = FLAG_ENCRYPTED;
      M->from_id = MK_USER (our_id);
      M->to_id = f->to_id;
      M->unread = 1;
      M->message = tstrdup ("");
      M->out = 1;
      M->id = r;
      M->date = time (0);
      
      send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_encr_file_methods, M);

    }
    tfree_str (f->file_name);
    tfree (f, sizeof (*f));
  }
}

void send_file_thumb (struct send_file *f) {
  clear_packet ();
  f->thumb_id = lrand48 () * (1ll << 32) + lrand48 ();
  out_int (CODE_upload_save_file_part);
  out_long (f->thumb_id);
  out_int (0);
  out_cstring ((void *)thumb_file, thumb_file_size);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_file_part_methods, f);
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
  struct send_file *f = talloc0 (sizeof (*f));
  f->fd = fd;
  f->size = size;
  f->offset = 0;
  f->part_num = 0;
  int tmp = ((size + 2999) / 3000);
  f->part_size = (1 << 10);
  while (f->part_size < tmp) {
    f->part_size *= 2;
  }

  f->id = lrand48 () * (1ll << 32) + lrand48 ();
  f->to_id = to_id;
  f->media_type = type;
  f->file_name = file_name;
  if (get_peer_type (f->to_id) == PEER_ENCR_CHAT) {
    f->encr = 1;
    f->iv = talloc (32);
    secure_random (f->iv, 32);
    f->init_iv = talloc (32);
    memcpy (f->init_iv, f->iv, 32);
    f->key = talloc (32);
    secure_random (f->key, 32);
  }
  if (f->part_size > (512 << 10)) {
    close (fd);
    rprintf ("Too big file. Maximal supported size is %d", (512 << 10) * 1000);
    return;
  }
  if (f->media_type == CODE_input_media_uploaded_video && !f->encr) {
    f->media_type = CODE_input_media_uploaded_thumb_video;
    send_file_thumb (f);
  } else if (f->media_type == CODE_input_media_uploaded_document && !f->encr) {
    f->media_type = CODE_input_media_uploaded_thumb_document;
    send_file_thumb (f);
  } else {
    send_part (f);
  }
}
/* }}} */

/* {{{ Forward */
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
  fetch_pts ();
  fetch_seq ();
  print_message (M);
  return 0;
}

struct query_methods fwd_msg_methods = {
  .on_answer = fwd_msg_on_answer
};

void do_forward_message (peer_id_t id, int n) {
  if (get_peer_type (id) == PEER_ENCR_CHAT) {
    rprintf ("Can not forward messages from secret chat\n");
    return;
  }
  clear_packet ();
  out_int (CODE_messages_forward_message);
  out_peer_id (id);
  out_int (n);
  out_long (lrand48 () * (1ll << 32) + lrand48 ());
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &fwd_msg_methods, 0);
}
/* }}} */

/* {{{ Rename chat */
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
  fetch_pts ();
  fetch_seq ();
  print_message (M);
  return 0;
}

struct query_methods rename_chat_methods = {
  .on_answer = rename_chat_on_answer
};

void do_rename_chat (peer_id_t id, char *name UU) {
  clear_packet ();
  out_int (CODE_messages_edit_chat_title);
  assert (get_peer_type (id) == PEER_CHAT);
  out_int (get_peer_id (id));
  out_string (name);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &rename_chat_methods, 0);
}
/* }}} */

/* {{{ Chat info */
void print_chat_info (struct chat *C) {
  peer_t *U = (void *)C;
  print_start ();
  push_color (COLOR_YELLOW);
  printf ("Chat ");
  print_chat_name (U->id, U);
  printf (" members:\n");
  int i;
  for (i = 0; i < C->user_list_size; i++) {
    printf ("\t\t");
    print_user_name (MK_USER (C->user_list[i].user_id), user_chat_get (MK_USER (C->user_list[i].user_id)));
    printf (" invited by ");
    print_user_name (MK_USER (C->user_list[i].inviter_id), user_chat_get (MK_USER (C->user_list[i].inviter_id)));
    printf (" at ");
    print_date_full (C->user_list[i].date);
    if (C->user_list[i].user_id == C->admin_id) {
      printf (" admin");
    }
    printf ("\n");
  }
  pop_color ();
  print_end ();
}

int chat_info_on_answer (struct query *q UU) {
  struct chat *C = fetch_alloc_chat_full ();
  print_chat_info (C);
  return 0;
}

struct query_methods chat_info_methods = {
  .on_answer = chat_info_on_answer
};

void do_get_chat_info (peer_id_t id) {
  if (offline_mode) {
    peer_t *C = user_chat_get (id);
    if (!C) {
      rprintf ("No such chat\n");
    } else {
      print_chat_info (&C->chat);
    }
    return;
  }
  clear_packet ();
  out_int (CODE_messages_get_full_chat);
  assert (get_peer_type (id) == PEER_CHAT);
  out_int (get_peer_id (id));
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &chat_info_methods, 0);
}
/* }}} */

/* {{{ User info */

void print_user_info (struct user *U) {
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
}

int user_info_on_answer (struct query *q UU) {
  struct user *U = fetch_alloc_user_full ();
  print_user_info (U);
  return 0;
}

struct query_methods user_info_methods = {
  .on_answer = user_info_on_answer
};

void do_get_user_info (peer_id_t id) {
  if (offline_mode) {
    peer_t *C = user_chat_get (id);
    if (!C) {
      rprintf ("No such user\n");
    } else {
      print_user_info (&C->user);
    }
    return;
  }
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
/* }}} */

/* {{{ Get user info silently */
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
/* }}} */

/* {{{ Load photo/video */
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
  unsigned char *iv;
  unsigned char *key;
  int type;
};


void end_load (struct download *D) {
  cur_downloading_bytes -= D->size;
  cur_downloaded_bytes -= D->size;
  update_prompt ();
  close (D->fd);
  if (D->next == 1) {
    logprintf ("Done: %s\n", D->name);
  } else if (D->next == 2) {
    static char buf[PATH_MAX];
    if (tsnprintf (buf, sizeof (buf), OPEN_BIN, D->name) >= (int) sizeof (buf)) {
      logprintf ("Open image command buffer overflow\n");
    } else {
      int x = system (buf);
      if (x < 0) {
        logprintf ("Can not open image viewer: %m\n");
        logprintf ("Image is at %s\n", D->name);
      }
    }
  }
  if (D->iv) {
    tfree_secure (D->iv, 32);
  }
  tfree_str (D->name);
  tfree (D, sizeof (*D));
}

void load_next_part (struct download *D);
int download_on_answer (struct query *q) {
  assert (fetch_int () == (int)CODE_upload_file);
  unsigned x = fetch_int ();
  assert (x);
  struct download *D = q->extra;
  if (D->fd == -1) {
    D->fd = open (D->name, O_CREAT | O_WRONLY, 0640);
  }
  fetch_int (); // mtime
  int len = prefetch_strlen ();
  assert (len >= 0);
  cur_downloaded_bytes += len;
  update_prompt ();
  if (D->iv) {
    unsigned char *ptr = (void *)fetch_str (len);
    assert (!(len & 15));
    AES_KEY aes_key;
    AES_set_decrypt_key (D->key, 256, &aes_key);
    AES_ige_encrypt (ptr, ptr, len, &aes_key, D->iv, 0);
    if (len > D->size - D->offset) {
      len = D->size - D->offset;
    }
    assert (write (D->fd, ptr, len) == len);
  } else {
    assert (write (D->fd, fetch_str (len), len) == len);
  }
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
    static char buf[PATH_MAX];
    int l;
    if (!D->id) {
      l = tsnprintf (buf, sizeof (buf), "%s/download_%lld_%d", get_downloads_directory (), D->volume, D->local_id);
    } else {
      l = tsnprintf (buf, sizeof (buf), "%s/download_%lld", get_downloads_directory (), D->id);
    }
    if (l >= (int) sizeof (buf)) {
      logprintf ("Download filename is too long");
      exit (1);
    }
    D->name = tstrdup (buf);
    struct stat st;
    if (stat (buf, &st) >= 0) {
      D->offset = st.st_size;      
      if (D->offset >= D->size) {
        cur_downloading_bytes += D->size;
        cur_downloaded_bytes += D->offset;
        rprintf ("Already downloaded\n");
        end_load (D);
        return;
      }
    }
    
    cur_downloading_bytes += D->size;
    cur_downloaded_bytes += D->offset;
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
    if (D->iv) {
      out_int (CODE_input_encrypted_file_location);
    } else {
      out_int (D->type);
    }
    out_long (D->id);
    out_long (D->access_hash);
  }
  out_int (D->offset);
  out_int (1 << 14);
  send_query (DC_list[D->dc], packet_ptr - packet_buffer, packet_buffer, &download_methods, D);
  //send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &download_methods, D);
}

void do_load_photo_size (struct photo_size *P, int next) {
  if (!P->loc.dc) {
    rprintf ("Bad video thumb\n");
    return;
  }
  
  assert (P);
  assert (next);
  struct download *D = talloc0 (sizeof (*D));
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

void do_load_document_thumb (struct document *video, int next) {
  do_load_photo_size (&video->thumb, next);
}

void do_load_video (struct video *V, int next) {
  assert (V);
  assert (next);
  struct download *D = talloc0 (sizeof (*D));
  D->offset = 0;
  D->size = V->size;
  D->id = V->id;
  D->access_hash = V->access_hash;
  D->dc = V->dc_id;
  D->next = next;
  D->name = 0;
  D->fd = -1;
  D->type = CODE_input_video_file_location;
  load_next_part (D);
}

void do_load_document (struct document *V, int next) {
  assert (V);
  assert (next);
  struct download *D = talloc0 (sizeof (*D));
  D->offset = 0;
  D->size = V->size;
  D->id = V->id;
  D->access_hash = V->access_hash;
  D->dc = V->dc_id;
  D->next = next;
  D->name = 0;
  D->fd = -1;
  D->type = CODE_input_document_file_location;
  load_next_part (D);
}

void do_load_encr_video (struct encr_video *V, int next) {
  assert (V);
  assert (next);
  struct download *D = talloc0 (sizeof (*D));
  D->offset = 0;
  D->size = V->size;
  D->id = V->id;
  D->access_hash = V->access_hash;
  D->dc = V->dc_id;
  D->next = next;
  D->name = 0;
  D->fd = -1;
  D->key = V->key;
  D->iv = talloc (32);
  memcpy (D->iv, V->iv, 32);
  load_next_part (D);
      
  unsigned char md5[16];
  unsigned char str[64];
  memcpy (str, V->key, 32);
  memcpy (str + 32, V->iv, 32);
  MD5 (str, 64, md5);
  assert (V->key_fingerprint == ((*(int *)md5) ^ (*(int *)(md5 + 4))));
}
/* }}} */

/* {{{ Export auth */
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
  char *s = talloc (l);
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
/* }}} */

/* {{{ Import auth */
int import_auth_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_auth_authorization);
  fetch_int (); // expires
  fetch_alloc_user ();
  tfree_str (export_auth_str);
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
/* }}} */

/* {{{ Add contact */
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
/* }}} */

/* {{{ Msg search */
int msg_search_on_answer (struct query *q UU) {
  return get_history_on_answer (q);
}

struct query_methods msg_search_methods = {
  .on_answer = msg_search_on_answer
};

void do_msg_search (peer_id_t id, int from, int to, int limit, const char *s) {
  if (get_peer_type (id) == PEER_ENCR_CHAT) {
    rprintf ("Can not search in secure chat\n");
    return;
  }
  clear_packet ();
  out_int (CODE_messages_search);
  if (get_peer_type (id) == PEER_UNKNOWN) {
    out_int (CODE_input_peer_empty);
  } else {
    out_peer_id (id);
  }
  out_string (s);
  out_int (CODE_input_messages_filter_empty);
  out_int (from);
  out_int (to);
  out_int (0); // offset
  out_int (0); // max_id
  out_int (limit);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &msg_search_methods, 0);
}
/* }}} */

/* {{{ Contacts search */
int contacts_search_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_contacts_found);
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  int i;
  for (i = 0; i < n; i++) {
    assert (fetch_int () == (int)CODE_contact_found);
    fetch_int ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  print_start ();
  push_color (COLOR_YELLOW);
  for (i = 0; i < n; i++) {
    struct user *U = fetch_alloc_user ();
    printf ("User ");
    push_color  (COLOR_RED);
    printf ("%s %s", U->first_name, U->last_name); 
    pop_color ();
    printf (". Phone %s\n", U->phone);
  }
  pop_color ();
  print_end ();
  return 0;
}

struct query_methods contacts_search_methods = {
  .on_answer = contacts_search_on_answer
};

void do_contacts_search (int limit, const char *s) {
  clear_packet ();
  out_int (CODE_contacts_search);
  out_string (s);
  out_int (limit);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &contacts_search_methods, 0);
}
/* }}} */

/* {{{ Encr accept */
int send_encr_accept_on_answer (struct query *q UU) {
  struct secret_chat *E = fetch_alloc_encrypted_chat ();

  if (E->state == sc_ok) {
    print_start ();
    push_color (COLOR_YELLOW);
    printf ("Encrypted connection with ");
    print_encr_chat_name (E->id, (void *)E);
    printf (" established\n");
    pop_color ();
    print_end ();
  } else {
    print_start ();
    push_color (COLOR_YELLOW);
    printf ("Encrypted connection with ");
    print_encr_chat_name (E->id, (void *)E);
    printf (" failed\n");
    pop_color ();
    print_end ();
  }
  return 0;
}

int send_encr_request_on_answer (struct query *q UU) {
  struct secret_chat *E = fetch_alloc_encrypted_chat ();
  if (E->state == sc_deleted) {
    print_start ();
    push_color (COLOR_YELLOW);
    printf ("Encrypted connection with ");
    print_encr_chat_name (E->id, (void *)E);
    printf (" can not be established\n");
    pop_color ();
    print_end ();
  } else {
    print_start ();
    push_color (COLOR_YELLOW);
    printf ("Establishing connection with ");
    print_encr_chat_name (E->id, (void *)E);
    printf ("\n");
    pop_color ();
    print_end ();

    assert (E->state == sc_waiting);
  }
  return 0;
}

struct query_methods send_encr_accept_methods  = {
  .on_answer = send_encr_accept_on_answer
};

struct query_methods send_encr_request_methods  = {
  .on_answer = send_encr_request_on_answer
};

int encr_root;
unsigned char *encr_prime;
int encr_param_version;
BN_CTX *ctx;

void do_send_accept_encr_chat (struct secret_chat *E, unsigned char *random) {
  int i;
  int ok = 0;
  for (i = 0; i < 64; i++) {
    if (E->key[i]) {
      ok = 1;
      break;
    }
  }
  if (ok) { return; } // Already generated key for this chat
  unsigned char random_here[256];
  secure_random (random_here, 256);
  for (i = 0; i < 256; i++) {
    random[i] ^= random_here[i];
  }
  BIGNUM *b = BN_bin2bn (random, 256, 0);
  ensure_ptr (b);
  BIGNUM *g_a = BN_bin2bn (E->g_key, 256, 0);
  ensure_ptr (g_a);
  assert (check_g (encr_prime, g_a) >= 0);
  if (!ctx) {
    ctx = BN_CTX_new ();
    ensure_ptr (ctx);
  }
  BIGNUM *p = BN_bin2bn (encr_prime, 256, 0); 
  ensure_ptr (p);
  BIGNUM *r = BN_new ();
  ensure_ptr (r);
  ensure (BN_mod_exp (r, g_a, b, p, ctx));
  static unsigned char kk[256];
  memset (kk, 0, sizeof (kk));
  BN_bn2bin (r, kk);
  for (i = 0; i < 256; i++) {
    kk[i] ^= E->nonce[i];
  }
  static unsigned char sha_buffer[20];
  sha1 (kk, 256, sha_buffer);

  bl_do_set_encr_chat_key (E, kk, *(long long *)(sha_buffer + 12));

  clear_packet ();
  out_int (CODE_messages_accept_encryption);
  out_int (CODE_input_encrypted_chat);
  out_int (get_peer_id (E->id));
  out_long (E->access_hash);
  
  ensure (BN_set_word (g_a, encr_root));
  ensure (BN_mod_exp (r, g_a, b, p, ctx));
  static unsigned char buf[256];
  memset (buf, 0, sizeof (buf));
  BN_bn2bin (r, buf);
  out_cstring ((void *)buf, 256);

  out_long (E->key_fingerprint);
  BN_clear_free (b);
  BN_clear_free (g_a);
  BN_clear_free (p);
  BN_clear_free (r);

  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_encr_accept_methods, E);
}

void do_create_keys_end (struct secret_chat *U) {
  assert (encr_prime);
  BIGNUM *g_b = BN_bin2bn (U->g_key, 256, 0);
  ensure_ptr (g_b);
  assert (check_g (encr_prime, g_b) >= 0);
  if (!ctx) {
    ctx = BN_CTX_new ();
    ensure_ptr (ctx);
  }
  BIGNUM *p = BN_bin2bn (encr_prime, 256, 0); 
  ensure_ptr (p);
  BIGNUM *r = BN_new ();
  ensure_ptr (r);
  BIGNUM *a = BN_bin2bn ((void *)U->key, 256, 0);
  ensure_ptr (a);
  ensure (BN_mod_exp (r, g_b, a, p, ctx));

  void *t = talloc (256);
  memcpy (t, U->key, 256);
  
  memset (U->key, 0, sizeof (U->key));
  BN_bn2bin (r, (void *)U->key);
  int i;
  for (i = 0; i < 64; i++) {
    U->key[i] ^= *(((int *)U->nonce) + i);
  }
  
  static unsigned char sha_buffer[20];
  sha1 ((void *)U->key, 256, sha_buffer);
  long long k = *(long long *)(sha_buffer + 12);
  if (k != U->key_fingerprint) {
    logprintf ("version = %d\n", encr_param_version);
    hexdump ((void *)U->nonce, (void *)(U->nonce + 256));
    hexdump ((void *)U->g_key, (void *)(U->g_key + 256));
    hexdump ((void *)U->key, (void *)(U->key + 64));
    hexdump ((void *)t, (void *)(t + 256));
    hexdump ((void *)sha_buffer, (void *)(sha_buffer + 20));
    logprintf ("!!Key fingerprint mismatch (my 0x%llx 0x%llx)\n", (unsigned long long)k, (unsigned long long)U->key_fingerprint);
    U->state = sc_deleted;
  }

  tfree_secure (t, 256);
  
  BN_clear_free (p);
  BN_clear_free (g_b);
  BN_clear_free (r);
  BN_clear_free (a);
}

void do_send_create_encr_chat (void *x, unsigned char *random) {
  int user_id = (long)x;
  int i;
  unsigned char random_here[256];
  secure_random (random_here, 256);
  for (i = 0; i < 256; i++) {
    random[i] ^= random_here[i];
  }
  if (!ctx) {
    ctx = BN_CTX_new ();
    ensure_ptr (ctx);
  }
  BIGNUM *a = BN_bin2bn (random, 256, 0);
  ensure_ptr (a);
  BIGNUM *p = BN_bin2bn (encr_prime, 256, 0); 
  ensure_ptr (p);
 
  BIGNUM *g = BN_new ();
  ensure_ptr (g);

  ensure (BN_set_word (g, encr_root));

  BIGNUM *r = BN_new ();
  ensure_ptr (r);

  ensure (BN_mod_exp (r, g, a, p, ctx));

  BN_clear_free (a);

  static char g_a[256];
  memset (g_a, 0, 256);

  BN_bn2bin (r, (void *)g_a);
  
  int t = lrand48 ();
  while (user_chat_get (MK_ENCR_CHAT (t))) {
    t = lrand48 ();
  }

  bl_do_encr_chat_init (t, user_id, (void *)random, (void *)g_a);
  peer_t *_E = user_chat_get (MK_ENCR_CHAT (t));
  assert (_E);
  struct secret_chat *E = &_E->encr_chat;
  
  clear_packet ();
  out_int (CODE_messages_request_encryption);
  peer_t *U = user_chat_get (MK_USER (E->user_id));
  assert (U);
  if (U && U->user.access_hash) {
    out_int (CODE_input_user_foreign);
    out_int (E->user_id);
    out_long (U->user.access_hash);
  } else {
    out_int (CODE_input_user_contact);
    out_int (E->user_id);
  }
  out_int (get_peer_id (E->id));
  out_cstring (g_a, 256);
  write_secret_chat_file ();
  
  BN_clear_free (g);
  BN_clear_free (p);
  BN_clear_free (r);

  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &send_encr_request_methods, E);
}

int get_dh_config_on_answer (struct query *q UU) {
  unsigned x = fetch_int ();
  assert (x == CODE_messages_dh_config || x == CODE_messages_dh_config_not_modified || LOG_DH_CONFIG);
  if (x == CODE_messages_dh_config || x == LOG_DH_CONFIG)  {
    int a = fetch_int ();
    int l = prefetch_strlen ();
    assert (l == 256);
    char *s = fetch_str (l);
    int v = fetch_int ();
    bl_do_set_dh_params (a, (void *)s, v);

    BIGNUM *p = BN_bin2bn ((void *)s, 256, 0);
    ensure_ptr (p);
    assert (check_DH_params (p, a) >= 0);
    BN_free (p);      
  }
  if (x == LOG_DH_CONFIG) { return 0; }
  int l = prefetch_strlen ();
  assert (l == 256);
  unsigned char *random = talloc (256);
  memcpy (random, fetch_str (256), 256);
  if (q->extra) {
    void **x = q->extra;
    ((void (*)(void *, void *))(*x))(x[1], random);
    tfree (x, 2 * sizeof (void *));
    tfree_secure (random, 256);
  } else {
    tfree_secure (random, 256);
  }
  return 0;
}

struct query_methods get_dh_config_methods  = {
  .on_answer = get_dh_config_on_answer
};

void do_accept_encr_chat_request (struct secret_chat *E) {
  assert (E->state == sc_request);
  
  clear_packet ();
  out_int (CODE_messages_get_dh_config);
  out_int (encr_param_version);
  out_int (256);
  void **x = talloc (2 * sizeof (void *));
  x[0] = do_send_accept_encr_chat;
  x[1] = E;
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &get_dh_config_methods, x);
}

void do_create_encr_chat_request (int user_id) {
  clear_packet ();
  out_int (CODE_messages_get_dh_config);
  out_int (encr_param_version);
  out_int (256);
  void **x = talloc (2 * sizeof (void *));
  x[0] = do_send_create_encr_chat;
  x[1] = (void *)(long)(user_id);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &get_dh_config_methods, x);
}
/* }}} */

/* {{{ Get difference */
int unread_messages;
int difference_got;
int seq, pts, qts, last_date;
int get_state_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_updates_state);
  bl_do_set_pts (fetch_int ());
  bl_do_set_qts (fetch_int ());
  bl_do_set_date (fetch_int ());
  bl_do_set_seq (fetch_int ());
  unread_messages = fetch_int ();
  write_state_file ();
  difference_got = 1;
  return 0;
}

int get_difference_active;
int get_difference_on_answer (struct query *q UU) {
  get_difference_active = 0;
  unsigned x = fetch_int ();
  if (x == CODE_updates_difference_empty) {
    bl_do_set_date (fetch_int ());
    bl_do_set_seq (fetch_int ());
    difference_got = 1;
  } else if (x == CODE_updates_difference || x == CODE_updates_difference_slice) {
    int n, i;
    assert (fetch_int () == CODE_vector);
    n = fetch_int ();
    static struct message *ML[10000];
    int ml_pos = 0;
    for (i = 0; i < n; i++) {
      if (ml_pos < 10000) {
        ML[ml_pos ++] = fetch_alloc_message ();
      } else {
        fetch_alloc_message ();
      }
    }
    assert (fetch_int () == CODE_vector);
    n = fetch_int ();
    for (i = 0; i < n; i++) {
      if (ml_pos < 10000) {
        ML[ml_pos ++] = fetch_alloc_encrypted_message ();
      } else {
        fetch_alloc_encrypted_message ();
      }
    }
    assert (fetch_int () == CODE_vector);
    n = fetch_int ();
    for (i = 0; i < n; i++) {
      work_update (0, 0);
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
    assert (fetch_int () == (int)CODE_updates_state);
    bl_do_set_pts (fetch_int ());
    bl_do_set_qts (fetch_int ());
    bl_do_set_date (fetch_int ());
    bl_do_set_seq (fetch_int ());
    unread_messages = fetch_int ();
    write_state_file ();
    for (i = 0; i < ml_pos; i++) {
      print_message (ML[i]);
    }
    if (x == CODE_updates_difference_slice) {
      do_get_difference ();
    } else {
      difference_got = 1;
    }
  } else {
    assert (0);
  }
  return 0;   
}

struct query_methods get_state_methods = {
  .on_answer = get_state_on_answer
};

struct query_methods get_difference_methods = {
  .on_answer = get_difference_on_answer
};

void do_get_difference (void) {
  get_difference_active = 1;
  difference_got = 0;
  clear_packet ();
  do_insert_header ();
  if (seq > 0 || sync_from_start) {
    if (pts == 0) { pts = 1; }
    if (qts == 0) { qts = 1; }
    if (last_date == 0) { last_date = 1; }
    out_int (CODE_updates_get_difference);
    out_int (pts);
    out_int (last_date);
    out_int (qts);
    send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &get_difference_methods, 0);
  } else {
    out_int (CODE_updates_get_state);
    send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &get_state_methods, 0);
  }
}
/* }}} */

/* {{{ Visualize key */
char *colors[4] = {COLOR_GREY, COLOR_CYAN, COLOR_BLUE, COLOR_GREEN};

void do_visualize_key (peer_id_t id) {
  assert (get_peer_type (id) == PEER_ENCR_CHAT);
  peer_t *P = user_chat_get (id);
  assert (P);
  if (P->encr_chat.state != sc_ok) {
    rprintf ("Chat is not initialized yet\n");
    return;
  }
  unsigned char buf[20];
  SHA1 ((void *)P->encr_chat.key, 256, buf);
  print_start ();
  int i;
  for (i = 0; i < 16; i++) {
    int x = buf[i];
    int j;
    for (j = 0; j < 4; j ++) {    
      push_color (colors[x & 3]);
      push_color (COLOR_INVERSE);
      printf ("  ");
      pop_color ();
      pop_color ();
      x = x >> 2;
    }
    if (i & 1) { printf ("\n"); }
  }
  print_end ();
}
/* }}} */

/* {{{ Get suggested */
int get_suggested_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_contacts_suggested);
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  logprintf ("n = %d\n", n);
  assert (n <= 200);
  int l[400];
  int i;
  for (i = 0; i < n; i++) {
    assert (fetch_int () == CODE_contact_suggested);
    l[2 * i] = fetch_int ();
    l[2 * i + 1] = fetch_int ();
  }
  assert (fetch_int () == CODE_vector);
  int m = fetch_int ();
  assert (n == m);
  print_start ();
  push_color (COLOR_YELLOW);
  for (i = 0; i < m; i++) {
    peer_t *U = (void *)fetch_alloc_user ();
    assert (get_peer_id (U->id) == l[2 * i]);
    print_user_name (U->id, U);
    printf (" phone %s: %d mutual friends\n", U->user.phone, l[2 * i + 1]);
  }
  pop_color ();
  print_end ();
  return 0;
}

struct query_methods get_suggested_methods = {
  .on_answer = get_suggested_on_answer
};

void do_get_suggested (void) {
  clear_packet ();
  out_int (CODE_contacts_get_suggested);
  out_int (100);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &get_suggested_methods, 0);
}
/* }}} */

/* {{{ Add user to chat */

struct query_methods add_user_to_chat_methods = {
  .on_answer = fwd_msg_on_answer
};

void do_add_user_to_chat (peer_id_t chat_id, peer_id_t id, int limit) {
  clear_packet ();
  out_int (CODE_messages_add_chat_user);
  out_int (get_peer_id (chat_id));
  
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
  out_int (limit);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &add_user_to_chat_methods, 0);
}

void do_del_user_from_chat (peer_id_t chat_id, peer_id_t id) {
  clear_packet ();
  out_int (CODE_messages_delete_chat_user);
  out_int (get_peer_id (chat_id));
  
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
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &add_user_to_chat_methods, 0);
}
/* }}} */

/* {{{ Create secret chat */
char *create_print_name (peer_id_t id, const char *a1, const char *a2, const char *a3, const char *a4);

void do_create_secret_chat (peer_id_t id) {
  assert (get_peer_type (id) == PEER_USER);
  peer_t *U = user_chat_get (id);
  if (!U) { 
    rprintf ("Can not create chat with unknown user\n");
    return;
  }

  do_create_encr_chat_request (get_peer_id (id)); 
}
/* }}} */

int update_status_on_answer (struct query *q UU) {
  fetch_bool ();
  return 0;
}

struct query_methods update_status_methods = {
  .on_answer = update_status_on_answer
};

void do_update_status (int online UU) {
  clear_packet ();
  out_int (CODE_account_update_status);
  out_int (online ? CODE_bool_false : CODE_bool_true);
  send_query (DC_working, packet_ptr - packet_buffer, packet_buffer, &update_status_methods, 0);
}
