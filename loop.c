/*
    This file is part of telegram-cli.

    Telegram-cli is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Telegram-cli is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this telegram-cli.  If not, see <http://www.gnu.org/licenses/>.

    Copyright Vitaly Valtman 2013-2015
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if USE_PYTHON
#include "python-tg.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define READLINE_CALLBACKS

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef READLINE_GNU
#include <readline/readline.h>
#include <readline/history.h>
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>

#ifdef EVENT_V2
#include <event2/event.h>
#include <event2/bufferevent.h>
#else
#include <event.h>
#include "event-old.h"
#endif

#include "interface.h"
#include "telegram.h"
#include "loop.h"
#if USE_LUA
#include "lua-tg.h"
#endif

#include <tgl/tgl.h>
#include <tgl/tgl-binlog.h>
#include <tgl/tgl-net.h>
#include <tgl/tgl-timers.h>
#include <tgl/tgl-queries.h>

#include <openssl/sha.h>

int verbosity;
extern int readline_disabled;
extern char *bot_hash;

extern int bot_mode;
int binlog_read;
extern char *default_username;
extern char *auth_token;
void set_default_username (const char *s);
extern int binlog_enabled;

extern int unknown_user_list_pos;
extern int unknown_user_list[];
int register_mode;
extern int safe_quit;
extern int sync_from_start;

extern int disable_output;
extern int reset_authorization;

extern int sfd;
extern int usfd;

void got_it (char *line, int len);
void write_state_file (void);

static char *line_buffer;
static int line_buffer_size;
static int line_buffer_pos;
static int delete_stdin_event;

extern volatile int sigterm_cnt;

extern char *start_command;
extern struct tgl_state *TLS;
extern int ipv6_enabled;

struct event *term_ev = 0;
int read_one_string;
#define MAX_ONE_STRING_LEN 511
char one_string[MAX_ONE_STRING_LEN + 1];
int one_string_len;
void (*one_string_cb)(struct tgl_state *TLS, const char *string[], void *arg);
enum tgl_value_type one_string_type;
int one_string_num;
int one_string_total_args;
char *one_string_results[10];

void *string_cb_arg;
char *one_string_prompt;
int one_string_flags;
extern int disable_link_preview;

void deactivate_readline (void);
void reactivate_readline (void);

void do_get_string (struct tgl_state *TLS);
static void one_string_read_end (void) {
  printf ("\n");
  fflush (stdout);

  read_one_string = 0;
  tfree_str (one_string_prompt);
  one_string_prompt = NULL;
  reactivate_readline ();

  one_string_results[one_string_num] = tstrdup (one_string);
  ++one_string_num;

  if (one_string_num < one_string_total_args) {
    do_get_string (TLS);
  } else {
    one_string_cb (TLS, (void *)one_string_results, string_cb_arg);
    int i;
    for (i = 0; i < one_string_total_args; i++) {
      tfree_str (one_string_results[i]);
    }
  }
}

void generate_prompt (enum tgl_value_type type, int num) {
  switch (type) {
  case tgl_phone_number:
    assert (!num);
    one_string_prompt = tstrdup ("phone number: ");
    one_string_flags = 0;
    return;
  case tgl_code:
    assert (!num);
    one_string_prompt = tstrdup ("code ('CALL' for phone code): ");
    one_string_flags = 0;
    return;
  case tgl_register_info:
    one_string_flags = 0;
    switch (num) {
    case 0:
      one_string_prompt = tstrdup ("register (Y/n): ");
      return;
    case 1:
      one_string_prompt = tstrdup ("first name: ");
      return;
    case 2:
      one_string_prompt = tstrdup ("last name: ");
      return;
    default:
      assert (0);
    }
    return;
  case tgl_new_password:
    one_string_flags = 1;
    switch (num) {
    case 0:
      one_string_prompt = tstrdup ("new password: ");
      return;
    case 1:
      one_string_prompt = tstrdup ("retype new password: ");
      return;
    default:
      assert (0);
    }
    return;
  case tgl_cur_and_new_password:
    one_string_flags = 1;
    switch (num) {
    case 0:
      one_string_prompt = tstrdup ("old password: ");
      return;
    case 1:
      one_string_prompt = tstrdup ("new password: ");
      return;
    case 2:
      one_string_prompt = tstrdup ("retype new password: ");
      return;
    default:
      assert (0);
    }
    return;
  case tgl_cur_password:
    one_string_flags = 1;
    assert (!num);
    one_string_prompt = tstrdup ("password: ");
    return;
  case tgl_bot_hash:
    one_string_flags = 0;
    assert (!num);
    one_string_prompt = tstrdup ("hash: ");
    return;
  default:
    assert (0);
  }
}

void do_get_string (struct tgl_state *TLS) {
  deactivate_readline ();
  generate_prompt (one_string_type, one_string_num);  
  printf ("%s", one_string_prompt);
  fflush (stdout);
  read_one_string = 1;
  one_string_len = 0;  
}

void do_get_values (struct tgl_state *TLS, enum tgl_value_type type, const char *prompt, int num_values,
          void (*callback)(struct tgl_state *TLS, const char *string[], void *arg), void *arg) {
  if (type == tgl_bot_hash && bot_hash) {
    assert (num_values == 1);
    one_string_results[0] = bot_hash;
    callback (TLS, (void *)one_string_results, arg);
    return;
  }
  one_string_cb = callback;
  one_string_num = 0;
  one_string_total_args = num_values;
  one_string_type = type;
  string_cb_arg = arg;
  do_get_string (TLS); 
}

static void stdin_read_callback (evutil_socket_t fd, short what, void *arg) {
  if (!readline_disabled && !read_one_string) {
    rl_callback_read_char ();
    return;
  }
  if (read_one_string) {
    char c;
    int r = read (0, &c, 1);
    if (r <= 0) {
      perror ("read");
      delete_stdin_event = 1;
      return;
    }
    if (c == '\n' || c == '\r') {
      one_string[one_string_len] = 0;
      one_string_read_end ();
      return;
    }
    if (one_string_len < MAX_ONE_STRING_LEN) {
      one_string[one_string_len ++] = c;
      if (!(one_string_flags & 1)) {
        printf ("%c", c);
        fflush (stdout);
      }
    }
    return;
  }

  if (line_buffer_pos == line_buffer_size) {
    line_buffer = realloc (line_buffer, line_buffer_size * 2 + 100);
    assert (line_buffer);
    line_buffer_size = line_buffer_size * 2 + 100;
    assert (line_buffer);
  }
  int r = read (0, line_buffer + line_buffer_pos, line_buffer_size - line_buffer_pos);
  if (r <= 0) {
    perror ("read");
    delete_stdin_event = 1;
    return;
  }
  line_buffer_pos += r;

  while (1) {
    int p = 0;
    while (p < line_buffer_pos && line_buffer[p] != '\n') { p ++; }
    if (p < line_buffer_pos) {
      line_buffer[p] = 0;
      interpreter (line_buffer);
      memmove (line_buffer, line_buffer + p + 1, line_buffer_pos - p - 1);
      line_buffer_pos -= (p + 1);
    } else {
      break;
    }
  }
}


void net_loop (void) {
  delete_stdin_event = 0;
  if (verbosity >= E_DEBUG) {
    logprintf ("Starting netloop\n");
  }
  term_ev = event_new (TLS->ev_base, 0, EV_READ | EV_PERSIST, stdin_read_callback, 0);
  event_add (term_ev, 0);
  
  int last_get_state = time (0);
  while (1) {
    event_base_loop (TLS->ev_base, EVLOOP_ONCE);

    if (term_ev && delete_stdin_event) {
      logprintf ("delete stdin\n");
      event_free (term_ev);
      term_ev = 0;
    }

    #ifdef USE_LUA
      lua_do_all ();
    #endif
    
    #ifdef USE_PYTHON
      py_do_all ();
    #endif

    if (safe_quit && !TLS->active_queries) {
      printf ("All done. Exit\n");
      do_halt (0);
      safe_quit = 0;
    }
    if (sigterm_cnt > 0) {
      do_halt (0);
    }
    if (time (0) - last_get_state > 3600) {
      tgl_do_lookup_state (TLS);
      last_get_state = time (0);
    }
    
    write_state_file ();
    update_prompt ();
    
/*    if (unknown_user_list_pos) {
      int i;
      for (i = 0; i < unknown_user_list_pos; i++) {
        tgl_do_get_user_info (TLS, TGL_MK_USER (unknown_user_list[i]), 0, 0, 0);
      }
      unknown_user_list_pos = 0;
    }   */
  }

  if (term_ev) {
    event_free (term_ev);
    term_ev = 0;
  }
  
  if (verbosity >= E_DEBUG) {
    logprintf ("End of netloop\n");
  }
}

struct tgl_dc *cur_a_dc;
int is_authorized (void) {
  return tgl_authorized_dc (TLS, cur_a_dc);
}

int all_authorized (void) {
  int i;
  for (i = 0; i <= TLS->max_dc_num; i++) if (TLS->DC_list[i]) {
    if (!tgl_authorized_dc (TLS, TLS->DC_list[i])) {
      return 0;
    }
  }
  return 1;
}

int zero[512];


int readline_active;
int new_dc_num;
int wait_dialog_list;

extern struct tgl_update_callback upd_cb;

#define DC_SERIALIZED_MAGIC 0x868aa81d
#define STATE_FILE_MAGIC 0x28949a93
#define SECRET_CHAT_FILE_MAGIC 0x37a1988a

char *get_auth_key_filename (void);
char *get_state_filename (void);
char *get_secret_chat_filename (void);

void read_state_file (void) {
  if (binlog_enabled) { return; }
  int state_file_fd = open (get_state_filename (), O_CREAT | O_RDWR, 0600);
  if (state_file_fd < 0) {
    return;
  }
  int version, magic;
  if (read (state_file_fd, &magic, 4) < 4) { close (state_file_fd); return; }
  if (magic != (int)STATE_FILE_MAGIC) { close (state_file_fd); return; }
  if (read (state_file_fd, &version, 4) < 4) { close (state_file_fd); return; }
  assert (version >= 0);
  int x[4];
  if (read (state_file_fd, x, 16) < 16) {
    close (state_file_fd); 
    return;
  }
  int pts = x[0];
  int qts = x[1];
  int seq = x[2];
  int date = x[3];
  close (state_file_fd); 
  bl_do_set_seq (TLS, seq);
  bl_do_set_pts (TLS, pts);
  bl_do_set_qts (TLS, qts);
  bl_do_set_date (TLS, date);
}


void write_state_file (void) {
  if (binlog_enabled) { return; }
  static int wseq;
  static int wpts;
  static int wqts;
  static int wdate;
  if (wseq >= TLS->seq && wpts >= TLS->pts && wqts >= TLS->qts && wdate >= TLS->date) { return; }
  wseq = TLS->seq; wpts = TLS->pts; wqts = TLS->qts; wdate = TLS->date;
  int state_file_fd = open (get_state_filename (), O_CREAT | O_RDWR, 0600);
  if (state_file_fd < 0) {
    logprintf ("Can not write state file '%s': %m\n", get_state_filename ());
    do_halt (1);
  }
  int x[6];
  x[0] = STATE_FILE_MAGIC;
  x[1] = 0;
  x[2] = wpts;
  x[3] = wqts;
  x[4] = wseq;
  x[5] = wdate;
  assert (write (state_file_fd, x, 24) == 24);
  close (state_file_fd); 
}

void write_dc (struct tgl_dc *DC, void *extra) {
  int auth_file_fd = *(int *)extra;
  if (!DC) { 
    int x = 0;
    assert (write (auth_file_fd, &x, 4) == 4);
    return;
  } else {
    int x = 1;
    assert (write (auth_file_fd, &x, 4) == 4);
  }

  assert (DC->flags & TGLDCF_LOGGED_IN);

  assert (write (auth_file_fd, &DC->options[0]->port, 4) == 4);
  int l = strlen (DC->options[0]->ip);
  assert (write (auth_file_fd, &l, 4) == 4);
  assert (write (auth_file_fd, DC->options[0]->ip, l) == l);
  assert (write (auth_file_fd, &DC->auth_key_id, 8) == 8);
  assert (write (auth_file_fd, DC->auth_key, 256) == 256);
}

void write_auth_file (void) {
  if (binlog_enabled) { return; }
  int auth_file_fd = open (get_auth_key_filename (), O_CREAT | O_RDWR, 0600);
  assert (auth_file_fd >= 0);
  int x = DC_SERIALIZED_MAGIC;
  assert (write (auth_file_fd, &x, 4) == 4);
  assert (write (auth_file_fd, &TLS->max_dc_num, 4) == 4);
  assert (write (auth_file_fd, &TLS->dc_working_num, 4) == 4);

  tgl_dc_iterator_ex (TLS, write_dc, &auth_file_fd);

  assert (write (auth_file_fd, &TLS->our_id.peer_id, 4) == 4);
  close (auth_file_fd);
}

void write_secret_chat (tgl_peer_t *Peer, void *extra) {
  struct tgl_secret_chat *P = (void *)Peer;
  if (tgl_get_peer_type (P->id) != TGL_PEER_ENCR_CHAT) { return; }
  if (P->state != sc_ok) { return; }
  int *a = extra;
  int fd = a[0];
  a[1] ++;

  int id = tgl_get_peer_id (P->id);
  assert (write (fd, &id, 4) == 4);
  //assert (write (fd, &P->flags, 4) == 4);
  int l = strlen (P->print_name);
  assert (write (fd, &l, 4) == 4);
  assert (write (fd, P->print_name, l) == l);
  assert (write (fd, &P->user_id, 4) == 4);
  assert (write (fd, &P->admin_id, 4) == 4);
  assert (write (fd, &P->date, 4) == 4);
  assert (write (fd, &P->ttl, 4) == 4);
  assert (write (fd, &P->layer, 4) == 4);
  assert (write (fd, &P->access_hash, 8) == 8);
  assert (write (fd, &P->state, 4) == 4);
  assert (write (fd, &P->key_fingerprint, 8) == 8);
  assert (write (fd, &P->key, 256) == 256);
  assert (write (fd, &P->first_key_sha, 20) == 20);
  assert (write (fd, &P->in_seq_no, 4) == 4);
  assert (write (fd, &P->last_in_seq_no, 4) == 4);
  assert (write (fd, &P->out_seq_no, 4) == 4);
}

void write_secret_chat_file (void) {
  if (binlog_enabled) { return; }
  int secret_chat_fd = open (get_secret_chat_filename (), O_CREAT | O_RDWR, 0600);
  assert (secret_chat_fd >= 0);
  int x = SECRET_CHAT_FILE_MAGIC;
  assert (write (secret_chat_fd, &x, 4) == 4);
  x = 2; 
  assert (write (secret_chat_fd, &x, 4) == 4); // version
  assert (write (secret_chat_fd, &x, 4) == 4); // num

  int y[2];
  y[0] = secret_chat_fd;
  y[1] = 0;

  tgl_peer_iterator_ex (TLS, write_secret_chat, y);

  lseek (secret_chat_fd, 8, SEEK_SET);
  assert (write (secret_chat_fd, &y[1], 4) == 4);
  close (secret_chat_fd);
}

void read_dc (int auth_file_fd, int id, unsigned ver) {
  int port = 0;
  assert (read (auth_file_fd, &port, 4) == 4);
  int l = 0;
  assert (read (auth_file_fd, &l, 4) == 4);
  assert (l >= 0 && l < 100);
  char ip[100];
  assert (read (auth_file_fd, ip, l) == l);
  ip[l] = 0;

  long long auth_key_id;
  static unsigned char auth_key[256];
  assert (read (auth_file_fd, &auth_key_id, 8) == 8);
  assert (read (auth_file_fd, auth_key, 256) == 256);

  //bl_do_add_dc (id, ip, l, port, auth_key_id, auth_key);
  bl_do_dc_option (TLS, 0, id, "DC", 2, ip, l, port);
  bl_do_set_auth_key (TLS, id, auth_key);
  bl_do_dc_signed (TLS, id);
}

void empty_auth_file (void) {
  if (TLS->test_mode) {
    bl_do_dc_option (TLS, 0, 1, "", 0, TG_SERVER_TEST_1, strlen (TG_SERVER_TEST_1), 443);
    bl_do_dc_option (TLS, 0, 2, "", 0, TG_SERVER_TEST_2, strlen (TG_SERVER_TEST_2), 443);
    bl_do_dc_option (TLS, 0, 3, "", 0, TG_SERVER_TEST_3, strlen (TG_SERVER_TEST_3), 443);
    bl_do_set_working_dc (TLS, TG_SERVER_TEST_DEFAULT);
  } else {
    bl_do_dc_option (TLS, 0, 1, "", 0, TG_SERVER_1, strlen (TG_SERVER_1), 443);
    bl_do_dc_option (TLS, 0, 2, "", 0, TG_SERVER_2, strlen (TG_SERVER_2), 443);
    bl_do_dc_option (TLS, 0, 3, "", 0, TG_SERVER_3, strlen (TG_SERVER_3), 443);
    bl_do_dc_option (TLS, 0, 4, "", 0, TG_SERVER_4, strlen (TG_SERVER_4), 443);
    bl_do_dc_option (TLS, 0, 5, "", 0, TG_SERVER_5, strlen (TG_SERVER_5), 443);
    bl_do_set_working_dc (TLS, TG_SERVER_DEFAULT);
  }
}

int need_dc_list_update;
void read_auth_file (void) {
  if (binlog_enabled) { return; }
  int auth_file_fd = open (get_auth_key_filename (), O_CREAT | O_RDWR, 0600);
  if (auth_file_fd < 0) {
    empty_auth_file ();
    return;
  }
  assert (auth_file_fd >= 0);
  unsigned x;
  unsigned m;
  if (read (auth_file_fd, &m, 4) < 4 || (m != DC_SERIALIZED_MAGIC)) {
    close (auth_file_fd);
    empty_auth_file ();
    return;
  }
  assert (read (auth_file_fd, &x, 4) == 4);
  assert (x > 0);
  int dc_working_num;
  assert (read (auth_file_fd, &dc_working_num, 4) == 4);
  
  int i;
  for (i = 0; i <= (int)x; i++) {
    int y;
    assert (read (auth_file_fd, &y, 4) == 4);
    if (y) {
      read_dc (auth_file_fd, i, m);
    }
  }
  bl_do_set_working_dc (TLS, dc_working_num);
  int our_id;
  int l = read (auth_file_fd, &our_id, 4);
  if (l < 4) {
    assert (!l);
  }
  if (our_id) {
    bl_do_set_our_id (TLS, TGL_MK_USER (our_id));
  }
  close (auth_file_fd);
}

void read_secret_chat (int fd, int v) {
  int id, l, user_id, admin_id, date, ttl, layer, state;
  long long access_hash, key_fingerprint;
  static char s[1000];
  static unsigned char key[256];
  static unsigned char sha[20];
  assert (read (fd, &id, 4) == 4);
  //assert (read (fd, &flags, 4) == 4);
  assert (read (fd, &l, 4) == 4);
  assert (l > 0 && l < 1000);
  assert (read (fd, s, l) == l);
  assert (read (fd, &user_id, 4) == 4);
  assert (read (fd, &admin_id, 4) == 4);
  assert (read (fd, &date, 4) == 4);
  assert (read (fd, &ttl, 4) == 4);
  assert (read (fd, &layer, 4) == 4);
  assert (read (fd, &access_hash, 8) == 8);
  assert (read (fd, &state, 4) == 4);
  assert (read (fd, &key_fingerprint, 8) == 8);
  assert (read (fd, &key, 256) == 256);
  assert (read (fd, sha, 20) == 20);
  int in_seq_no = 0, out_seq_no = 0, last_in_seq_no = 0;
  if (v >= 1) {
    assert (read (fd, &in_seq_no, 4) == 4);
    assert (read (fd, &last_in_seq_no, 4) == 4);
    assert (read (fd, &out_seq_no, 4) == 4);
  }

  bl_do_encr_chat (TLS, id, 
    &access_hash,
    &date,
    &admin_id,
    &user_id,
    key,
    NULL,
    sha,
    &state,
    &ttl,
    &layer,
    &in_seq_no,
    &last_in_seq_no,
    &out_seq_no,
    &key_fingerprint,
    TGLECF_CREATE | TGLECF_CREATED,
    NULL, 0
  );
    
}

void read_secret_chat_file (void) {
  if (binlog_enabled) { return; }
  int secret_chat_fd = open (get_secret_chat_filename (), O_RDWR, 0600);
  if (secret_chat_fd < 0) { return; }
  //assert (secret_chat_fd >= 0);
  int x;
  if (read (secret_chat_fd, &x, 4) < 4) { close (secret_chat_fd); return; }
  if (x != SECRET_CHAT_FILE_MAGIC) { close (secret_chat_fd); return; }
  int v = 0;
  assert (read (secret_chat_fd, &v, 4) == 4);
  assert (v == 0 || v == 1 || v == 2); // version  
  assert (read (secret_chat_fd, &x, 4) == 4);
  assert (x >= 0);
  while (x --> 0) {
    read_secret_chat (secret_chat_fd, v);
  }
  close (secret_chat_fd);
}

static void read_incoming (struct bufferevent *bev, void *_arg) {
  vlogprintf (E_WARNING, "Read from incoming connection\n");
  struct in_ev *ev = _arg;
  assert (ev->bev == bev);
  ev->in_buf_pos += bufferevent_read (bev, ev->in_buf + ev->in_buf_pos, 4096 - ev->in_buf_pos);

  while (1) {
    int pos = 0;
    int ok = 0;
    while (pos < ev->in_buf_pos) {
      if (ev->in_buf[pos] == '\n') {
        if (!ev->error) {
          ev->in_buf[pos] = 0;
          interpreter_ex (ev->in_buf, ev);
        } else {
          ev->error = 0;
        }
        ok = 1;
        ev->in_buf_pos -= (pos + 1);
        memmove (ev->in_buf, ev->in_buf + pos + 1, ev->in_buf_pos);
        pos = 0;
      } else {
        pos ++;
      }
    }
    if (ok) {
      ev->in_buf_pos += bufferevent_read (bev, ev->in_buf + ev->in_buf_pos, 4096 - ev->in_buf_pos);
    } else {
      if (ev->in_buf_pos == 4096) {
        ev->error = 1;
      }
      break;
    }
  }
}

void event_incoming (struct bufferevent *bev, short what, void *_arg) {
  struct in_ev *ev = _arg;
  if (what & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
    vlogprintf (E_WARNING, "Closing incoming connection\n");
    assert (ev->fd >= 0);
    close (ev->fd);
    bufferevent_free (bev);
    ev->bev = 0;
    if (!--ev->refcnt) { free (ev); }
  }
}

static void accept_incoming (evutil_socket_t efd, short what, void *arg) {
  vlogprintf (E_WARNING, "Accepting incoming connection\n");
  socklen_t clilen = 0;
  struct sockaddr_in cli_addr;
  int fd = accept (efd, (struct sockaddr *)&cli_addr, &clilen);

  assert (fd >= 0);
  struct bufferevent *bev = bufferevent_socket_new (TLS->ev_base, fd, 0);
  struct in_ev *e = malloc (sizeof (*e));
  e->bev = bev;
  e->refcnt = 1;
  e->in_buf_pos = 0;
  e->error = 0;
  e->fd = fd;
  bufferevent_setcb (bev, read_incoming, 0, event_incoming, e);
  bufferevent_enable (bev, EV_READ | EV_WRITE);
}

char *get_downloads_directory (void);
void on_login (struct tgl_state *TLS) {
  write_auth_file ();
}

void on_failed_login (struct tgl_state *TLS) {
  logprintf ("login failed\n");
  logprintf ("login error #%d: %s\n", TLS->error_code, TLS->error);
  logprintf ("you can relogin by deleting auth file or running telegram-cli with '-q' flag\n");
  exit (2);
}

void on_started (struct tgl_state *TLS);
void clist_cb (struct tgl_state *TLSR, void *callback_extra, int success, int size, tgl_peer_id_t peers[], tgl_message_id_t *last_msg_id[], int unread_count[]) {
  on_started (TLS);
}

void dlist_cb (struct tgl_state *TLSR, void *callback_extra, int success, int size, tgl_peer_id_t peers[], tgl_message_id_t *last_msg_id[], int unread_count[])  {
  tgl_do_get_channels_dialog_list (TLS, 100, 0, clist_cb, 0);
}

void on_started (struct tgl_state *TLS) {
  if (wait_dialog_list) {
    wait_dialog_list = 0;
    tgl_do_get_dialog_list (TLS, 100, 0, dlist_cb, 0);
    return;
  }
  #ifdef USE_LUA
    lua_diff_end ();
  #endif

  #ifdef USE_PYTHON
    py_diff_end ();
  #endif
  
  if (start_command) {
    safe_quit = 1;
    while (*start_command) {
      char *start = start_command;
      while (*start_command && *start_command != '\n') {
        start_command ++;
      }
      if (*start_command) {
        *start_command = 0;
        start_command ++;
      } 
      interpreter_ex (start, 0);
    }
  }
}

int loop (void) {
  tgl_set_callback (TLS, &upd_cb);
  struct event_base *ev = event_base_new ();
  tgl_set_ev_base (TLS, ev);
  tgl_set_net_methods (TLS, &tgl_conn_methods);
  tgl_set_timer_methods (TLS, &tgl_libevent_timers);
  assert (TLS->timer_methods);
  tgl_set_download_directory (TLS, get_downloads_directory ());
  tgl_register_app_id (TLS, TELEGRAM_CLI_APP_ID, TELEGRAM_CLI_APP_HASH); 
  tgl_set_app_version (TLS, "Telegram-cli " TELEGRAM_CLI_VERSION);
  if (ipv6_enabled) {
    tgl_enable_ipv6 (TLS);
  }
  if (bot_mode) {
    tgl_enable_bot (TLS);
  }
  if (disable_link_preview) {
    tgl_disable_link_preview (TLS);
  }
  assert (tgl_init (TLS) >= 0);
 
  /*if (binlog_enabled) {
    double t = tglt_get_double_time ();
    if (verbosity >= E_DEBUG) {
      logprintf ("replay log start\n");
    }
    tgl_replay_log (TLS);
    if (verbosity >= E_DEBUG) {
      logprintf ("replay log end in %lf seconds\n", tglt_get_double_time () - t);
    }
    tgl_reopen_binlog_for_writing (TLS);
  } else {*/
    read_auth_file ();
    read_state_file ();
    read_secret_chat_file ();
  //}

  binlog_read = 1;
  #ifdef USE_LUA
    lua_binlog_end ();
  #endif
  
  #ifdef USE_PYTHON
    py_binlog_end ();
  #endif
  
  if (sfd >= 0) {
    struct event *ev = event_new (TLS->ev_base, sfd, EV_READ | EV_PERSIST, accept_incoming, 0);
    event_add (ev, 0);
  }
  if (usfd >= 0) {
    struct event *ev = event_new (TLS->ev_base, usfd, EV_READ | EV_PERSIST, accept_incoming, 0);
    event_add (ev, 0);
  }
  update_prompt ();
   
  if (reset_authorization) {
    tgl_peer_t *P = tgl_peer_get (TLS, TLS->our_id);
    if (P && P->user.phone && reset_authorization == 1) {
      set_default_username (P->user.phone);
    }
    bl_do_reset_authorization (TLS);
  }

  set_interface_callbacks ();
  tgl_login (TLS);
  net_loop ();
  return 0;
}

