/* 
    This file is part of tgl-library

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

    Copyright Vitaly Valtman 2013-2014
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _FILE_OFFSET_BITS 64
#include <string.h>
#include <memory.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/utsname.h>


#include "include.h"
#include "mtproto-client.h"
#include "queries.h"
#include "tree.h"
#include "mtproto-common.h"
//#include "telegram.h"
#include "loop.h"
#include "structures.h"
//#include "interface.h"
//#include "net.h"
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

#include "no-preview.h"
#include "binlog.h"
#include "updates.h"
#include "auto.h"
#include "tgl.h"
#ifdef EVENT_V2
#include <event2/event.h>
#else
#include <event.h>
#include "event-old.h"
#endif

#define sha1 SHA1

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

//int want_dc_num;
char *get_downloads_directory (void);
//extern int offline_mode;

//long long cur_uploading_bytes;
//long long cur_uploaded_bytes;
//long long cur_downloading_bytes;
//long long cur_downloaded_bytes;

//extern int binlog_enabled;
//extern int sync_from_start;

//static int queries_num;

static void out_peer_id (tgl_peer_id_t id);
#define QUERY_TIMEOUT 6.0

#define memcmp8(a,b) memcmp ((a), (b), 8)
DEFINE_TREE (query, struct query *, memcmp8, 0) ;
static struct tree_query *queries_tree;

struct query *tglq_query_get (long long id) {
  return tree_lookup_query (queries_tree, (void *)&id);
}

static int alarm_query (struct query *q) {
  assert (q);
  vlogprintf (E_DEBUG, "Alarm query %lld\n", q->msg_id);
  //q->ev.timeout = get_double_time () + QUERY_TIMEOUT;
  //insert_event_timer (&q->ev);
  
  
  static struct timeval ptimeout = { QUERY_TIMEOUT, 0};
  event_add (q->ev, &ptimeout);

  /*if (q->session->c->out_bytes >= 100000) {
    return 0;
  }*/

  if (q->session->session_id == q->session_id) {
    clear_packet ();
    out_int (CODE_msg_container);
    out_int (1);
    out_long (q->msg_id);
    out_int (q->seq_no);
    out_int (4 * q->data_len);
    out_ints (q->data, q->data_len);
  
    tglmp_encrypt_send_message (q->session->c, packet_buffer, packet_ptr - packet_buffer, q->flags & QUERY_FORCE_SEND);
  } else {
    q->flags &= ~QUERY_ACK_RECEIVED;
    queries_tree = tree_delete_query (queries_tree, q);
    q->msg_id = tglmp_encrypt_send_message (q->session->c, q->data, q->data_len, (q->flags & QUERY_FORCE_SEND) | 1);
    queries_tree = tree_insert_query (queries_tree, q, lrand48 ());
    q->session_id = q->session->session_id;
    if (!(q->session->dc->flags & 4) && !(q->flags & QUERY_FORCE_SEND)) {
      q->session_id = 0;
    }
  }
  return 0;
}

void tglq_query_restart (long long id) {
  struct query *q = tglq_query_get (id);
  if (q) {
    event_del (q->ev);
    alarm_query (q);
  }
}

static void alarm_query_gateway (evutil_socket_t fd, short what, void *arg) {
  alarm_query (arg);
}


struct query *tglq_send_query_ex (struct tgl_dc *DC, int ints, void *data, struct query_methods *methods, void *extra, void *callback, void *callback_extra, int flags) {
  assert (DC);
  assert (DC->auth_key_id);
  if (!DC->sessions[0]) {
    tglmp_dc_create_session (DC);
  }
  vlogprintf (E_DEBUG, "Sending query of size %d to DC (%s:%d)\n", 4 * ints, DC->ip, DC->port);
  struct query *q = talloc0 (sizeof (*q));
  q->data_len = ints;
  q->data = talloc (4 * ints);
  memcpy (q->data, data, 4 * ints);
  q->msg_id = tglmp_encrypt_send_message (DC->sessions[0]->c, data, ints, 1 | (flags & QUERY_FORCE_SEND));
  q->session = DC->sessions[0];
  q->seq_no = q->session->seq_no - 1; 
  q->session_id = q->session->session_id;
  if (!(DC->flags & 4) && !(flags & QUERY_FORCE_SEND)) {
    q->session_id = 0;
  }
  vlogprintf (E_DEBUG, "Msg_id is %lld %p\n", q->msg_id, q);
  q->methods = methods;
  q->DC = DC;
  q->flags = flags & QUERY_FORCE_SEND;
  if (queries_tree) {
    vlogprintf (E_DEBUG + 2, "%lld %lld\n", q->msg_id, queries_tree->x->msg_id);
  }
  queries_tree = tree_insert_query (queries_tree, q, lrand48 ());

  //q->ev.alarm = (void *)alarm_query;
  //q->ev.timeout = get_double_time () + QUERY_TIMEOUT;
  //q->ev.self = (void *)q;
  //insert_event_timer (&q->ev);

  q->ev = evtimer_new (tgl_state.ev_base, alarm_query_gateway, q);
  static struct timeval ptimeout = { QUERY_TIMEOUT, 0};
  event_add (q->ev, &ptimeout);

  q->extra = extra;
  q->callback = callback;
  q->callback_extra = callback_extra;
  tgl_state.active_queries ++;
  return q;
}

struct query *tglq_send_query (struct tgl_dc *DC, int ints, void *data, struct query_methods *methods, void *extra, void *callback, void *callback_extra) {
  return tglq_send_query_ex (DC, ints, data, methods, extra, callback, callback_extra, 0);
}

static int fail_on_error (struct query *q UU, int error_code UU, int l UU, char *error UU) {
  fprintf (stderr, "error #%d: %.*s\n", error_code, l, error);
  assert (0);
  return 0;
}

void tglq_query_ack (long long id) {
  struct query *q = tglq_query_get (id);
  if (q && !(q->flags & QUERY_ACK_RECEIVED)) { 
    assert (q->msg_id == id);
    q->flags |= QUERY_ACK_RECEIVED; 
    event_del (q->ev);
  }
}

void tglq_query_error (long long id) {
  assert (fetch_int () == CODE_rpc_error);
  int error_code = fetch_int ();
  int error_len = prefetch_strlen ();
  char *error = fetch_str (error_len);
  vlogprintf (E_WARNING, "error for query #%lld: #%d :%.*s\n", id, error_code, error_len, error);
  struct query *q = tglq_query_get (id);
  if (!q) {
    vlogprintf (E_WARNING, "No such query\n");
  } else {
    if (!(q->flags & QUERY_ACK_RECEIVED)) {
      event_del (q->ev);
    }
    queries_tree = tree_delete_query (queries_tree, q);
    if (q->methods && q->methods->on_error) {
      q->methods->on_error (q, error_code, error_len, error);
    } else {
      vlogprintf ( E_WARNING, "error for query #%lld: #%d :%.*s\n", id, error_code, error_len, error);
    }
    tfree (q->data, q->data_len * 4);
    event_free (q->ev);
    tfree (q, sizeof (*q));
  }
  tgl_state.active_queries --;
}

#define MAX_PACKED_SIZE (1 << 24)
static int packed_buffer[MAX_PACKED_SIZE / 4];

void tglq_query_result (long long id UU) {
  vlogprintf (E_DEBUG, "result for query #%lld. Size %ld bytes\n", id, (long)4 * (in_end - in_ptr));
  /*if (verbosity  >= 4) {
    logprintf ( "result: ");
    hexdump_in ();
  }*/
  int op = prefetch_int ();
  int *end = 0;
  int *eend = 0;
  if (op == CODE_gzip_packed) {
    fetch_int ();
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    int total_out = tgl_inflate (s, l, packed_buffer, MAX_PACKED_SIZE);
    vlogprintf (E_DEBUG, "inflated %d bytes\n", total_out);
    end = in_ptr;
    eend = in_end;
    //assert (total_out % 4 == 0);
    in_ptr = packed_buffer;
    in_end = in_ptr + total_out / 4;
    /*if (verbosity >= 4) {
      logprintf ( "Unzipped data: ");
      hexdump_in ();
    }*/
  }
  struct query *q = tglq_query_get (id);
  if (!q) {
    //if (verbosity) {
    //  logprintf ( "No such query\n");
    //}
    vlogprintf (E_WARNING, "No such query\n");
    in_ptr = in_end;
  } else {
    if (!(q->flags & QUERY_ACK_RECEIVED)) {
      event_del (q->ev);
    }
    queries_tree = tree_delete_query (queries_tree, q);
    if (q->methods && q->methods->on_answer) {
      if (q->methods->type) {
        int *save = in_ptr;
        vlogprintf (E_DEBUG, "in_ptr = %p, end_ptr = %p\n", in_ptr, in_end);
        if (skip_type_any (q->methods->type) < 0) {
          vlogprintf (E_ERROR, "Skipped %ld int out of %ld (type %s)\n", (long)(in_ptr - save), (long)(in_end - save), q->methods->type->type->id);
          assert (0);
        }
        
        assert (in_ptr == in_end);
        in_ptr = save;
      }
      q->methods->on_answer (q);
      assert (in_ptr == in_end);
    }
    tfree (q->data, 4 * q->data_len);
    event_free (q->ev);
    tfree (q, sizeof (*q));
  }
  if (end) {
    in_ptr = end;
    in_end = eend;
  }
  tgl_state.active_queries --;
} 


//int max_chat_size;
//int max_bcast_size;
//int want_dc_num;
//int new_dc_num;
//extern struct tgl_dc *DC_list[];
//extern struct tgl_dc *tgl_state.DC_working;

static void out_random (int n) {
  assert (n <= 32);
  static char buf[32];
  tglt_secure_random (buf, n);
  out_cstring (buf, n);
}

int allow_send_linux_version;
void tgl_do_insert_header (void) {
  out_int (CODE_invoke_with_layer16);  
  out_int (CODE_init_connection);
  out_int (TG_APP_ID);
  if (allow_send_linux_version) {
    struct utsname st;
    uname (&st);
    out_string (st.machine);
    static char buf[4096];
    tsnprintf (buf, sizeof (buf), "%.999s %.999s %.999s\n", st.sysname, st.release, st.version);
    out_string (buf);
    out_string (TGL_VERSION " (build " TGL_BUILD ")");
    out_string ("En");
  } else { 
    out_string ("x86");
    out_string ("Linux");
    out_string (TGL_VERSION);
    out_string ("en");
  }
}

/* {{{ Get config */

static void fetch_dc_option (void) {
  assert (fetch_int () == CODE_dc_option);
  int id = fetch_int ();
  int l1 = prefetch_strlen ();
  char *name = fetch_str (l1);
  int l2 = prefetch_strlen ();
  char *ip = fetch_str (l2);
  int port = fetch_int ();
  vlogprintf (E_DEBUG, "id = %d, name = %.*s ip = %.*s port = %d\n", id, l1, name, l2, ip, port);

  bl_do_dc_option (id, l1, name, l2, ip, port);
}

static int help_get_config_on_answer (struct query *q UU) {
  unsigned op = fetch_int ();
  assert (op == CODE_config || op == CODE_config_old);
  fetch_int ();

  unsigned test_mode = fetch_int ();
  assert (test_mode == CODE_bool_true || test_mode == CODE_bool_false);
  assert (test_mode == CODE_bool_false || test_mode == CODE_bool_true);
  int this_dc = fetch_int ();
  vlogprintf (E_DEBUG, "this_dc = %d\n", this_dc);
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  assert (n <= 10);
  int i;
  for (i = 0; i < n; i++) {
    fetch_dc_option ();
  }
  int max_chat_size = fetch_int ();
  int max_bcast_size = 0;
  if (op == CODE_config) {
    max_bcast_size = fetch_int ();
  }
  vlogprintf (E_DEBUG, "chat_size = %d, bcast_size = %d\n", max_chat_size, max_bcast_size);

  if (q->callback) {
    ((void (*)(void *, int))(q->callback))(q->callback_extra, 1);
  }
  return 0;
}

static struct query_methods help_get_config_methods  = {
  .on_answer = help_get_config_on_answer,
  .type = TYPE_TO_PARAM(config)
};

void tgl_do_help_get_config (void (*callback)(void *, int), void *callback_extra) {
  clear_packet ();  
  tgl_do_insert_header ();
  out_int (CODE_help_get_config);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &help_get_config_methods, 0, callback, callback_extra);
}

void tgl_do_help_get_config_dc (struct tgl_dc *D, void (*callback)(void *, int), void *callback_extra) {
  clear_packet ();  
  tgl_do_insert_header ();
  out_int (CODE_help_get_config);
  tglq_send_query_ex (D, packet_ptr - packet_buffer, packet_buffer, &help_get_config_methods, 0, callback, callback_extra, 2);
}
/* }}} */

/* {{{ Send code */
static int send_code_on_answer (struct query *q UU) {
  static char *phone_code_hash;  
  assert (fetch_int () == (int)CODE_auth_sent_code);
  int registered = fetch_bool ();
  int l = prefetch_strlen ();
  char *s = fetch_str (l);
  if (phone_code_hash) {
    tfree_str (phone_code_hash);
  }
  phone_code_hash = tstrndup (s, l);
  fetch_int (); 
  fetch_bool ();
  tfree_str (q->extra);
  
  if (q->callback) {
    ((void (*)(void *, int, int, const char *))(q->callback)) (q->callback_extra, 1, registered, phone_code_hash);
  }
  return 0;
}

static int send_code_on_error (struct query *q UU, int error_code, int l, char *error) {
  int s = strlen ("PHONE_MIGRATE_");
  int s2 = strlen ("NETWORK_MIGRATE_");
  int want_dc_num = 0;
  if (l >= s && !memcmp (error, "PHONE_MIGRATE_", s)) {
    int i = error[s] - '0';
    want_dc_num = i;
  } else if (l >= s2 && !memcmp (error, "NETWORK_MIGRATE_", s2)) {
    int i = error[s2] - '0';
    want_dc_num = i;
  } else {
    vlogprintf (E_ERROR, "error_code = %d, error = %.*s\n", error_code, l, error);
    assert (0);
  }
  bl_do_set_working_dc (want_dc_num);
  //if (q->callback) {
  //  ((void (*)(void *, int, int, const char *))(q->callback)) (q->callback_extra, 0, 0, 0);
  //}
  assert (tgl_state.DC_working->id == want_dc_num);
  tgl_do_send_code (q->extra, q->callback, q->callback_extra);
  tfree_str (q->extra);
  return 0;
}

static struct query_methods send_code_methods  = {
  .on_answer = send_code_on_answer,
  .on_error = send_code_on_error,
  .type = TYPE_TO_PARAM(auth_sent_code)
};

//char *suser;
//extern int dc_working_num;
void tgl_do_send_code (const char *user, void (*callback)(void *callback_extra, int success, int registered, const char *hash), void *callback_extra) {
  vlogprintf (E_DEBUG, "sending code to dc %d\n", tgl_state.dc_working_num);
  //suser = tstrdup (user);
  clear_packet ();
  tgl_do_insert_header ();
  out_int (CODE_auth_send_code);
  out_string (user);
  out_int (0);
  out_int (TG_APP_ID);
  out_string (TG_APP_HASH);
  out_string ("en");

  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &send_code_methods, tstrdup (user), callback, callback_extra);
}


static int phone_call_on_answer (struct query *q UU) {
  fetch_bool ();
  if (q->callback) {
    ((void (*)(void *, int))(q->callback))(q->callback_extra, 1);
  }
  return 0;
}

static struct query_methods phone_call_methods  = {
  .on_answer = phone_call_on_answer,
  .type = TYPE_TO_PARAM(bool)
};

void tgl_do_phone_call (const char *user, const char *hash,void (*callback)(void *callback_extra, int success), void *callback_extra) {
  vlogprintf (E_DEBUG, "calling user\n");
  //suser = tstrdup (user);
  //want_dc_num = 0;
  clear_packet ();
  tgl_do_insert_header ();
  out_int (CODE_auth_send_call);
  out_string (user);
  out_string (hash);

  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &phone_call_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Check phone */
/*int check_phone_result;
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
    tgl_state.DC_working = DC_list[i];
    write_auth_file ();

    bl_do_set_working_dc (i);

    check_phone_result = 1;
  } else if (l >= s2 && !memcmp (error, "NETWORK_MIGRATE_", s2)) {
    int i = error[s2] - '0';
    assert (DC_list[i]);
    dc_working_num = i;

    bl_do_set_working_dc (i);

    tgl_state.DC_working = DC_list[i];
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
  .on_error = check_phone_on_error,
  .type = TYPE_TO_PARAM(auth_checked_phone)
};

int tgl_do_auth_check_phone (const char *user) {
  suser = tstrdup (user);
  clear_packet ();
  out_int (CODE_auth_check_phone);
  out_string (user);
  check_phone_result = -1;
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &check_phone_methods, 0);
  net_loop (0, cr_f);
  check_phone_result = -1;
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &check_phone_methods, 0);
  net_loop (0, cr_f);
  return check_phone_result;
}*/
/* }}} */

/* {{{ Nearest DC */
/*int nearest_dc_num;
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

struct query_methods nearest_dc_methods = {
  .on_answer = nearest_dc_on_answer,
  .on_error = fail_on_error,
  .type = TYPE_TO_PARAM(nearest_dc)
};

int tgl_do_get_nearest_dc (void) {
  clear_packet ();
  out_int (CODE_help_get_nearest_dc);
  nearest_dc_num = -1;
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &nearest_dc_methods, 0);
  net_loop (0, nr_f);
  return nearest_dc_num;
}*/
/* }}} */

/* {{{ Sign in / Sign up */
static int sign_in_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_auth_authorization);
  int expires = fetch_int ();
  vlogprintf (E_DEBUG, "Expires in %d\n", expires);

  struct tgl_user *U = tglf_fetch_alloc_user ();
  
  tgl_state.DC_working->has_auth = 1;

  bl_do_dc_signed (tgl_state.DC_working->id);

  if (q->callback) {
    ((void (*)(void *, int, struct tgl_user *))q->callback) (q->callback_extra, 1, U);
  }

  return 0;
}

static struct query_methods sign_in_methods  = {
  .on_answer = sign_in_on_answer,
  .type = TYPE_TO_PARAM(auth_authorization)
};

int tgl_do_send_code_result (const char *user, const char *hash, const char *code, void (*callback)(void *callback_extra, int success, struct tgl_user *Self), void *callback_extra) {
  clear_packet ();
  out_int (CODE_auth_sign_in);
  out_string (user);
  out_string (hash);
  out_string (code);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &sign_in_methods, 0, callback, callback_extra);
  return 0;
}

int tgl_do_send_code_result_auth (const char *user, const char *hash, const char *code, const char *first_name, const char *last_name, void (*callback)(void *callback_extra, int success, struct tgl_user *Self), void *callback_extra) {
  clear_packet ();
  out_int (CODE_auth_sign_up);
  out_string (user);
  out_string (hash);
  out_string (code);
  out_string (first_name);
  out_string (last_name);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &sign_in_methods, 0, callback, callback_extra);
  return 0;
}
/* }}} */

/* {{{ Get contacts */
static int get_contacts_on_answer (struct query *q UU) {
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
  struct tgl_user **list = talloc (sizeof (void *) * n);
  for (i = 0; i < n; i++) {
    list[i] = tglf_fetch_alloc_user ();
  }
  if (q->callback) {
    ((void (*)(void *, int, int, struct tgl_user **))q->callback) (q->callback_extra, 1, n, list);  
  }
  tfree (list, sizeof (void *) * n); 
/*  for (i = 0; i < n; i++) {
    struct tgl_user *U = tglf_fetch_alloc_user ();
    print_start ();
    push_color (COLOR_YELLOW);
    printf ("User #%d: ", tgl_get_peer_id (U->id));
    print_user_name (U->id, (tgl_peer_t *)U);
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
  }*/
  return 0;
}

static struct query_methods get_contacts_methods = {
  .on_answer = get_contacts_on_answer,
  .type = TYPE_TO_PARAM(contacts_contacts)
};


void tgl_do_update_contact_list (void (*callback) (void *callback_extra, int success, int size, struct tgl_user *contacts[]), void *callback_extra) {
  clear_packet ();
  out_int (CODE_contacts_get_contacts);
  out_string ("");
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &get_contacts_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Encrypt decrypted */
static int *encr_extra;
static int *encr_ptr;
static int *encr_end;

static char *encrypt_decrypted_message (struct tgl_secret_chat *E) {
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
  memset (&aes_key, 0, sizeof (aes_key));

  return (void *)msg_key;
}

static void encr_start (void) {
  encr_extra = packet_ptr;
  packet_ptr += 1; // str len
  packet_ptr += 2; // fingerprint
  packet_ptr += 4; // msg_key
  packet_ptr += 1; // len
}


static void encr_finish (struct tgl_secret_chat *E) {
  int l = packet_ptr - (encr_extra +  8);
  while (((packet_ptr - encr_extra) - 3) & 3) {  
    int t;
    tglt_secure_random (&t, 4);
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

void tgl_do_send_encr_chat_layer (struct tgl_secret_chat *E) {
  long long t;
  tglt_secure_random (&t, 8);
  int action[2];
  action[0] = CODE_decrypted_message_action_notify_layer;
  action[1] = TGL_ENCRYPTED_LAYER;
  bl_do_send_message_action_encr (t, tgl_state.our_id, tgl_get_peer_type (E->id), tgl_get_peer_id (E->id), time (0), 2, action);

  struct tgl_message *M = tgl_message_get (t);
  assert (M);
  assert (M->action.type == tgl_message_action_notify_layer);
  tgl_do_send_msg (M, 0, 0);
  //print_message (M);
}

/* {{{ Seng msg (plain text) */
static int msg_send_encr_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_messages_sent_encrypted_message);
  struct tgl_message *M = q->extra;
  //M->date = fetch_int ();
  fetch_int ();
  if (M->flags & FLAG_PENDING) {
    bl_do_set_message_sent (M);
    bl_do_msg_update (M->id);
  }

  if (q->callback) {
    ((void (*)(void *, int, struct tgl_message *))q->callback) (q->callback_extra, 1, M);
  }
  return 0;
}

static int msg_send_on_answer (struct query *q UU) {
  unsigned x = fetch_int ();
  assert (x == CODE_messages_sent_message || x == CODE_messages_sent_message_link);
  int id = fetch_int (); // id
  struct tgl_message *M = q->extra;
  if (M->id != id) {
    bl_do_set_msg_id (M, id);
  }
  int date = fetch_int ();
  int pts = fetch_int ();
  //tglu_fetch_seq ();
  //bl_do_
  int seq = fetch_int ();
  if (seq == tgl_state.seq + 1 && !(tgl_state.locks & TGL_LOCK_DIFF)) {
    bl_do_set_date (date);
    bl_do_set_pts (pts);
    bl_do_msg_seq_update (id);
  } else {
    if (seq > tgl_state.seq + 1) {
      vlogprintf (E_NOTICE, "Hole in seq\n");
      tgl_do_get_difference (0, 0, 0);
    }
  }
  if (x == CODE_messages_sent_message_link) {
    assert (skip_type_any (TYPE_TO_PARAM_1 (vector, TYPE_TO_PARAM (contacts_link))) >= 0);
  }
  /*if (x == CODE_messages_sent_message_link) {
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
      struct tgl_user *U = tglf_fetch_alloc_user ();
  
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
  }*/
  if (M->flags & FLAG_PENDING) {
    bl_do_set_message_sent (M);
  }
  if (q->callback) {
    ((void (*)(void *, int, struct tgl_message *))q->callback) (q->callback_extra, 1, M);
  }
  return 0;
}

static int msg_send_on_error (struct query *q, int error_code, int error_len, char *error) {
  vlogprintf (E_WARNING, "error for query #%lld: #%d :%.*s\n", q->msg_id, error_code, error_len, error);
  struct tgl_message *M = q->extra;
  if (q->callback) {
    ((void (*)(void *, int, struct tgl_message *))q->callback) (q->callback_extra, 0, M);
  }
  bl_do_delete_msg (M);
  return 0;
}

static struct query_methods msg_send_methods = {
  .on_answer = msg_send_on_answer,
  .on_error = msg_send_on_error,
  .type = TYPE_TO_PARAM(messages_sent_message)
};

static struct query_methods msg_send_encr_methods = {
  .on_answer = msg_send_encr_on_answer,
  .type = TYPE_TO_PARAM(messages_sent_encrypted_message)
};

//int out_message_num;

void tgl_do_send_encr_msg_action (struct tgl_message *M, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  tgl_peer_t *P = tgl_peer_get (M->to_id);
  if (!P || P->encr_chat.state != sc_ok) { 
    vlogprintf (E_WARNING, "Unknown encrypted chat\n");
    if (callback) {
      ((void (*)(void *, int, struct tgl_message *))callback) (callback_extra, 0, M);
    }
    return;
  }
  
  clear_packet ();
  out_int (CODE_messages_send_encrypted_service);
  out_int (CODE_input_encrypted_chat);
  out_int (tgl_get_peer_id (M->to_id));
  out_long (P->encr_chat.access_hash);
  out_long (M->id);
  encr_start ();
  out_int (CODE_decrypted_message_service);
  out_long (M->id);
  static int buf[4];
  tglt_secure_random (buf, 16);
  out_cstring ((void *)buf, 16);

  switch (M->action.type) {
  case tgl_message_action_notify_layer:
    out_int (CODE_decrypted_message_action_notify_layer);
    out_int (M->action.layer);
    break;
  default:
    assert (0);
  }
  encr_finish (&P->encr_chat);
  
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &msg_send_encr_methods, M, callback, callback_extra);
}

void tgl_do_send_encr_msg (struct tgl_message *M, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  if (M->service) {
    tgl_do_send_encr_msg_action (M, callback, callback_extra);
    return;
  }
  tgl_peer_t *P = tgl_peer_get (M->to_id);
  if (!P || P->encr_chat.state != sc_ok) { 
    vlogprintf (E_WARNING, "Unknown encrypted chat\n");
    if (callback) {
      ((void (*)(void *, int, struct tgl_message *))callback) (callback_extra, 0, M);
    }
    return;
  }
  
  clear_packet ();
  out_int (CODE_messages_send_encrypted);
  out_int (CODE_input_encrypted_chat);
  out_int (tgl_get_peer_id (M->to_id));
  out_long (P->encr_chat.access_hash);
  out_long (M->id);
  encr_start ();
  out_int (CODE_decrypted_message);
  out_long (M->id);
  static int buf[4];
  tglt_secure_random (buf, 16);
  out_cstring ((void *)buf, 16);
  out_cstring ((void *)M->message, M->message_len);
  out_int (CODE_decrypted_message_media_empty);
  encr_finish (&P->encr_chat);
  
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &msg_send_encr_methods, M, callback, callback_extra);
}

void tgl_do_send_msg (struct tgl_message *M, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  if (tgl_get_peer_type (M->to_id) == TGL_PEER_ENCR_CHAT) {
    tgl_do_send_encr_msg (M, callback, callback_extra);
    return;
  }
  clear_packet ();
  out_int (CODE_messages_send_message);
  out_peer_id (M->to_id);
  out_cstring (M->message, M->message_len);
  out_long (M->id);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &msg_send_methods, M, callback, callback_extra);
}

void tgl_do_send_message (tgl_peer_id_t id, const char *msg, int len, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  if (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT) {
    tgl_peer_t *P = tgl_peer_get (id);
    if (!P) {
      vlogprintf (E_WARNING, "Unknown encrypted chat\n");
      if (callback) {
        ((void (*)(void *, int, struct tgl_message *))callback) (callback_extra, 0, 0);
      }
      return;
    }
    if (P->encr_chat.state != sc_ok) {
      vlogprintf (E_WARNING, "Chat is not yet initialized\n");
      if (callback) {
        ((void (*)(void *, int, struct tgl_message *))callback) (callback_extra, 0, 0);
      }
      return;
    }
  }
  long long t;
  tglt_secure_random (&t, 8);
  vlogprintf (E_DEBUG, "t = %lld, len = %d\n", t, len);
  bl_do_send_message_text (t, tgl_state.our_id, tgl_get_peer_type (id), tgl_get_peer_id (id), time (0), len, msg);
  struct tgl_message *M = tgl_message_get (t);
  assert (M);
  tgl_do_send_msg (M, callback, callback_extra);
  //print_message (M);
}
/* }}} */

/* {{{ Send text file */
void tgl_do_send_text (tgl_peer_id_t id, char *file_name, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  int fd = open (file_name, O_RDONLY);
  if (fd < 0) {
    vlogprintf (E_WARNING, "No such file '%s'\n", file_name);
    if (callback) {
      ((void (*)(void *, int, struct tgl_message *))callback) (callback_extra, 0, 0);
    }
    return;
  }
  static char buf[(1 << 20) + 1];
  int x = read (fd, buf, (1 << 20) + 1);
  assert (x >= 0);
  if (x == (1 << 20) + 1) {
    vlogprintf (E_WARNING, "Too big file '%s'\n", file_name);
    close (fd);
    if (callback) {
      ((void (*)(void *, int, struct tgl_message *))callback) (callback_extra, 0, 0);
    }
  } else {
    buf[x] = 0;
    tgl_do_send_message (id, buf, x, callback, callback_extra);
    //tfree_str (file_name);
    close (fd);
  }
}
/* }}} */

/* {{{ Mark read */
void tgl_do_messages_mark_read (tgl_peer_id_t id, int max_id, int offset, void (*callback)(void *callback_extra, int), void *callback_extra);
static int mark_read_on_receive (struct query *q UU) {
  assert (fetch_int () == (int)CODE_messages_affected_history);
  //tglu_fetch_pts ();
  int pts = fetch_int ();
  //tglu_fetch_seq ();
  int seq = fetch_int (); // seq

  if (seq == tgl_state.seq + 1 && !(tgl_state.locks & TGL_LOCK_DIFF)) {
    bl_do_set_pts (pts);
    bl_do_set_seq (seq);
  } else {
    if (seq > tgl_state.seq + 1) {
      vlogprintf (E_NOTICE, "Hole in seq\n");
      tgl_do_get_difference (0, 0, 0);
    }
  }

  int offset = fetch_int (); // offset
  int *t = q->extra;
  if (offset > 0) {
    tgl_do_messages_mark_read (tgl_set_peer_id (t[0], t[1]), t[2], offset, q->callback, q->callback_extra);
  } else {
    if (q->callback) {
      ((void (*)(void *, int))q->callback)(q->callback_extra, 1);
    }
  }
  tfree (t, 12);
  return 0;
}

static int mark_read_encr_on_receive (struct query *q UU) {
  fetch_bool ();
  if (q->callback) {
    ((void (*)(void *, int))q->callback)(q->callback_extra, 1);
  }
  return 0;
}

static struct query_methods mark_read_methods = {
  .on_answer = mark_read_on_receive,
  .type = TYPE_TO_PARAM(messages_affected_history)
};

static struct query_methods mark_read_encr_methods = {
  .on_answer = mark_read_encr_on_receive,
  .type = TYPE_TO_PARAM(bool)
};

void tgl_do_messages_mark_read (tgl_peer_id_t id, int max_id, int offset, void (*callback)(void *callback_extra, int), void *callback_extra) {
  clear_packet ();
  out_int (CODE_messages_read_history);
  out_peer_id (id);
  out_int (max_id);
  out_int (offset);
  int *t = talloc (12);
  t[0] = tgl_get_peer_type (id);
  t[1] = tgl_get_peer_id (id);
  t[2] = max_id;
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &mark_read_methods, t, callback, callback_extra);
}

void tgl_do_messages_mark_read_encr (tgl_peer_id_t id, long long access_hash, int last_time, void (*callback)(void *callback_extra, int), void *callback_extra) {
  clear_packet ();
  out_int (CODE_messages_read_encrypted_history);
  out_int (CODE_input_encrypted_chat);
  out_int (tgl_get_peer_id (id));
  out_long (access_hash);
  out_int (last_time);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &mark_read_encr_methods, 0, callback, callback_extra);
}

void tgl_do_mark_read (tgl_peer_id_t id, void (*callback)(void *callback_extra, int success), void *callback_extra) {
  if (tgl_get_peer_type (id) == TGL_PEER_USER || tgl_get_peer_type (id) == TGL_PEER_CHAT) {
    tgl_do_messages_mark_read (id, tgl_state.max_msg_id, 0, callback, callback_extra);
    return;
  }
  tgl_peer_t *P = tgl_peer_get (id);
  if (!P) {
    vlogprintf (E_WARNING, "Unknown peer\n");
    callback (callback_extra, 0);
    return;
  }
  assert (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT);
  if (P->last) {
    tgl_do_messages_mark_read_encr (id, P->encr_chat.access_hash, P->last->date, callback, callback_extra);
  } else {
    tgl_do_messages_mark_read_encr (id, P->encr_chat.access_hash, time (0) - 10, callback, callback_extra);
    
  }
}
/* }}} */

/* {{{ Get history */
void _tgl_do_get_history (tgl_peer_id_t id, int limit, int offset, int list_offset, int list_size, struct tgl_message *ML[], void (*callback)(void *callback_extra, int success, int size, struct tgl_message *list[]), void *callback_extra);
static int get_history_on_answer (struct query *q UU) {
  int count = -1;
  int i;
  int x = fetch_int ();
  assert (x == (int)CODE_messages_messages_slice || x == (int)CODE_messages_messages);
  if (x == (int)CODE_messages_messages_slice) {
    count = fetch_int ();
    //fetch_int ();
  }
  assert (fetch_int () == CODE_vector);
  void **T = q->extra;
  struct tgl_message **ML = T[0];
  int list_offset = (long)T[1];
  int list_size = (long)T[2];
  tgl_peer_id_t id = tgl_set_peer_id ((long)T[4], (long)T[3]);
  int limit = (long)T[5];
  int offset = (long)T[6];
  tfree (T, sizeof (void *) * 7);
  
  int n = fetch_int ();

  if (list_size - list_offset < n) {
    int new_list_size = 2 * list_size;
    if (new_list_size - list_offset < n) {
      new_list_size = n + list_offset;
    }
    ML = trealloc (ML, list_size * sizeof (void *), new_list_size * sizeof (void *));
    assert (ML);
    list_size = new_list_size;
  }
  //struct tgl_message **ML = talloc (sizeof (void *) * n);
  for (i = 0; i < n; i++) {
    ML[i + list_offset] = tglf_fetch_alloc_message ();
  }
  list_offset += n;
  offset += n;
  limit -= n;
  if (count >= 0 && limit + offset >= count) {
    limit = count - offset;
    if (limit < 0) { limit = 0; }
  }
  assert (limit >= 0);
  
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_chat ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_user ();
  }

 
  if (limit <= 0 || x == (int)CODE_messages_messages) {
    if (q->callback) {
      ((void (*)(void *, int, int, struct tgl_message **))q->callback) (q->callback_extra, 1, list_offset, ML);
    }
    if (list_offset > 0) {
      tgl_do_messages_mark_read (id, ML[0]->id, 0, 0, 0);
    }

  
    tfree (ML, sizeof (void *) * list_size);
  } else {
   _tgl_do_get_history (id, limit, offset, list_offset, list_size, ML, q->callback, q->callback_extra);
  }
  return 0;
}

static struct query_methods get_history_methods = {
  .on_answer = get_history_on_answer,
  .type = TYPE_TO_PARAM(messages_messages)
};

void tgl_do_get_local_history (tgl_peer_id_t id, int limit, void (*callback)(void *callback_extra, int success, int size, struct tgl_message *list[]), void *callback_extra) {
  tgl_peer_t *P = tgl_peer_get (id);
  if (!P || !P->last) { 
    callback (callback_extra, 0, 0, 0);
    return; 
  }
  struct tgl_message *M = P->last;
  int count = 1;
  assert (!M->prev);
  while (count < limit && M->next) {
    M = M->next;
    count ++;
  }
  struct tgl_message **ML = talloc (sizeof (void *) * count);
  M = P->last;
  ML[0] = M;
  count = 1;
  while (count < limit && M->next) {
    M = M->next;
    ML[count ++] = M;
  }

  callback (callback_extra, 1, count, ML);
  tfree (ML, sizeof (void *) * count);
}

void tgl_do_get_local_history_ext (tgl_peer_id_t id, int offset, int limit, void (*callback)(void *callback_extra, int success, int size, struct tgl_message *list[]), void *callback_extra) {
  tgl_peer_t *P = tgl_peer_get (id);
  if (!P || !P->last) { 
    callback (callback_extra, 0, 0, 0);
    return; 
  }
  struct tgl_message *M = P->last;
  int count = 1;
  assert (!M->prev);
  while (count < limit + offset && M->next) {
    M = M->next;
    count ++;
  }
  if (count <= offset) {
    callback (callback_extra, 1, 0, 0);
    return;
  }
  struct tgl_message **ML = talloc (sizeof (void *) * (count - offset));
  M = P->last;
  ML[0] = M;
  count = 1;
  while (count < limit && M->next) {
    M = M->next;
    if (count >= offset) {
      ML[count - offset] = M;
    }
    count ++;
  }

  callback (callback_extra, 1, count - offset, ML);
  tfree (ML, sizeof (void *) * (count) - offset);
}



void _tgl_do_get_history (tgl_peer_id_t id, int limit, int offset, int list_offset, int list_size, struct tgl_message *ML[], void (*callback)(void *callback_extra, int success, int size, struct tgl_message *list[]), void *callback_extra) {
  void **T = talloc (sizeof (void *) * 7);
  T[0] = ML;
  T[1] = (void *)(long)list_offset;
  T[2] = (void *)(long)list_size;
  T[3] = (void *)(long)tgl_get_peer_id (id);
  T[4] = (void *)(long)tgl_get_peer_type (id);
  T[5] = (void *)(long)limit;
  T[6] = (void *)(long)offset;

  clear_packet ();
  out_int (CODE_messages_get_history);
  out_peer_id (id);
  out_int (offset);
  out_int (0);
  out_int (limit);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &get_history_methods, T, callback, callback_extra);
}

void tgl_do_get_history (tgl_peer_id_t id, int limit, int offline_mode, void (*callback)(void *callback_extra, int success, int size, struct tgl_message *list[]), void *callback_extra) {
  if (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT || offline_mode) {
    tgl_do_get_local_history (id, limit, callback, callback_extra);
    tgl_do_mark_read (id, 0, 0);
    return;
  }
  _tgl_do_get_history (id, limit, 0, 0, 0, 0, callback, callback_extra);
}

void tgl_do_get_history_ext (tgl_peer_id_t id, int offset, int limit, int offline_mode, void (*callback)(void *callback_extra, int success, int size, struct tgl_message *list[]), void *callback_extra) {
  if (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT || offline_mode) {
    tgl_do_get_local_history (id, limit, callback, callback_extra);
    tgl_do_mark_read (id, 0, 0);
    return;
  }
  _tgl_do_get_history (id, limit, offset, 0, 0, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Get dialogs */
static int get_dialogs_on_answer (struct query *q UU) {
  unsigned x = fetch_int (); 
  assert (x == CODE_messages_dialogs || x == CODE_messages_dialogs_slice);
  if (x == CODE_messages_dialogs_slice) {
    fetch_int (); // total_count
  }
  assert (fetch_int () == CODE_vector);
  int n, i;
  n = fetch_int ();
  int dl_size = n;

  tgl_peer_id_t *PL = talloc0 (sizeof (tgl_peer_id_t) * n);
  int *UC = talloc0 (4 * n);
  int *LM = talloc0 (4 * n);
  for (i = 0; i < n; i++) {
    assert (fetch_int () == (int)CODE_dialog);
    PL[i] = tglf_fetch_peer_id ();
    LM[i] = fetch_int ();
    UC[i] = fetch_int ();
    assert (skip_type_any (TYPE_TO_PARAM (peer_notify_settings)) >= 0);
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_message ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_chat ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_user ();
  }
  /*print_start ();
  push_color (COLOR_YELLOW);
  for (i = dl_size - 1; i >= 0; i--) {
    tgl_peer_t *UC;
    switch (tgl_get_peer_type (plist[i])) {
    case TGL_PEER_USER:
      UC = tgl_peer_get (plist[i]);
      printf ("User ");
      print_user_name (plist[i], UC);
      printf (": %d unread\n", dlist[2 * i + 1]);
      break;
    case TGL_PEER_CHAT:
      UC = tgl_peer_get (plist[i]);
      printf ("Chat ");
      print_chat_name (plist[i], UC);
      printf (": %d unread\n", dlist[2 * i + 1]);
      break;
    }
  }
  pop_color ();
  print_end ();

  dialog_list_got = 1;*/

  if (q->callback) {
    ((void (*)(void *, int, int, tgl_peer_id_t *, int *, int *))q->callback) (q->callback_extra, 1, dl_size, PL, LM, UC);
  }
  tfree (PL, sizeof (tgl_peer_id_t) * dl_size);
  tfree (UC, 4 * dl_size);
  tfree (LM, 4 * dl_size);
  
  return 0;
}

static struct query_methods get_dialogs_methods = {
  .on_answer = get_dialogs_on_answer,
  .type = TYPE_TO_PARAM(messages_dialogs)
};


void tgl_do_get_dialog_list (void (*callback)(void *callback_extra, int success, int size, tgl_peer_id_t peers[], int last_msg_id[], int unread_count[]), void *callback_extra) {
  clear_packet ();
  out_int (CODE_messages_get_dialogs);
  out_int (0);
  out_int (0);
  out_int (1000);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &get_dialogs_methods, 0, callback, callback_extra);
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
  tgl_peer_id_t to_id;
  unsigned media_type;
  char *file_name;
  int encr;
  int avatar;
  unsigned char *iv;
  unsigned char *init_iv;
  unsigned char *key;
};

static void out_peer_id (tgl_peer_id_t id) {
  tgl_peer_t *U;
  switch (tgl_get_peer_type (id)) {
  case TGL_PEER_CHAT:
    out_int (CODE_input_peer_chat);
    out_int (tgl_get_peer_id (id));
    break;
  case TGL_PEER_USER:
    U = tgl_peer_get (id);
    if (U && U->user.access_hash) {
      out_int (CODE_input_peer_foreign);
      out_int (tgl_get_peer_id (id));
      out_long (U->user.access_hash);
    } else {
      out_int (CODE_input_peer_contact);
      out_int (tgl_get_peer_id (id));
    }
    break;
  default:
    assert (0);
  }
}

static void send_part (struct send_file *f, void *callback, void *callback_extra);
static int send_file_part_on_answer (struct query *q) {
  assert (fetch_int () == (int)CODE_bool_true);
  send_part (q->extra, q->callback, q->callback_extra);
  return 0;
}

static int send_file_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_messages_stated_message);
  struct tgl_message *M = tglf_fetch_alloc_message ();
  assert (fetch_int () == CODE_vector);
  int n, i;
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_chat ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_user ();
  }
  //tglu_fetch_pts ();
  int pts = fetch_int ();
  //tglu_fetch_seq ();
  
  int seq = fetch_int ();
  if (seq == tgl_state.seq + 1 && !(tgl_state.locks & TGL_LOCK_DIFF)) {
    bl_do_set_pts (pts);
    bl_do_msg_seq_update (M->id);
  } else {
    if (seq > tgl_state.seq + 1) {
      vlogprintf (E_NOTICE, "Hole in seq\n");
      tgl_do_get_difference (0, 0, 0);
    }
  }

  if (q->callback) {
    ((void (*)(void *, int, struct tgl_message *))q->callback)(q->callback_extra, 1, M);
  }
  //print_message (M);
  return 0;
}

static int send_encr_file_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_messages_sent_encrypted_file);
  struct tgl_message *M = q->extra;
  M->date = fetch_int ();
  assert (fetch_int () == CODE_encrypted_file);
  M->media.encr_photo.id = fetch_long ();
  M->media.encr_photo.access_hash = fetch_long ();
  //M->media.encr_photo.size = fetch_int ();
  fetch_int ();
  M->media.encr_photo.dc_id = fetch_int ();
  assert (fetch_int () == M->media.encr_photo.key_fingerprint);
  //print_message (M);
  tglm_message_insert (M);
  bl_do_msg_update (M->id);
  
  if (q->callback) {
    ((void (*)(void *, int, struct tgl_message *))q->callback)(q->callback_extra, 1, M);
  }
  return 0;
}

static int set_photo_on_answer (struct query *q) {
  assert (skip_type_any (TYPE_TO_PARAM(photos_photo)) >= 0);
  if (q->callback) {
    ((void (*)(void *, int))q->callback)(q->callback_extra, 1);
  }
  return 0;
}

static struct query_methods send_file_part_methods = {
  .on_answer = send_file_part_on_answer,
  .type = TYPE_TO_PARAM(bool)
};

static struct query_methods send_file_methods = {
  .on_answer = send_file_on_answer,
  .type = TYPE_TO_PARAM(messages_stated_message)
};

static struct query_methods set_photo_methods = {
  .on_answer = set_photo_on_answer,
  .type = TYPE_TO_PARAM(photos_photo)
};

static struct query_methods send_encr_file_methods = {
  .on_answer = send_encr_file_on_answer,
  .type = TYPE_TO_PARAM(messages_sent_encrypted_message)
};

static void send_part (struct send_file *f, void *callback, void *callback_extra) {
  if (f->fd >= 0) {
    if (!f->part_num) {
      tgl_state.cur_uploading_bytes += f->size;
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
    tgl_state.cur_uploaded_bytes += x;
    
    if (f->encr) {
      if (x & 15) {
        assert (f->offset == f->size);
        tglt_secure_random (buf + x, (-x) & 15);
        x = (x + 15) & ~15;
      }
      
      AES_KEY aes_key;
      AES_set_encrypt_key (f->key, 256, &aes_key);
      AES_ige_encrypt ((void *)buf, (void *)buf, x, &aes_key, f->iv, 1);
      memset (&aes_key, 0, sizeof (aes_key));
    }
    out_cstring (buf, x);
    vlogprintf (E_DEBUG, "offset=%lld size=%lld\n", f->offset, f->size);
    if (f->offset == f->size) {
      close (f->fd);
      f->fd = -1;
    } else {
      assert (f->part_size == x);
    }
    //update_prompt ();
    tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &send_file_part_methods, f, callback, callback_extra);
  } else {
    tgl_state.cur_uploaded_bytes -= f->size;
    tgl_state.cur_uploading_bytes -= f->size;
    //update_prompt ();
    clear_packet ();
    assert (f->media_type == CODE_input_media_uploaded_photo || f->media_type == CODE_input_media_uploaded_video || f->media_type == CODE_input_media_uploaded_thumb_video || f->media_type == CODE_input_media_uploaded_audio || f->media_type == CODE_input_media_uploaded_document || f->media_type == CODE_input_media_uploaded_thumb_document);
    if (f->avatar) {
      assert (!f->encr);
      if (f->avatar > 0) {
        out_int (CODE_messages_edit_chat_photo);
        out_int (f->avatar);
        out_int (CODE_input_chat_uploaded_photo);
        if (f->size < (16 << 20)) {
          out_int (CODE_input_file);
        } else {
          out_int (CODE_input_file_big);
        }
        out_long (f->id);
        out_int (f->part_num);
        /*char *s = f->file_name + strlen (f->file_name);
        while (s >= f->file_name && *s != '/') { s --;}
        out_string (s + 1);*/
        out_string ("");
        if (f->size < (16 << 20)) {
          out_string ("");
        }
        out_int (CODE_input_photo_crop_auto);
        tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &send_file_methods, 0, callback, callback_extra);
      } else {
        out_int (CODE_photos_upload_profile_photo);
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
        out_string ("profile photo");
        out_int (CODE_input_geo_point_empty);
        out_int (CODE_input_photo_crop_auto);
        tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &set_photo_methods, 0, callback, callback_extra);
      }
    } else if (!f->encr) {
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
        out_string ("video");
      }
      if (f->media_type == CODE_input_media_uploaded_document || f->media_type == CODE_input_media_uploaded_thumb_document) {
        out_string (s + 1);
        out_string ("text");
      }
      if (f->media_type == CODE_input_media_uploaded_audio) {
        out_int (60);
        out_string ("audio");
      }

      long long r;
      tglt_secure_random (&r, 8);
      out_long (r);
      tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &send_file_methods, 0, callback, callback_extra);
    } else {
      struct tgl_message *M = talloc0 (sizeof (*M));

      out_int (CODE_messages_send_encrypted_file);
      out_int (CODE_input_encrypted_chat);
      out_int (tgl_get_peer_id (f->to_id));
      tgl_peer_t *P = tgl_peer_get (f->to_id);
      assert (P);
      out_long (P->encr_chat.access_hash);
      long long r;
      tglt_secure_random (&r, 8);
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
        out_string ("video");
      }
      if (f->media_type == CODE_input_media_uploaded_document) {
        out_string (f->file_name);
        out_string ("text");
      }
      if (f->media_type == CODE_input_media_uploaded_audio) {
        out_int (60);
        out_string ("audio");
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
      M->from_id = TGL_MK_USER (tgl_state.our_id);
      M->to_id = f->to_id;
      M->unread = 1;
      M->message = tstrdup ("");
      M->out = 1;
      M->id = r;
      M->date = time (0);
      
      tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &send_encr_file_methods, M, callback, callback_extra);
    }
    tfree_str (f->file_name);
    tfree (f, sizeof (*f));
  }
}

/*void send_file_thumb (struct send_file *f, void *callback, void *callback_extra) {
  clear_packet ();
  f->thumb_id = lrand48 () * (1ll << 32) + lrand48 ();
  out_int (CODE_upload_save_file_part);
  out_long (f->thumb_id);
  out_int (0);
  out_cstring ((void *)thumb_file, thumb_file_size);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &send_file_part_methods, f, callback, callback_extra);
}*/

void _tgl_do_send_photo (enum tgl_message_media_type type, tgl_peer_id_t to_id, char *file_name, int avatar, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  int fd = open (file_name, O_RDONLY);
  if (fd < 0) {
    vlogprintf (E_WARNING, "No such file '%s'\n", file_name);
    callback (callback_extra, 0, 0);
    return;
  }
  struct stat buf;
  fstat (fd, &buf);
  long long size = buf.st_size;
  if (size <= 0) {
    vlogprintf (E_WARNING, "File has zero length\n");
    close (fd);
    callback (callback_extra, 0, 0);
    return;
  }
  struct send_file *f = talloc0 (sizeof (*f));
  f->fd = fd;
  f->size = size;
  f->offset = 0;
  f->part_num = 0;
  f->avatar = avatar;
  int tmp = ((size + 2999) / 3000);
  f->part_size = (1 << 10);
  while (f->part_size < tmp) {
    f->part_size *= 2;
  }

  if (f->part_size > (512 << 10)) {
    close (fd);
    vlogprintf (E_WARNING, "Too big file. Maximal supported size is %d.\n", (512 << 10) * 1000);
    tfree (f, sizeof (*f));
    callback (callback_extra, 0, 0);
    return;
  }
  
  tglt_secure_random (&f->id, 8);
  f->to_id = to_id;
  switch (type) {
  case tgl_message_media_photo:
    f->media_type = CODE_input_media_uploaded_photo;
    break;
  case tgl_message_media_video:
    f->media_type = CODE_input_media_uploaded_video;
    break;
  case tgl_message_media_audio:
    f->media_type = CODE_input_media_uploaded_audio;
    break;
  case tgl_message_media_document:
    f->media_type = CODE_input_media_uploaded_document;
    break;
  default:
    close (fd);
    vlogprintf (E_WARNING, "Unknown type %d.\n", type);
    tfree (f, sizeof (*f));
    callback (callback_extra, 0, 0);
    return;
  }
  f->file_name = tstrdup (file_name);
  if (tgl_get_peer_type (f->to_id) == TGL_PEER_ENCR_CHAT) {
    f->encr = 1;
    f->iv = talloc (32);
    tglt_secure_random (f->iv, 32);
    f->init_iv = talloc (32);
    memcpy (f->init_iv, f->iv, 32);
    f->key = talloc (32);
    tglt_secure_random (f->key, 32);
  }
  /*if (f->media_type == CODE_input_media_uploaded_video && !f->encr) {
    f->media_type = CODE_input_media_uploaded_thumb_video;
    send_file_thumb (f);
  } else if (f->media_type == CODE_input_media_uploaded_document && !f->encr) {
    f->media_type = CODE_input_media_uploaded_thumb_document;
    send_file_thumb (f);
  } else {
    send_part (f);
  }*/
  send_part (f, callback, callback_extra);
}

void tgl_do_send_photo (enum tgl_message_media_type type, tgl_peer_id_t to_id, char *file_name, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  _tgl_do_send_photo (type, to_id, file_name, 0, callback, callback_extra);
}

void tgl_do_set_chat_photo (tgl_peer_id_t chat_id, char *file_name, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  assert (tgl_get_peer_type (chat_id) == TGL_PEER_CHAT);
  _tgl_do_send_photo (tgl_message_media_photo, chat_id, file_name, tgl_get_peer_id (chat_id), callback, callback_extra);
}

void tgl_do_set_profile_photo (char *file_name, void (*callback)(void *callback_extra, int success), void *callback_extra) {
  _tgl_do_send_photo (tgl_message_media_photo, TGL_MK_USER(tgl_state.our_id), file_name, -1, (void *)callback, callback_extra);
}
/* }}} */

/* {{{ Forward */
static int fwd_msg_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_messages_stated_message);
  struct tgl_message *M = tglf_fetch_alloc_message ();
  assert (fetch_int () == CODE_vector);
  int n, i;
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_chat ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_user ();
  }
  //tglu_fetch_pts ();
  int pts = fetch_int ();
  
  int seq = fetch_int ();
  if (seq == tgl_state.seq + 1 && !(tgl_state.locks & TGL_LOCK_DIFF)) {
    bl_do_set_pts (pts);
    bl_do_msg_seq_update (M->id);
  } else {
    if (seq > tgl_state.seq + 1) {
      vlogprintf (E_NOTICE, "Hole in seq\n");
      tgl_do_get_difference (0, 0, 0);
    }
  }
  //print_message (M);
  if (q->callback) {
    ((void (*)(void *, int, struct tgl_message *))q->callback) (q->callback_extra, 1, M);
  }
  return 0;
}

static struct query_methods fwd_msg_methods = {
  .on_answer = fwd_msg_on_answer,
  .type = TYPE_TO_PARAM(messages_stated_message)
};

void tgl_do_forward_message (tgl_peer_id_t id, int n, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  if (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT) {
    vlogprintf (E_WARNING, "Can not forward messages from secret chat\n");
    callback (callback_extra, 0, 0);
    return;
  }
  clear_packet ();
  out_int (CODE_messages_forward_message);
  out_peer_id (id);
  out_int (n);
  long long r;
  tglt_secure_random (&r, 8);
  out_long (r);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &fwd_msg_methods, 0, callback, callback_extra);
}

void tgl_do_send_contact (tgl_peer_id_t id, const char *phone, int phone_len, const char *first_name, int first_name_len, const char *last_name, int last_name_len, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  if (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT) {
    if (callback) {
      ((void (*)(void *, int, struct tgl_message *))callback) (callback_extra, 0, 0);
    }
    return;
  }
  long long t;
  tglt_secure_random (&t, 8);
  vlogprintf (E_DEBUG, "t = %lld\n", t);

  clear_packet ();
  out_int (CODE_messages_send_media);
  out_peer_id (id);
  out_int (CODE_input_media_contact);
  out_cstring (phone, phone_len);
  out_cstring (first_name, first_name_len);
  out_cstring (last_name, last_name_len);
  out_long (t);

  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &fwd_msg_methods, 0, callback, callback_extra);
}

void tgl_do_forward_media (tgl_peer_id_t id, int n, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  if (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT) {
    vlogprintf (E_WARNING, "Can not forward messages from secret chat\n");
    callback (callback_extra, 0, 0);
    return;
  }
  struct tgl_message *M = tgl_message_get (n);
  if (!M) {
    vlogprintf (E_WARNING, "No such message\n");
    callback (callback_extra, 0, 0);
    return;
  }
  if (M->flags & FLAG_ENCRYPTED) {
    vlogprintf (E_WARNING, "Can not forward media from encrypted message\n");
    callback (callback_extra, 0, 0);
    return;
  }
  if (M->media.type != tgl_message_media_photo && M->media.type != tgl_message_media_video && M->media.type != tgl_message_media_audio && M->media.type != tgl_message_media_document) {
    vlogprintf (E_WARNING, "Can only forward photo/audio/video/document\n");
    callback (callback_extra, 0, 0);
    return;
  }
  clear_packet ();
  out_int (CODE_messages_send_media);
  out_peer_id (id);
  switch (M->media.type) {
  case tgl_message_media_photo:
    out_int (CODE_input_media_photo);
    out_int (CODE_input_photo);
    out_long (M->media.photo.id);
    out_long (M->media.photo.access_hash);
    break;
  case tgl_message_media_video:
    out_int (CODE_input_media_video);
    out_int (CODE_input_video);
    out_long (M->media.video.id);
    out_long (M->media.video.access_hash);
    break;
  case tgl_message_media_audio:
    out_int (CODE_input_media_audio);
    out_int (CODE_input_audio);
    out_long (M->media.audio.id);
    out_long (M->media.audio.access_hash);
    break;
  case tgl_message_media_document:
    out_int (CODE_input_media_document);
    out_int (CODE_input_document);
    out_long (M->media.document.id);
    out_long (M->media.document.access_hash);
    break;
  default:
    assert (0);
  }
  long long r;
  tglt_secure_random (&r, 8);
  out_long (r);

  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &fwd_msg_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Rename chat */
static int rename_chat_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_messages_stated_message);
  struct tgl_message *M = tglf_fetch_alloc_message ();
  assert (fetch_int () == CODE_vector);
  int n, i;
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_chat ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_user ();
  }
  //tglu_fetch_pts ();
  int pts = fetch_int ();

  int seq = fetch_int ();
  if (seq == tgl_state.seq + 1 && !(tgl_state.locks & TGL_LOCK_DIFF)) {
    bl_do_set_pts (pts);
    bl_do_msg_seq_update (M->id);
  } else {
    if (seq > tgl_state.seq + 1) {
      vlogprintf (E_NOTICE, "Hole in seq\n");
      tgl_do_get_difference (0, 0, 0);
    }
  }
  //print_message (M);
  if (q->callback) {
    ((void (*)(void *, int, struct tgl_message *))q->callback) (q->callback_extra, 1, M);
  }
  return 0;
}

static struct query_methods rename_chat_methods = {
  .on_answer = rename_chat_on_answer,
  .type = TYPE_TO_PARAM(messages_stated_message)
};

void tgl_do_rename_chat (tgl_peer_id_t id, char *name UU, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  clear_packet ();
  out_int (CODE_messages_edit_chat_title);
  assert (tgl_get_peer_type (id) == TGL_PEER_CHAT);
  out_int (tgl_get_peer_id (id));
  out_string (name);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &rename_chat_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Chat info */
/*void print_chat_info (struct tgl_chat *C) {
  tgl_peer_t *U = (void *)C;
  print_start ();
  push_color (COLOR_YELLOW);
  printf ("Chat ");
  print_chat_name (U->id, U);
  printf (" members:\n");
  int i;
  for (i = 0; i < C->user_list_size; i++) {
    printf ("\t\t");
    print_user_name (TGL_MK_USER (C->user_list[i].user_id), tgl_peer_get (TGL_MK_USER (C->user_list[i].user_id)));
    printf (" invited by ");
    print_user_name (TGL_MK_USER (C->user_list[i].inviter_id), tgl_peer_get (TGL_MK_USER (C->user_list[i].inviter_id)));
    printf (" at ");
    print_date_full (C->user_list[i].date);
    if (C->user_list[i].user_id == C->admin_id) {
      printf (" admin");
    }
    printf ("\n");
  }
  pop_color ();
  print_end ();
}*/

static int chat_info_on_answer (struct query *q UU) {
  struct tgl_chat *C = tglf_fetch_alloc_chat_full ();
  //print_chat_info (C);
  if (q->callback) {
    ((void (*)(void *, int, struct tgl_chat *))q->callback) (q->callback_extra, 1, C);
  }
  return 0;
}

static struct query_methods chat_info_methods = {
  .on_answer = chat_info_on_answer,
  .type = TYPE_TO_PARAM(messages_chat_full)
};

void tgl_do_get_chat_info (tgl_peer_id_t id, int offline_mode, void (*callback)(void *callback_extra, int success, struct tgl_chat *C), void *callback_extra) {
  if (offline_mode) {
    tgl_peer_t *C = tgl_peer_get (id);
    if (!C) {
      vlogprintf (E_WARNING, "No such chat\n");
      callback (callback_extra, 0, 0);
    } else {
      //print_chat_info (&C->chat);
      callback (callback_extra, 1, &C->chat);
    }
    return;
  }
  clear_packet ();
  out_int (CODE_messages_get_full_chat);
  assert (tgl_get_peer_type (id) == TGL_PEER_CHAT);
  out_int (tgl_get_peer_id (id));
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &chat_info_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ User info */

/*void print_user_info (struct tgl_user *U) {
  tgl_peer_t *C = (void *)U;
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
}*/

static int user_info_on_answer (struct query *q UU) {
  struct tgl_user *U = tglf_fetch_alloc_user_full ();
  if (q->callback) {
    ((void (*)(void *, int, struct tgl_user *))q->callback) (q->callback_extra, 1, U);
  }
  return 0;
}

static struct query_methods user_info_methods = {
  .on_answer = user_info_on_answer,
  .type = TYPE_TO_PARAM(user_full)
};

void tgl_do_get_user_info (tgl_peer_id_t id, int offline_mode, void (*callback)(void *callback_extra, int success, struct tgl_user *U), void *callback_extra) {
  if (offline_mode) {
    tgl_peer_t *C = tgl_peer_get (id);
    if (!C) {
      vlogprintf (E_WARNING, "No such user\n");
      callback (callback_extra, 0, 0);
    } else {
      callback (callback_extra, 1, &C->user);
    }
    return;
  }
  clear_packet ();
  out_int (CODE_users_get_full_user);
  assert (tgl_get_peer_type (id) == TGL_PEER_USER);
  tgl_peer_t *U = tgl_peer_get (id);
  if (U && U->user.access_hash) {
    out_int (CODE_input_user_foreign);
    out_int (tgl_get_peer_id (id));
    out_long (U->user.access_hash);
  } else {
    out_int (CODE_input_user_contact);
    out_int (tgl_get_peer_id (id));
  }
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &user_info_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Get user info silently */
/*int user_list_info_silent_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  int i;
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_user ();
  }
  return 0;
}

struct query_methods user_list_info_silent_methods = {
  .on_answer = user_list_info_silent_on_answer,
  .type = TYPE_TO_PARAM_1(vector, TYPE_TO_PARAM(user))
};

void tgl_do_get_user_list_info_silent (int num, int *list) {
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
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &user_list_info_silent_methods, 0);
}*/
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


static void end_load (struct download *D, void *callback, void *callback_extra) {
  tgl_state.cur_downloading_bytes -= D->size;
  tgl_state.cur_downloaded_bytes -= D->size;
  //update_prompt ();
  close (D->fd);
  /*if (D->next == 1) {
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
  }*/

  if (callback) {
    ((void (*)(void *, int, char *))callback) (callback_extra, 1, D->name);
  }

  if (D->iv) {
    tfree_secure (D->iv, 32);
  }
  tfree_str (D->name);
  tfree (D, sizeof (*D));
}

static void load_next_part (struct download *D, void *callback, void *callback_extra);
static int download_on_answer (struct query *q) {
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
  tgl_state.cur_downloaded_bytes += len;
  //update_prompt ();
  if (D->iv) {
    unsigned char *ptr = (void *)fetch_str (len);
    assert (!(len & 15));
    AES_KEY aes_key;
    AES_set_decrypt_key (D->key, 256, &aes_key);
    AES_ige_encrypt (ptr, ptr, len, &aes_key, D->iv, 0);
    memset (&aes_key, 0, sizeof (aes_key));
    if (len > D->size - D->offset) {
      len = D->size - D->offset;
    }
    assert (write (D->fd, ptr, len) == len);
  } else {
    assert (write (D->fd, fetch_str (len), len) == len);
  }
  D->offset += len;
  if (D->offset < D->size) {
    load_next_part (D, q->callback, q->callback_extra);
    return 0;
  } else {
    end_load (D, q->callback, q->callback_extra);
    return 0;
  }
}

static struct query_methods download_methods = {
  .on_answer = download_on_answer,
  .type = TYPE_TO_PARAM(upload_file)
};

static void load_next_part (struct download *D, void *callback, void *callback_extra) {
  if (!D->offset) {
    static char buf[PATH_MAX];
    int l;
    if (!D->id) {
      l = tsnprintf (buf, sizeof (buf), "%s/download_%lld_%d", get_downloads_directory (), D->volume, D->local_id);
    } else {
      l = tsnprintf (buf, sizeof (buf), "%s/download_%lld", get_downloads_directory (), D->id);
    }
    if (l >= (int) sizeof (buf)) {
      vlogprintf (E_ERROR, "Download filename is too long");
      exit (1);
    }
    D->name = tstrdup (buf);
    struct stat st;
    if (stat (buf, &st) >= 0) {
      D->offset = st.st_size;      
      if (D->offset >= D->size) {
        tgl_state.cur_downloading_bytes += D->size;
        tgl_state.cur_downloaded_bytes += D->offset;
        vlogprintf (E_NOTICE, "Already downloaded\n");
        end_load (D, callback, callback_extra);
        return;
      }
    }
    
    tgl_state.cur_downloading_bytes += D->size;
    tgl_state.cur_downloaded_bytes += D->offset;
    //update_prompt ();
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
  tglq_send_query (tgl_state.DC_list[D->dc], packet_ptr - packet_buffer, packet_buffer, &download_methods, D, callback, callback_extra);
  //tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &download_methods, D);
}

void tgl_do_load_photo_size (struct tgl_photo_size *P, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra) {
  if (!P->loc.dc) {
    vlogprintf (E_WARNING, "Bad video thumb\n");
    callback (callback_extra, 0, 0);
    return;
  }
  
  assert (P);
  struct download *D = talloc0 (sizeof (*D));
  D->id = 0;
  D->offset = 0;
  D->size = P->size;
  D->volume = P->loc.volume;
  D->dc = P->loc.dc;
  D->local_id = P->loc.local_id;
  D->secret = P->loc.secret;
  D->name = 0;
  D->fd = -1;
  load_next_part (D, callback, callback_extra);
}

void tgl_do_load_photo (struct tgl_photo *photo, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra) {
  if (!photo->sizes_num) { 
    vlogprintf (E_WARNING, "No sizes\n");
    callback (callback_extra, 0, 0);
    return; 
  }
  int max = -1;
  int maxi = 0;
  int i;
  for (i = 0; i < photo->sizes_num; i++) {
    if (photo->sizes[i].w + photo->sizes[i].h > max) {
      max = photo->sizes[i].w + photo->sizes[i].h;
      maxi = i;
    }
  }
  tgl_do_load_photo_size (&photo->sizes[maxi], callback, callback_extra);
}

void tgl_do_load_video_thumb (struct tgl_video *video, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra) {
  tgl_do_load_photo_size (&video->thumb, callback, callback_extra);
}

void tgl_do_load_document_thumb (struct tgl_document *video, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra) {
  tgl_do_load_photo_size (&video->thumb, callback, callback_extra);
}

void tgl_do_load_video (struct tgl_video *V, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra) {
  assert (V);
  struct download *D = talloc0 (sizeof (*D));
  D->offset = 0;
  D->size = V->size;
  D->id = V->id;
  D->access_hash = V->access_hash;
  D->dc = V->dc_id;
  D->name = 0;
  D->fd = -1;
  D->type = CODE_input_video_file_location;
  load_next_part (D, callback, callback_extra);
}

void tgl_do_load_audio (struct tgl_audio *V, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra) {
  assert (V);
  struct download *D = talloc0 (sizeof (*D));
  D->offset = 0;
  D->size = V->size;
  D->id = V->id;
  D->access_hash = V->access_hash;
  D->dc = V->dc_id;
  D->name = 0;
  D->fd = -1;
  D->type = CODE_input_audio_file_location;
  load_next_part (D, callback, callback_extra);
}

void tgl_do_load_document (struct tgl_document *V, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra) {
  assert (V);
  struct download *D = talloc0 (sizeof (*D));
  D->offset = 0;
  D->size = V->size;
  D->id = V->id;
  D->access_hash = V->access_hash;
  D->dc = V->dc_id;
  D->name = 0;
  D->fd = -1;
  D->type = CODE_input_document_file_location;
  load_next_part (D, callback, callback_extra);
}

void tgl_do_load_encr_video (struct tgl_encr_video *V, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra) {
  assert (V);
  struct download *D = talloc0 (sizeof (*D));
  D->offset = 0;
  D->size = V->size;
  D->id = V->id;
  D->access_hash = V->access_hash;
  D->dc = V->dc_id;
  D->name = 0;
  D->fd = -1;
  D->key = V->key;
  D->iv = talloc (32);
  memcpy (D->iv, V->iv, 32);
  load_next_part (D, callback, callback_extra);
      
  unsigned char md5[16];
  unsigned char str[64];
  memcpy (str, V->key, 32);
  memcpy (str + 32, V->iv, 32);
  MD5 (str, 64, md5);
  assert (V->key_fingerprint == ((*(int *)md5) ^ (*(int *)(md5 + 4))));
}
/* }}} */

/* {{{ Export auth */

static int import_auth_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_auth_authorization);
  fetch_int (); // expires
  tglf_fetch_alloc_user ();
  
  bl_do_dc_signed (((struct tgl_dc *)q->extra)->id);

  if (q->callback) {
    ((void (*)(void *, int))q->callback) (q->callback_extra, 1);
  }
  return 0;
}

static struct query_methods import_auth_methods = {
  .on_answer = import_auth_on_answer,
  .on_error = fail_on_error,
  .type = TYPE_TO_PARAM(auth_authorization)
};

static int export_auth_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_auth_exported_authorization);
  bl_do_set_our_id (fetch_int ());
  int l = prefetch_strlen ();
  char *s = talloc (l);
  memcpy (s, fetch_str (l), l);
  
  clear_packet ();
  tgl_do_insert_header ();
  out_int (CODE_auth_import_authorization);
  out_int (tgl_state.our_id);
  out_cstring (s, l);
  tglq_send_query (q->extra, packet_ptr - packet_buffer, packet_buffer, &import_auth_methods, q->extra, q->callback, q->callback_extra);
  tfree (s, l);
  return 0;
}

static struct query_methods export_auth_methods = {
  .on_answer = export_auth_on_answer,
  .on_error = fail_on_error,
  .type = TYPE_TO_PARAM(auth_exported_authorization)
};

void tgl_do_export_auth (int num, void (*callback) (void *callback_extra, int success), void *callback_extra) {
  clear_packet ();
  out_int (CODE_auth_export_authorization);
  out_int (num);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &export_auth_methods, tgl_state.DC_list[num], callback, callback_extra);
}
/* }}} */

/* {{{ Add contact */
static int add_contact_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_contacts_imported_contacts);
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  if (n > 0) {
    vlogprintf (E_DEBUG, "Added successfully");
  } else {
    vlogprintf (E_DEBUG, "Not added");
  }
  int i;
  for (i = 0; i < n ; i++) {
    assert (fetch_int () == (int)CODE_imported_contact);
    fetch_int (); // uid
    fetch_long (); // client_id
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    long long id = fetch_long ();
    vlogprintf (E_NOTICE, "contact #%lld not added. Please retry\n", id);
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();

  struct tgl_user **UL = talloc (n * sizeof (void *));
  for (i = 0; i < n; i++) {
    UL[i] = tglf_fetch_alloc_user ();
  }

  if (q->callback) {
    ((void (*)(void *, int, int, struct tgl_user **))q->callback) (q->callback_extra, 1, n, UL);
  }
  tfree (UL, n * sizeof (void *));
  return 0;
}

static struct query_methods add_contact_methods = {
  .on_answer = add_contact_on_answer,
  .type = TYPE_TO_PARAM(contacts_imported_contacts)
};

void tgl_do_add_contact (const char *phone, int phone_len, const char *first_name, int first_name_len, const char *last_name, int last_name_len, int force, void (*callback)(void *callback_extra, int success, int size, struct tgl_user *users[]), void *callback_extra) {
  clear_packet ();
  out_int (CODE_contacts_import_contacts);
  out_int (CODE_vector);
  out_int (1);
  out_int (CODE_input_phone_contact);
  long long r;
  tglt_secure_random (&r, 8);
  out_long (r);
  out_cstring (phone, phone_len);
  out_cstring (first_name, first_name_len);
  out_cstring (last_name, last_name_len);
  out_int (force ? CODE_bool_true : CODE_bool_false);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &add_contact_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Msg search */
static int msg_search_on_answer (struct query *q UU) {
  return get_history_on_answer (q);
}

static struct query_methods msg_search_methods = {
  .on_answer = msg_search_on_answer,
  .type = TYPE_TO_PARAM(messages_messages)
};

void tgl_do_msg_search (tgl_peer_id_t id, int from, int to, int limit, const char *s, void (*callback)(void *callback_extra, int success, int size, struct tgl_message *list[]), void *callback_extra) {
  if (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT) {
    vlogprintf (E_WARNING, "Can not search in secure chat\n");
    if (callback) {
      callback (callback_extra, 0, 0, 0);
    }
    return;
  }
  clear_packet ();
  out_int (CODE_messages_search);
  if (tgl_get_peer_type (id) == TGL_PEER_UNKNOWN) {
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
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &msg_search_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Contacts search */
static int contacts_search_on_answer (struct query *q UU) {
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

  struct tgl_user **UL = talloc (sizeof (void *) * n);
  for (i = 0; i < n; i++) {
    UL[i] = tglf_fetch_alloc_user ();
  }
  /*print_start ();
  push_color (COLOR_YELLOW);
  for (i = 0; i < n; i++) {
    struct tgl_user *U = tglf_fetch_alloc_user ();
    printf ("User ");
    push_color  (COLOR_RED);
    printf ("%s %s", U->first_name, U->last_name); 
    pop_color ();
    printf (". Phone %s\n", U->phone);
  }
  pop_color ();
  print_end ();*/
  if (q->callback) {
    ((void (*)(void *, int, int, struct tgl_user **))q->callback) (q->callback_extra, 1, n, UL);
  }
  tfree (UL, sizeof (void *) * n);
  return 0;
}

static struct query_methods contacts_search_methods = {
  .on_answer = contacts_search_on_answer,
  .type = TYPE_TO_PARAM(contacts_found)
};

void tgl_do_contacts_search (int limit, const char *s, void (*callback) (void *callback_extra, int success, int size, struct tgl_user *users[]), void *callback_extra) {
  clear_packet ();
  out_int (CODE_contacts_search);
  out_string (s);
  out_int (limit);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &contacts_search_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Encr accept */
static int send_encr_accept_on_answer (struct query *q UU) {
  struct tgl_secret_chat *E = tglf_fetch_alloc_encrypted_chat ();

  /*if (E->state == sc_ok) {
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
  }*/

  if (q->callback) {
    ((void (*)(void *, int, struct tgl_secret_chat *))q->callback) (q->callback_extra, E->state == sc_ok, E);
  }
  return 0;
}

static int send_encr_request_on_answer (struct query *q UU) {
  struct tgl_secret_chat *E = tglf_fetch_alloc_encrypted_chat ();
  /*if (E->state == sc_deleted) {
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
  }*/
  
  if (q->callback) {
    ((void (*)(void *, int, struct tgl_secret_chat *))q->callback) (q->callback_extra, E->state != sc_deleted, E);
  }
  return 0;
}

static struct query_methods send_encr_accept_methods  = {
  .on_answer = send_encr_accept_on_answer,
  .type = TYPE_TO_PARAM(encrypted_chat)
};

static struct query_methods send_encr_request_methods  = {
  .on_answer = send_encr_request_on_answer,
  .type = TYPE_TO_PARAM(encrypted_chat)
};

//int encr_root;
//unsigned char *encr_prime;
//int encr_param_version;
//static BN_CTX *ctx;

void tgl_do_send_accept_encr_chat (struct tgl_secret_chat *E, unsigned char *random, void (*callback)(void *callback_extra, int success, struct tgl_secret_chat *E), void *callback_extra) {
  int i;
  int ok = 0;
  for (i = 0; i < 64; i++) {
    if (E->key[i]) {
      ok = 1;
      break;
    }
  }
  if (ok) { 
    callback (callback_extra, 1, E);
    return; 
  } // Already generated key for this chat
  unsigned char random_here[256];
  tglt_secure_random (random_here, 256);
  for (i = 0; i < 256; i++) {
    random[i] ^= random_here[i];
  }
  BIGNUM *b = BN_bin2bn (random, 256, 0);
  ensure_ptr (b);
  BIGNUM *g_a = BN_bin2bn (E->g_key, 256, 0);
  ensure_ptr (g_a);
  assert (tglmp_check_g (tgl_state.encr_prime, g_a) >= 0);
  //if (!ctx) {
  //  ctx = BN_CTX_new ();
  //  ensure_ptr (ctx);
  //}
  BIGNUM *p = BN_bin2bn (tgl_state.encr_prime, 256, 0); 
  ensure_ptr (p);
  BIGNUM *r = BN_new ();
  ensure_ptr (r);
  ensure (BN_mod_exp (r, g_a, b, p, tgl_state.BN_ctx));
  static unsigned char kk[256];
  memset (kk, 0, sizeof (kk));
  BN_bn2bin (r, kk);
  for (i = 0; i < 256; i++) {
    kk[i] ^= E->nonce[i];
  }
  static unsigned char sha_buffer[20];
  sha1 (kk, 256, sha_buffer);

  bl_do_encr_chat_set_key (E, kk, *(long long *)(sha_buffer + 12));

  clear_packet ();
  out_int (CODE_messages_accept_encryption);
  out_int (CODE_input_encrypted_chat);
  out_int (tgl_get_peer_id (E->id));
  out_long (E->access_hash);
  
  ensure (BN_set_word (g_a, tgl_state.encr_root));
  ensure (BN_mod_exp (r, g_a, b, p, tgl_state.BN_ctx));
  static unsigned char buf[256];
  memset (buf, 0, sizeof (buf));
  BN_bn2bin (r, buf);
  out_cstring ((void *)buf, 256);

  out_long (E->key_fingerprint);
  BN_clear_free (b);
  BN_clear_free (g_a);
  BN_clear_free (p);
  BN_clear_free (r);

  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &send_encr_accept_methods, E, callback, callback_extra);
}

void tgl_do_create_keys_end (struct tgl_secret_chat *U) {
  assert (tgl_state.encr_prime);
  BIGNUM *g_b = BN_bin2bn (U->g_key, 256, 0);
  ensure_ptr (g_b);
  assert (tglmp_check_g (tgl_state.encr_prime, g_b) >= 0);
  BIGNUM *p = BN_bin2bn (tgl_state.encr_prime, 256, 0); 
  ensure_ptr (p);
  BIGNUM *r = BN_new ();
  ensure_ptr (r);
  BIGNUM *a = BN_bin2bn ((void *)U->key, 256, 0);
  ensure_ptr (a);
  ensure (BN_mod_exp (r, g_b, a, p, tgl_state.BN_ctx));

  unsigned char *t = talloc (256);
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
    vlogprintf (E_WARNING, "Key fingerprint mismatch (my 0x%llx 0x%llx)\n", (unsigned long long)k, (unsigned long long)U->key_fingerprint);
    U->state = sc_deleted;
  }

  tfree_secure (t, 256);
  
  BN_clear_free (p);
  BN_clear_free (g_b);
  BN_clear_free (r);
  BN_clear_free (a);
}

void tgl_do_send_create_encr_chat (void *x, unsigned char *random, void (*callback)(void *callback_extra, int success, struct tgl_secret_chat *E), void *callback_extra) {
  int user_id = (long)x;
  int i;
  unsigned char random_here[256];
  tglt_secure_random (random_here, 256);
  for (i = 0; i < 256; i++) {
    random[i] ^= random_here[i];
  }
  BIGNUM *a = BN_bin2bn (random, 256, 0);
  ensure_ptr (a);
  BIGNUM *p = BN_bin2bn (tgl_state.encr_prime, 256, 0); 
  ensure_ptr (p);
 
  BIGNUM *g = BN_new ();
  ensure_ptr (g);

  ensure (BN_set_word (g, tgl_state.encr_root));

  BIGNUM *r = BN_new ();
  ensure_ptr (r);

  ensure (BN_mod_exp (r, g, a, p, tgl_state.BN_ctx));

  BN_clear_free (a);

  static char g_a[256];
  memset (g_a, 0, 256);

  BN_bn2bin (r, (void *)g_a);
  
  int t = lrand48 ();
  while (tgl_peer_get (TGL_MK_ENCR_CHAT (t))) {
    t = lrand48 ();
  }

  bl_do_encr_chat_init (t, user_id, (void *)random, (void *)g_a);
  tgl_peer_t *_E = tgl_peer_get (TGL_MK_ENCR_CHAT (t));
  assert (_E);
  struct tgl_secret_chat *E = &_E->encr_chat;
  
  clear_packet ();
  out_int (CODE_messages_request_encryption);
  tgl_peer_t *U = tgl_peer_get (TGL_MK_USER (E->user_id));
  assert (U);
  if (U && U->user.access_hash) {
    out_int (CODE_input_user_foreign);
    out_int (E->user_id);
    out_long (U->user.access_hash);
  } else {
    out_int (CODE_input_user_contact);
    out_int (E->user_id);
  }
  out_int (tgl_get_peer_id (E->id));
  out_cstring (g_a, 256);
  //write_secret_chat_file ();
  
  BN_clear_free (g);
  BN_clear_free (p);
  BN_clear_free (r);

  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &send_encr_request_methods, E, callback, callback_extra);
}

static int get_dh_config_on_answer (struct query *q UU) {
  unsigned x = fetch_int ();
  assert (x == CODE_messages_dh_config || x == CODE_messages_dh_config_not_modified);
  if (x == CODE_messages_dh_config)  {
    int a = fetch_int ();
    int l = prefetch_strlen ();
    assert (l == 256);
    char *s = fetch_str (l);
    int v = fetch_int ();
    bl_do_set_dh_params (a, (void *)s, v);

    BIGNUM *p = BN_bin2bn ((void *)s, 256, 0);
    ensure_ptr (p);
    assert (tglmp_check_DH_params (p, a) >= 0);
    BN_free (p);      
  }
  int l = prefetch_strlen ();
  assert (l == 256);
  unsigned char *random = talloc (256);
  memcpy (random, fetch_str (256), 256);
  if (q->extra) {
    void **x = q->extra;
    ((void (*)(void *, void *, void *, void *))(*x))(x[1], random, q->callback, q->callback_extra);
    tfree (x, 2 * sizeof (void *));
    tfree_secure (random, 256);
  } else {
    tfree_secure (random, 256);
  }
  return 0;
}

static struct query_methods get_dh_config_methods  = {
  .on_answer = get_dh_config_on_answer,
  .type = TYPE_TO_PARAM(messages_dh_config)
};

void tgl_do_accept_encr_chat_request (struct tgl_secret_chat *E, void (*callback)(void *callback_extra, int success, struct tgl_secret_chat *E), void *callback_extra) {
  if (E->state != sc_request) {
    if (callback) {
      callback (callback_extra, 0, E);
    }
    return;
  }
  assert (E->state == sc_request);
  
  clear_packet ();
  out_int (CODE_messages_get_dh_config);
  out_int (tgl_state.encr_param_version);
  out_int (256);
  void **x = talloc (2 * sizeof (void *));
  x[0] = tgl_do_send_accept_encr_chat;
  x[1] = E;
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &get_dh_config_methods, x, callback, callback_extra);
}

void tgl_do_create_encr_chat_request (int user_id, void (*callback)(void *callback_extra, int success, struct tgl_secret_chat *E), void *callback_extra) {
  clear_packet ();
  out_int (CODE_messages_get_dh_config);
  out_int (tgl_state.encr_param_version);
  out_int (256);
  void **x = talloc (2 * sizeof (void *));
  x[0] = tgl_do_send_create_encr_chat;
  x[1] = (void *)(long)(user_id);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &get_dh_config_methods, x, callback, callback_extra);
}
/* }}} */

/* {{{ Get difference */
//int unread_messages;
//int difference_got;
//int seq, pts, qts, last_date;
static int get_state_on_answer (struct query *q UU) {
  assert (tgl_state.locks & TGL_LOCK_DIFF);
  tgl_state.locks ^= TGL_LOCK_DIFF;
  assert (fetch_int () == (int)CODE_updates_state);
  bl_do_set_pts (fetch_int ());
  bl_do_set_qts (fetch_int ());
  bl_do_set_date (fetch_int ());
  bl_do_set_seq (fetch_int ());
  //unread_messages = fetch_int ();
  fetch_int ();
  //write_state_file ();
  //difference_got = 1;

  if (q->callback) {
    ((void (*)(void *, int))q->callback) (q->callback_extra, 1);
  }
  return 0;
}

//int get_difference_active;
static int get_difference_on_answer (struct query *q UU) {
  //get_difference_active = 0;
  assert (tgl_state.locks & TGL_LOCK_DIFF);
  tgl_state.locks ^= TGL_LOCK_DIFF;

  unsigned x = fetch_int ();
  if (x == CODE_updates_difference_empty) {
    bl_do_set_date (fetch_int ());
    bl_do_set_seq (fetch_int ());
    //difference_got = 1;
    
    vlogprintf (E_DEBUG, "Empty difference. Seq = %d\n", tgl_state.seq);
    if (q->callback) {
      ((void (*)(void *, int))q->callback) (q->callback_extra, 1);
    }
  } else if (x == CODE_updates_difference || x == CODE_updates_difference_slice) {
    int n, i;
    assert (fetch_int () == CODE_vector);
    n = fetch_int ();
    struct tgl_message **ML = talloc (n * sizeof (void *));
    int ml_pos = 0;
    for (i = 0; i < n; i++) {
      ML[ml_pos ++] = tglf_fetch_alloc_message ();
    }
    assert (fetch_int () == CODE_vector);
    n = fetch_int ();
    struct tgl_message **EL = talloc (n * sizeof (void *));
    int el_pos = 0;
    for (i = 0; i < n; i++) {
      EL[el_pos ++] = tglf_fetch_alloc_encrypted_message ();
    }
    assert (fetch_int () == CODE_vector);
    n = fetch_int ();
    for (i = 0; i < n; i++) {
      tglu_work_update (0, 0);
    }
    assert (fetch_int () == CODE_vector);
    n = fetch_int ();
    for (i = 0; i < n; i++) {
      tglf_fetch_alloc_chat ();
    }
    assert (fetch_int () == CODE_vector);
    n = fetch_int ();
    for (i = 0; i < n; i++) {
      tglf_fetch_alloc_user ();
    }
    assert (fetch_int () == (int)CODE_updates_state);
    bl_do_set_pts (fetch_int ());
    bl_do_set_qts (fetch_int ());
    bl_do_set_date (fetch_int ());
    if (x == CODE_updates_difference) {
      bl_do_set_seq (fetch_int ());
      vlogprintf (E_DEBUG, "Difference end. New seq = %d\n", tgl_state.seq);
    } else {
      fetch_int ();
    }
    //unread_messages = fetch_int ();
    fetch_int ();
    //write_state_file ();
    /*for (i = 0; i < ml_pos; i++) {
      print_message (ML[i]);
    }*/
    for (i = 0; i < ml_pos; i++) {
      //tgl_state.callback.new_msg (ML[i]);
      bl_do_msg_update (ML[i]->id);
    }
    for (i = 0; i < el_pos; i++) {
      //tgl_state.callback.new_msg (EL[i]);
      bl_do_msg_update (EL[i]->id);
    }
    tfree (ML, ml_pos * sizeof (void *));
    tfree (EL, el_pos * sizeof (void *));

    if (x == CODE_updates_difference_slice) {
      //if (q->callback) {
      //  ((void (*)(void *, int))q->callback) (q->callback_extra, 1);
      //}
      tgl_do_get_difference (0, q->callback, q->callback_extra);
    } else {
      //difference_got = 1;
      if (q->callback) {
        ((void (*)(void *, int))q->callback) (q->callback_extra, 1);
      }
    }
  } else {
    assert (0);
  }
  return 0;   
}

static struct query_methods get_state_methods = {
  .on_answer = get_state_on_answer,
  .type = TYPE_TO_PARAM(updates_state)
};

static struct query_methods get_difference_methods = {
  .on_answer = get_difference_on_answer,
  .type = TYPE_TO_PARAM(updates_difference)
};

void tgl_do_get_difference (int sync_from_start, void (*callback)(void *callback_extra, int success), void *callback_extra) {
  //get_difference_active = 1;
  //difference_got = 0;
  if (tgl_state.locks & TGL_LOCK_DIFF) {
    if (callback) {
      callback (callback_extra, 0);
    }
    return;
  }
  tgl_state.locks |= TGL_LOCK_DIFF;
  clear_packet ();
  tgl_do_insert_header ();
  if (tgl_state.seq > 0 || sync_from_start) {
    if (tgl_state.pts == 0) { tgl_state.pts = 1; }
    //if (tgl_state.qts == 0) { tgl_state.qts = 1; }
    if (tgl_state.date == 0) { tgl_state.date = 1; }
    out_int (CODE_updates_get_difference);
    out_int (tgl_state.pts);
    out_int (tgl_state.date);
    out_int (tgl_state.qts);
    tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &get_difference_methods, 0, callback, callback_extra);
  } else {
    out_int (CODE_updates_get_state);
    tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &get_state_methods, 0, callback, callback_extra);
  }
}
/* }}} */

/* {{{ Visualize key */
/*char *colors[4] = {COLOR_GREY, COLOR_CYAN, COLOR_BLUE, COLOR_GREEN};

void tgl_do_visualize_key (tgl_peer_id_t id) {
  assert (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT);
  tgl_peer_t *P = tgl_peer_get (id);
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
}*/

void tgl_do_visualize_key (tgl_peer_id_t id, unsigned char buf[16]) {
  assert (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT);
  tgl_peer_t *P = tgl_peer_get (id);
  assert (P);
  if (P->encr_chat.state != sc_ok) {
    vlogprintf (E_WARNING, "Chat is not initialized yet\n");
    return;
  }
  unsigned char res[20];
  SHA1 ((void *)P->encr_chat.key, 256, res);
  memcpy (buf, res, 16);
}
/* }}} */

/* {{{ Get suggested */
/*int get_suggested_on_answer (struct query *q UU) {
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
    tgl_peer_t *U = (void *)tglf_fetch_alloc_user ();
    assert (tgl_get_peer_id (U->id) == l[2 * i]);
    print_user_name (U->id, U);
    printf (" phone %s: %d mutual friends\n", U->user.phone, l[2 * i + 1]);
  }
  pop_color ();
  print_end ();
  return 0;
}

struct query_methods get_suggested_methods = {
  .on_answer = get_suggested_on_answer,
  .type = TYPE_TO_PARAM(contacts_suggested)
};

void tgl_do_get_suggested (void) {
  clear_packet ();
  out_int (CODE_contacts_get_suggested);
  out_int (100);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &get_suggested_methods, 0);
}*/
/* }}} */

/* {{{ Add user to chat */

static struct query_methods add_user_to_chat_methods = {
  .on_answer = fwd_msg_on_answer,
  .type = TYPE_TO_PARAM(messages_stated_message)
};

void tgl_do_add_user_to_chat (tgl_peer_id_t chat_id, tgl_peer_id_t id, int limit, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  clear_packet ();
  out_int (CODE_messages_add_chat_user);
  out_int (tgl_get_peer_id (chat_id));
  
  assert (tgl_get_peer_type (id) == TGL_PEER_USER);
  tgl_peer_t *U = tgl_peer_get (id);
  if (U && U->user.access_hash) {
    out_int (CODE_input_user_foreign);
    out_int (tgl_get_peer_id (id));
    out_long (U->user.access_hash);
  } else {
    out_int (CODE_input_user_contact);
    out_int (tgl_get_peer_id (id));
  }
  out_int (limit);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &add_user_to_chat_methods, 0, callback, callback_extra);
}

void tgl_do_del_user_from_chat (tgl_peer_id_t chat_id, tgl_peer_id_t id, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  clear_packet ();
  out_int (CODE_messages_delete_chat_user);
  out_int (tgl_get_peer_id (chat_id));
  
  assert (tgl_get_peer_type (id) == TGL_PEER_USER);
  tgl_peer_t *U = tgl_peer_get (id);
  if (U && U->user.access_hash) {
    out_int (CODE_input_user_foreign);
    out_int (tgl_get_peer_id (id));
    out_long (U->user.access_hash);
  } else {
    out_int (CODE_input_user_contact);
    out_int (tgl_get_peer_id (id));
  }
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &add_user_to_chat_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Create secret chat */
//char *create_print_name (tgl_peer_id_t id, const char *a1, const char *a2, const char *a3, const char *a4);

void tgl_do_create_secret_chat (tgl_peer_id_t id, void (*callback)(void *callback_extra, int success, struct tgl_secret_chat *E), void *callback_extra) {
  assert (tgl_get_peer_type (id) == TGL_PEER_USER);
  tgl_peer_t *U = tgl_peer_get (id);
  if (!U) { 
    vlogprintf (E_WARNING, "Can not create chat with unknown user\n");
    return;
  }

  tgl_do_create_encr_chat_request (tgl_get_peer_id (id), callback, callback_extra); 
}
/* }}} */

/* {{{ Create group chat */
static struct query_methods create_group_chat_methods = {
  .on_answer = fwd_msg_on_answer,
  .type = TYPE_TO_PARAM(messages_stated_message)
};

void tgl_do_create_group_chat (tgl_peer_id_t id, char *chat_topic, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra) {
  assert (tgl_get_peer_type (id) == TGL_PEER_USER);
  tgl_peer_t *U = tgl_peer_get (id);
  if (!U) { 
    vlogprintf (E_WARNING, "Can not create chat with unknown user\n");
    return;
  }
  clear_packet ();
  out_int (CODE_messages_create_chat);
  out_int (CODE_vector);
  out_int (1); // Number of users, currently we support only 1 user.
  if (U && U->user.access_hash) {
    out_int (CODE_input_user_foreign);
    out_int (tgl_get_peer_id (id));
    out_long (U->user.access_hash);
  } else {
    out_int (CODE_input_user_contact);
    out_int (tgl_get_peer_id (id));
  }
  out_string (chat_topic);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &create_group_chat_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Delete msg */

static int delete_msg_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  fetch_skip (n);

  if (q->callback) {
    ((void (*)(void *, int))q->callback) (q->callback_extra, 1);
  }
  return 0;
}

static struct query_methods delete_msg_methods = {
  .on_answer = delete_msg_on_answer,
  .type = TYPE_TO_PARAM_1(vector, TYPE_TO_PARAM (bare_int))
};

void tgl_do_delete_msg (long long id, void (*callback)(void *callback_extra, int success), void *callback_extra) {
  clear_packet ();
  out_int (CODE_messages_delete_messages);
  out_int (CODE_vector);
  out_int (1);
  out_int (id);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &delete_msg_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Restore msg */

static int restore_msg_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  fetch_skip (n);
  //logprintf ("Restored %d messages\n", n);
  
  if (q->callback) {
    ((void (*)(void *, int))q->callback) (q->callback_extra, 1);
  }
  return 0;
}

static struct query_methods restore_msg_methods = {
  .on_answer = restore_msg_on_answer,
  .type = TYPE_TO_PARAM_1(vector, TYPE_TO_PARAM (bare_int))
};

void tgl_do_restore_msg (long long id, void (*callback)(void *callback_extra, int success), void *callback_extra) {
  clear_packet ();
  out_int (CODE_messages_restore_messages);
  out_int (CODE_vector);
  out_int (1);
  out_int (id);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &restore_msg_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Export card */

static int export_card_on_answer (struct query *q UU) {
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  //logprintf ("Restored %d messages\n", n);
  int *r = talloc (4 * n);
  fetch_ints (r, n);
  
  if (q->callback) {
    ((void (*)(void *, int, int, int *))q->callback) (q->callback_extra, 1, n, r);
  }
  free (r);
  return 0;
}

static struct query_methods export_card_methods = {
  .on_answer = export_card_on_answer,
  .type = TYPE_TO_PARAM_1(vector, TYPE_TO_PARAM (bare_int))
};

void tgl_do_export_card (void (*callback)(void *callback_extra, int success, int size, int *card), void *callback_extra) {
  clear_packet ();
  out_int (CODE_contacts_export_card);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &export_card_methods, 0, callback, callback_extra);
}
/* }}} */

/* {{{ Import card */

static int import_card_on_answer (struct query *q UU) {
  struct tgl_user *U = tglf_fetch_alloc_user ();
  
  if (q->callback) {
    ((void (*)(void *, int, struct tgl_user *))q->callback) (q->callback_extra, 1, U);
  }
  return 0;
}

static struct query_methods import_card_methods = {
  .on_answer = import_card_on_answer,
  .type = TYPE_TO_PARAM (user)
};

void tgl_do_import_card (int size, int *card, void (*callback)(void *callback_extra, int success, struct tgl_user *U), void *callback_extra) {
  clear_packet ();
  out_int (CODE_contacts_import_card);
  out_int (CODE_vector);
  out_int (size);
  out_ints (card, size);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &import_card_methods, 0, callback, callback_extra);
}
/* }}} */

static void set_flag_4 (void *_D, int success) {
  struct tgl_dc *D = _D;
  assert (success);
  D->flags |= 4;

  static struct timeval ptimeout;
  ptimeout.tv_sec = tgl_state.temp_key_expire_time * 0.9;
  event_add (D->ev, &ptimeout);
}

static int send_bind_temp_on_answer (struct query *q UU) {
  assert (fetch_int () == (int)CODE_bool_true);
  struct tgl_dc *D = q->extra;
  D->flags |= 2;
  tgl_do_help_get_config_dc (D, set_flag_4, D);
  vlogprintf (E_DEBUG, "Bind successful in dc %d\n", D->id);
  return 0;
}

static struct query_methods send_bind_temp_methods = {
  .on_answer = send_bind_temp_on_answer,
  .on_error = fail_on_error,
  .type = TYPE_TO_PARAM (bool)
};

void tgl_do_send_bind_temp_key (struct tgl_dc *D, long long nonce, int expires_at, void *data, int len, long long msg_id) {
  clear_packet ();
  out_int (CODE_auth_bind_temp_auth_key);
  out_long (D->auth_key_id);
  out_long (nonce);
  out_int (expires_at);
  out_cstring (data, len);
  struct query *q = tglq_send_query_ex (D, packet_ptr - packet_buffer, packet_buffer, &send_bind_temp_methods, D, 0, 0, 2);
  assert (q->msg_id == msg_id);
}

static int update_status_on_answer (struct query *q UU) {
  fetch_bool ();
  
  if (q->callback) {
    ((void (*)(void *, int))q->callback) (q->callback_extra, 1);
  }
  return 0;
}

struct query_methods update_status_methods = {
  .on_answer = update_status_on_answer,
  .type = TYPE_TO_PARAM(bool)
};

void tgl_do_update_status (int online UU, void (*callback)(void *callback_extra, int success), void *callback_extra) {
  clear_packet ();
  out_int (CODE_account_update_status);
  out_int (online ? CODE_bool_false : CODE_bool_true);
  tglq_send_query (tgl_state.DC_working, packet_ptr - packet_buffer, packet_buffer, &update_status_methods, 0, callback, callback_extra);
}
