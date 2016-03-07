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

#ifdef USE_PYTHON
#  include "python-tg.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef READLINE_GNU
#include <readline/readline.h>
#include <readline/history.h>
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif
#include <unistd.h>

//#include "queries.h"

#include "interface.h"
#include "telegram.h"

#ifdef EVENT_V2
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#else
#include <event.h>
#include "event-old.h"
#endif
//#include "auto/constants.h"
//#include "tools.h"
//#include "structures.h"

#ifdef USE_LUA
#  include "lua-tg.h"
#endif


//#include "mtproto-common.h"

#include <tgl/tgl.h>
#include <tgl/tgl-queries.h>
#include "loop.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef __APPLE__
#define OPEN_BIN "open %s"
#else
#define OPEN_BIN "xdg-open %s"
#endif

#ifdef USE_JSON
#  include <jansson.h>
#  include "json-tg.h"
#endif

#include "tgl/mtproto-common.h"
#include "auto/auto-store.h"
#include "auto/auto-fetch-ds.h"
#include "auto/auto-types.h"
#include "auto/auto-free-ds.h"

#include <errno.h>

#include "tgl/tree.h"

struct username_peer_pair {
  const char *username;
  tgl_peer_t *peer;
};

#define username_peer_pair_cmp(a,b) strcmp (a->username, b->username)
DEFINE_TREE (username_peer_pair, struct username_peer_pair *, username_peer_pair_cmp, NULL)
struct tree_username_peer_pair *username_peer_pair;

struct username_peer_pair *current_map;

#define ALLOW_MULT 1
char *default_prompt = "> ";

extern int read_one_string;
extern char one_string[];
extern int one_string_len;
extern char *one_string_prompt;
extern int one_string_flags;
extern int enable_json;
int disable_auto_accept;
int msg_num_mode;
int permanent_msg_id_mode;
int permanent_peer_id_mode;
int disable_colors;
extern int alert_sound;
extern int binlog_read;
extern char *home_directory;
int do_html;

int safe_quit;

int in_readline;
int readline_active;

int log_level;

char *line_ptr;

int in_chat_mode;
tgl_peer_id_t chat_mode_id;
extern int readline_disabled;

extern int disable_output;

struct in_ev *notify_ev;

extern int usfd;
extern int sfd;
extern int use_ids;

extern int daemonize;

extern struct tgl_state *TLS;
int readline_deactivated;

void fail_interface (struct tgl_state *TLS, struct in_ev *ev, int error_code, const char *format, ...) __attribute__ (( format (printf, 4, 5)));
void event_incoming (struct bufferevent *bev, short what, void *_arg);

int is_same_word (const char *s, size_t l, const char *word) {
  return s && word && strlen (word) == l && !memcmp (s, word, l);
}

static void skip_wspc (void) {
  while (*line_ptr && ((unsigned char)*line_ptr) <= ' ') {
    line_ptr ++;
  }
}

static char *cur_token;
static int cur_token_len;
static int cur_token_end_str;
static int cur_token_quoted;

#define SOCKET_ANSWER_MAX_SIZE (1 << 25)
static char socket_answer[SOCKET_ANSWER_MAX_SIZE + 1];
static int socket_answer_pos = -1;

void socket_answer_start (void) {
  socket_answer_pos = 0;
}

static void socket_answer_add_printf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));
void socket_answer_add_printf (const char *format, ...) {
  if (socket_answer_pos < 0) { return; }
  va_list ap;
  va_start (ap, format);
  socket_answer_pos += vsnprintf (socket_answer + socket_answer_pos, SOCKET_ANSWER_MAX_SIZE - socket_answer_pos, format, ap);
  va_end (ap);
  if (socket_answer_pos > SOCKET_ANSWER_MAX_SIZE) { socket_answer_pos = -1; }
}

void socket_answer_end (struct in_ev *ev) {
  if (ev->bev) {
    static char s[100];
    sprintf (s, "ANSWER %d\n", socket_answer_pos);
    bufferevent_write (ev->bev, s, strlen (s));
    bufferevent_write (ev->bev, socket_answer, socket_answer_pos);
    bufferevent_write (ev->bev, "\n", 1);
  }
  socket_answer_pos = -1;
}

#define mprintf(ev,...) \
  if (ev) { socket_answer_add_printf (__VA_ARGS__); } \
  else { printf (__VA_ARGS__); } 

#define mprint_start(ev,...) \
  if (!ev) { print_start (__VA_ARGS__); } \
  else { socket_answer_start (); }
  
#define mprint_end(ev,...) \
  if (!ev) { print_end (__VA_ARGS__); } \
  else { socket_answer_end (ev); }

#define mpush_color(ev,...) \
  if (!ev) { push_color (__VA_ARGS__); }

#define mpop_color(ev,...) \
  if (!ev) { pop_color (__VA_ARGS__); }

static void unescape_token (char *start, char *end) {
  static char cur_token_buff[(1 << 20) + 1];
  cur_token_len = 0;
  cur_token = cur_token_buff;
  while (start < end) {
    assert (cur_token_len < (1 << 20));
    switch (*start) {
    case '\\':
      start ++;
      switch (*start) {
      case 'n':
        cur_token[cur_token_len ++] = '\n';
        break;
      case 'r':
        cur_token[cur_token_len ++] = '\r';
        break;
      case 't':
        cur_token[cur_token_len ++] = '\t';
        break;
      case 'b':
        cur_token[cur_token_len ++] = '\b';
        break;
      case 'a':
        cur_token[cur_token_len ++] = '\a';
        break;
      default:
        cur_token[cur_token_len ++] = *start;
        break;
      }
      break;
    default:
      cur_token[cur_token_len ++] = *start;;
      break;
    }
    start ++;
  }
  cur_token[cur_token_len] = 0;
}

int force_end_mode;
static void next_token (void) {
  skip_wspc ();
  cur_token_end_str = 0;
  cur_token_quoted = 0;
  if (!*line_ptr) {
    cur_token_len = 0;
    cur_token_end_str = 1;
    return;
  }
  char c = *line_ptr;
  char *start = line_ptr;
  if (c == '"' || c == '\'') {
    cur_token_quoted = 1;
    line_ptr ++;
    int esc = 0;
    while (*line_ptr && (esc || *line_ptr != c)) {
      if (*line_ptr == '\\') {
        esc = 1 - esc;
      } else {
        esc = 0;
      }
      line_ptr ++;
    }
    if (!*line_ptr) {
      cur_token_len = -2;
    } else {
      unescape_token (start + 1, line_ptr);
      line_ptr ++;
    }
  } else {
    while (*line_ptr && ((unsigned char)*line_ptr) > ' ') {
      line_ptr ++;
    }
    cur_token = start;
    cur_token_len = line_ptr - start;
    cur_token_end_str = (!force_end_mode) && (*line_ptr == 0);
  }
}

void next_token_end (void) {
  skip_wspc ();
  
  if (*line_ptr && *line_ptr != '"' && *line_ptr != '\'') {
    cur_token_quoted = 0;
    cur_token = line_ptr;
    while (*line_ptr) { line_ptr ++; }
    cur_token_len = line_ptr - cur_token;
    while (((unsigned char)cur_token[cur_token_len - 1]) <= ' ' && cur_token_len >= 0) { 
      cur_token_len --;
    }
    assert (cur_token_len > 0);
    cur_token_end_str = !force_end_mode;
    return;
  } else {
    if (*line_ptr) {
      next_token ();
      skip_wspc ();
      if (*line_ptr) {
        cur_token_len = -1; 
      }
    } else {
      next_token ();
    }
  }
}

void next_token_end_ac (void) {
  skip_wspc ();
  
  if (*line_ptr && *line_ptr != '"' && *line_ptr != '\'') {
    cur_token_quoted = 0;
    cur_token = line_ptr;
    while (*line_ptr) { line_ptr ++; }
    cur_token_len = line_ptr - cur_token;
    assert (cur_token_len > 0);
    cur_token_end_str = !force_end_mode;
    return;
  } else {
    if (*line_ptr) {
      next_token ();
      skip_wspc ();
      if (*line_ptr) {
        cur_token_len = -1; 
      }
    } else {
      next_token ();
    }
  }
}

#define NOT_FOUND (int)0x80000000
tgl_peer_id_t TGL_PEER_NOT_FOUND = {.peer_id = NOT_FOUND};

long long cur_token_int (void) {
  if (cur_token_len <= 0) {
    return NOT_FOUND;
  } else {
    char c = cur_token[cur_token_len];
    cur_token[cur_token_len] = 0;
    char *end = 0;
    long long x = strtoll (cur_token, &end, 0);
    cur_token[cur_token_len] = c;
    if (end != cur_token + cur_token_len) {
      return NOT_FOUND;
    } else {
      return x;
    }
  }
}

int hex2int (char c) {
  if (c >= '0' && c <= '9') { return c - '0'; }
  if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
  assert (0);
  return 0;
}

char *print_permanent_msg_id (tgl_message_id_t id) {
  static char buf[2 * sizeof (tgl_message_id_t) + 1];
  
  unsigned char *s = (void *)&id;
  int i;
  for (i = 0; i < (int)sizeof (tgl_message_id_t); i++) {
    sprintf (buf + 2 * i, "%02x", (unsigned)s[i]);
  }
  return buf;
}

char *print_permanent_peer_id (tgl_peer_id_t id) {
  static char buf[2 * sizeof (tgl_peer_id_t) + 2];
  buf[0] = '$';
  
  unsigned char *s = (void *)&id;
  int i;
  for (i = 0; i < (int)sizeof (tgl_peer_id_t); i++) {
    sprintf (buf + 1 + 2 * i, "%02x", (unsigned)s[i]);
  }
  return buf;
}

tgl_message_id_t parse_input_msg_id (const char *s, int l) {
  if (!s || l <= 0) {
    tgl_message_id_t id;
    memset (&id, 0, sizeof (id));
    id.peer_type = 0;
    return id;
  } else {    
    tgl_message_id_t id;
    memset (&id, 0, sizeof (id));

    if (l == 2 * sizeof (tgl_message_id_t)) {
      int i;
      for (i = 0; i < (int)sizeof (tgl_message_id_t); i++) {
        if (
          (s[i] < '0' || s[i] > '9') &&
          (s[i] < 'a' || s[i] > 'f')
        ) { 
          id.peer_type = 0;
          return id;
        }
      }
      unsigned char *d = (void *)&id;
      for (i = 0; i < (int)sizeof (tgl_message_id_t); i++) {
        d[i] = hex2int (s[2 * i]) * 16 + hex2int (s[2 * i + 1]);
      }     
      return id;
    } else {
      char *sc = tstrndup (s, l);
      char *end = 0;
      long long x = strtoll (sc, &end, 0);
      tfree_str (sc);
      if (end != sc + l) {
        id.peer_type = 0;  
      } else {
        id.peer_type = TGL_PEER_TEMP_ID;
        id.id = x;
      }
      return id;
    }
  }
}

tgl_message_id_t cur_token_msg_id (void) {
  return parse_input_msg_id (cur_token, cur_token_len);
}

double cur_token_double (void) {
  if (cur_token_len <= 0) {
    return NOT_FOUND;
  } else {
    char c = cur_token[cur_token_len];
    cur_token[cur_token_len] = 0;
    char *end = 0;
    double x = strtod (cur_token, &end);
    cur_token[cur_token_len] = c;
    if (end != cur_token + cur_token_len) {
      return NOT_FOUND;
    } else {
      return x;
    }
  }
}

tgl_peer_id_t parse_input_peer_id (const char *s, int l, int mask) {
  if (!s || l <= 0) { return TGL_PEER_NOT_FOUND; }

  if (*s == '$') {
    s ++;
    l --;
    if (l != 2 * sizeof (tgl_peer_id_t)) {
      return TGL_PEER_NOT_FOUND;
    }

    tgl_peer_id_t res;
    unsigned char *r = (void *)&res;
    int i;
    for (i = 0; i < l; i++) {
      if ((s[i] < '0' || s[i] > '9') && 
          (s[i] < 'a' || s[i] > 'f')) {
        return TGL_PEER_NOT_FOUND;
      }
    }
    for (i = 0; i < (int)sizeof (tgl_peer_id_t); i++) {
      r[i] = hex2int (s[2 * i]) * 16 + hex2int (s[2 * i + 1]);
    }

    if (mask && tgl_get_peer_type (res) != mask) {
      return TGL_PEER_NOT_FOUND;
    }

    return res;
  }

  if (*s == '@') {
    s ++;
    l --;
    char *tmp = tstrndup (s, l);
    struct username_peer_pair *p = tree_lookup_username_peer_pair (username_peer_pair, (void *)&tmp);
    tfree_str (tmp);
    if (p && (!mask || tgl_get_peer_type (p->peer->id) == mask)) {
      return p->peer->id;
    } else {
      return TGL_PEER_NOT_FOUND;
    }
  }

  const char *ss[] = {"user#id", "user#", "chat#id", "chat#", "secret_chat#id", "secret_chat#", "channel#id", "channel#"};
  int tt[] = {TGL_PEER_USER, TGL_PEER_USER, TGL_PEER_CHAT, TGL_PEER_CHAT, TGL_PEER_ENCR_CHAT, TGL_PEER_ENCR_CHAT, TGL_PEER_CHANNEL, TGL_PEER_CHANNEL};

  char *sc = tstrndup (s, l);

  int i;
  for (i = 0; i < 8; i++) if (!mask || mask == tt[i]) {
    int x = strlen (ss[i]);
    if (l > x && !memcmp (s, ss[i], x)) {
      int r = atoi (sc + x);
      tfree_str (sc);
      if (r < 0) { return TGL_PEER_NOT_FOUND; }
      tgl_peer_t *P = tgl_peer_get (TLS, tgl_set_peer_id (tt[i], r));
      if (!P) { return TGL_PEER_NOT_FOUND; }
      return P->id;
    }
  }

  tgl_peer_t *P = tgl_peer_get_by_name (TLS, sc); 
  tfree_str (sc);
  
  if (P && (!mask || tgl_get_peer_type (P->id) == mask)) {
    return P->id;
  } else {
    return TGL_PEER_NOT_FOUND;
  }
}

tgl_peer_id_t cur_token_user (void) {
  return parse_input_peer_id (cur_token, cur_token_len, TGL_PEER_USER);
}

tgl_peer_id_t cur_token_chat (void) {
  return parse_input_peer_id (cur_token, cur_token_len, TGL_PEER_CHAT);
}

tgl_peer_id_t cur_token_encr_chat (void) {
  return parse_input_peer_id (cur_token, cur_token_len, TGL_PEER_ENCR_CHAT);
}

tgl_peer_id_t cur_token_channel (void) {
  return parse_input_peer_id (cur_token, cur_token_len, TGL_PEER_CHANNEL);
}

tgl_peer_id_t cur_token_peer (void) {
  return parse_input_peer_id (cur_token, cur_token_len, 0);
}
/*
static tgl_peer_t *mk_peer (tgl_peer_id_t id) {
  if (tgl_get_peer_type (id) == NOT_FOUND) { return 0; }
  tgl_peer_t *P = tgl_peer_get (TLS, id);
  if (!P) {
    if (tgl_get_peer_type (id) == TGL_PEER_USER) {
      tgl_insert_empty_user (TLS, tgl_get_peer_id (id));
    }
    if (tgl_get_peer_type (id) == TGL_PEER_CHAT) {
      tgl_insert_empty_chat (TLS, tgl_get_peer_id (id));
    }
    P = tgl_peer_get (TLS, id);
  }
  return P;
}*/

char *get_default_prompt (void) {
  static char buf[1000];
  int l = 0;
  if (in_chat_mode) {
    tgl_peer_t *U = tgl_peer_get (TLS, chat_mode_id);
    assert (U && U->print_name);
    l += snprintf (buf + l, 999 - l, COLOR_RED "%.*s " COLOR_NORMAL, 100, U->print_name);
  }
  if (TLS->unread_messages || TLS->cur_uploading_bytes || TLS->cur_downloading_bytes) {
    l += snprintf (buf + l, 999 - l, COLOR_RED "[");
    int ok = 0;
    if (TLS->unread_messages) {
      l += snprintf (buf + l, 999 - l, "%d unread", TLS->unread_messages);
      ok = 1;
    }
    if (TLS->cur_uploading_bytes) {
      if (ok) { *(buf + l) = ' '; l ++; }
      ok = 1;
      l += snprintf (buf + l, 999 - l, "%lld%%Up", 100 * TLS->cur_uploaded_bytes / TLS->cur_uploading_bytes);
    }
    if (TLS->cur_downloading_bytes) {
      if (ok) { *(buf + l) = ' '; l ++; }
      ok = 1;
      l += snprintf (buf + l, 999 - l, "%lld%%Down", 100 * TLS->cur_downloaded_bytes / TLS->cur_downloading_bytes);
    }
    l += snprintf (buf + l, 999 - l, "]" COLOR_NORMAL);
    l += snprintf (buf + l, 999 - l, "%s", default_prompt);
    return buf;
  } 
  l += snprintf (buf + l, 999 - l, "%s", default_prompt);
  return buf;
}

char *complete_none (const char *text, int state) {
  return 0;
}


void set_prompt (const char *s) {
  if (readline_disabled) { return; }
  rl_set_prompt (s);
}

void update_prompt (void) {
  if (readline_disabled) {
    fflush (stdout);
    return;
  }
  if (read_one_string) { return; }
  print_start ();
  set_prompt (get_default_prompt ());
  if (readline_active) {
    rl_redisplay ();
  }
  print_end ();
}

char *modifiers[] = {
  "[offline]",
  "[enable_preview]",
  "[disable_preview]",
  "[html]",
  "[reply=",
  0
};

char *in_chat_commands[] = {
  "/exit",
  "/quit",
  "/history",
  "/read",
  0
};

enum command_argument {
  ca_none,
  ca_user,
  ca_chat,
  ca_secret_chat,
  ca_channel,
  ca_peer,
  ca_file_name,
  ca_file_name_end,
  ca_period,
  ca_number,
  ca_double,
  ca_string_end,
  ca_msg_string_end,
  ca_string,
  ca_modifier,
  ca_command,
  ca_extf,
  ca_msg_id,


  ca_optional = 256
};

struct arg {
  int flags;
  union {
    //tgl_peer_t *P;
    //struct tgl_message *M;
    char *str;
    long long num;
    double dval;
    tgl_message_id_t msg_id;
    tgl_peer_id_t peer_id;
  };
};

struct command {
  char *name;
  enum command_argument args[10];
  void (*fun)(struct command *command, int arg_num, struct arg args[], struct in_ev *ev);
  char *desc;
  void *arg;
};


int offline_mode;
int reply_id;
int disable_msg_preview;

void print_user_list_gw (struct tgl_state *TLS, void *extra, int success, int num, struct tgl_user *UL[]);
void print_msg_list_gw (struct tgl_state *TLS, void *extra, int success, int num, struct tgl_message *ML[]);
void print_msg_list_history_gw (struct tgl_state *TLS, void *extra, int success, int num, struct tgl_message *ML[]);
void print_msg_list_success_gw (struct tgl_state *TLS, void *extra, int success, int num, struct tgl_message *ML[]);
void print_dialog_list_gw (struct tgl_state *TLS, void *extra, int success, int size, tgl_peer_id_t peers[], tgl_message_id_t *last_msg_id[], int unread_count[]);
void print_chat_info_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_chat *C);
void print_channel_info_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_channel *C);
void print_user_info_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_user *C);
void print_filename_gw (struct tgl_state *TLS, void *extra, int success, const char *name);
void print_string_gw (struct tgl_state *TLS, void *extra, int success, const char *name);
void open_filename_gw (struct tgl_state *TLS, void *extra, int success, const char *name);
void print_secret_chat_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_secret_chat *E);
void print_card_gw (struct tgl_state *TLS, void *extra, int success, int size, int *card);
void print_user_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_user *U);
void print_peer_gw (struct tgl_state *TLS, void *extra, int success, tgl_peer_t *U);
void print_msg_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_message *M);
void print_msg_success_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_message *M);
void print_encr_chat_success_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_secret_chat *E);;
void print_success_gw (struct tgl_state *TLS, void *extra, int success);

struct command commands[];

/* {{{ client methods */
void do_help (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { mprint_start (ev); }
  int total = 0;
  mpush_color (ev, COLOR_YELLOW);
  struct command *cmd = commands;
  while (cmd->name) {
    if (!args[0].str || !strcmp (args[0].str, cmd->name)) {
      mprintf (ev, "%s\n", cmd->desc);
      total ++;
    }
    cmd ++;
  }
  if (!total) {
    assert (arg_num == 1);
    mprintf (ev, "Unknown command '%s'\n", args[0].str);
  }
  mpop_color (ev);
  if (ev) { mprint_end (ev); }
  if (!ev) {
    fflush (stdout);
  }
}

void do_get_terms_of_service (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  if (ev) { ev->refcnt ++; }
  tgl_do_get_terms_of_service (TLS, print_string_gw, ev);
}

void do_stats (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  static char stat_buf[1 << 15];
  tgl_print_stat (TLS, stat_buf, (1 << 15) - 1);
  if (ev) { mprint_start (ev); }
  mprintf (ev, "%s\n", stat_buf);
  if (ev) { mprint_end (ev); }
  if (!ev) {
    fflush (stdout);
  }
}

void do_show_license (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  static char *b = 
#include "LICENSE.h"
  ;
  if (ev) { mprint_start (ev); }
  mprintf (ev, "%s", b);
  if (ev) { mprint_end (ev); }
}

void do_quit (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  if (daemonize) {
    event_incoming (ev->bev, BEV_EVENT_EOF, ev);
  }
  do_halt (0);
}

void do_safe_quit (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  if (daemonize) {
    event_incoming (ev->bev, BEV_EVENT_EOF, ev);
  }
  safe_quit = 1;
}

void do_set (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  int num = args[1].num;
  if (!strcmp (args[0].str, "debug_verbosity")) {
    tgl_set_verbosity (TLS, num); 
  } else if (!strcmp (args[0].str, "log_level")) {
    log_level = num;
  } else if (!strcmp (args[0].str, "msg_num")) {
    msg_num_mode = num;
  } else if (!strcmp (args[0].str, "alert")) {
    alert_sound = num;
  }
}

void do_chat_with_peer (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  if (!ev) {
    in_chat_mode = 1;
    chat_mode_id = args[0].peer_id;
  }
}

void do_main_session (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  if (notify_ev && !--notify_ev->refcnt) {
    free (notify_ev);
  }
  notify_ev = ev;
  if (ev) { ev->refcnt ++; }
}

void do_version (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  if (ev) { mprint_start (ev); }
  mpush_color (ev, COLOR_YELLOW);
  mprintf (ev, "Telegram-cli version %s (uses tgl version %s)\n", TELEGRAM_CLI_VERSION, TGL_VERSION);
  #ifdef TGL_AVOID_OPENSSL 
    mprintf (ev, "uses libgcrypt for encryption\n");
  #else
    mprintf (ev, "uses libopenssl for encryption\n");
  #endif
  mpop_color (ev);
  if (ev) { mprint_end (ev); }
  if (!ev) {
    fflush (stdout);
  }

}
/* }}} */

#define ARG2STR_DEF(n,def) args[n].str ? args[n].str : def, args[n].str ? strlen (args[n].str) : strlen (def)
#define ARG2STR(n) args[n].str, args[n].str ? strlen (args[n].str) : 0

/* {{{ WORK WITH ACCOUNT */

void do_set_password (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_set_password (TLS, ARG2STR_DEF(0, "empty"), print_success_gw, ev);
}
/* }}} */

/* {{{ SENDING MESSAGES */

void do_msg (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  vlogprintf (E_DEBUG, "reply_id=%d, disable=%d\n", reply_id, disable_msg_preview);
  tgl_do_send_message (TLS, args[0].peer_id, ARG2STR(1), TGL_SEND_MSG_FLAG_REPLY(reply_id) | disable_msg_preview | do_html, NULL, print_msg_success_gw, ev);
}

void do_post (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  vlogprintf (E_DEBUG, "reply_id=%d, disable=%d\n", reply_id, disable_msg_preview);
  tgl_do_send_message (TLS, args[0].peer_id, ARG2STR(1), TGL_SEND_MSG_FLAG_REPLY(reply_id) | disable_msg_preview | TGLMF_POST_AS_CHANNEL | do_html, NULL, print_msg_success_gw, ev);
}

void do_msg_kbd (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }

  clear_packet ();  
  if (tglf_store_type (TLS, ARG2STR(1), TYPE_TO_PARAM (reply_markup)) < 0) {
    fail_interface (TLS, ev, ENOSYS, "can not parse reply markup");    
    return;
  }
  in_ptr = packet_buffer;
  in_end = packet_ptr;

  struct tl_ds_reply_markup *DS_RM = fetch_ds_type_reply_markup (TYPE_TO_PARAM (reply_markup));
  assert (DS_RM);

  tgl_do_send_message (TLS, args[0].peer_id, ARG2STR(2), TGL_SEND_MSG_FLAG_REPLY(reply_id) | disable_msg_preview | do_html, DS_RM, print_msg_success_gw, ev);

  free_ds_type_reply_markup (DS_RM, TYPE_TO_PARAM (reply_markup));
}

void do_reply (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_reply_message (TLS, &args[0].msg_id, ARG2STR(1), disable_msg_preview | do_html, print_msg_success_gw, ev);
}

void do_send_text (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_text (TLS, args[0].peer_id, args[1].str, TGL_SEND_MSG_FLAG_REPLY(reply_id) | disable_msg_preview | do_html, print_msg_success_gw, ev);
}
 
void do_post_text (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_text (TLS, args[0].peer_id, args[1].str, TGL_SEND_MSG_FLAG_REPLY(reply_id) | disable_msg_preview | TGLMF_POST_AS_CHANNEL | do_html, print_msg_success_gw, ev);
}
void do_reply_text (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_reply_text (TLS, &args[0].msg_id, args[1].str, disable_msg_preview | do_html, print_msg_success_gw, ev);
}

static void _do_send_file (struct command *command, int arg_num, struct arg args[], struct in_ev *ev, unsigned long long flags) {
  assert (arg_num >= 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_document (TLS, args[0].peer_id, args[1].str, arg_num == 2 ? NULL : args[2].str, arg_num == 2 ? 0 : strlen (args[2].str), flags | TGL_SEND_MSG_FLAG_REPLY (reply_id), print_msg_success_gw, ev);
}


void do_send_photo (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_send_file (command, arg_num, args, ev, TGL_SEND_MSG_FLAG_DOCUMENT_PHOTO);
}

void do_send_file (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_send_file (command, arg_num, args, ev, TGL_SEND_MSG_FLAG_DOCUMENT_AUTO);
}

void do_send_audio (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_send_file (command, arg_num, args, ev, TGL_SEND_MSG_FLAG_DOCUMENT_AUDIO);
}

void do_send_video (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_send_file (command, arg_num, args, ev, TGL_SEND_MSG_FLAG_DOCUMENT_VIDEO);
}

void do_send_document (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_send_file (command, arg_num, args, ev, 0);
}

void do_post_photo (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_send_file (command, arg_num, args, ev, TGL_SEND_MSG_FLAG_DOCUMENT_PHOTO | TGLMF_POST_AS_CHANNEL);
}

void do_post_file (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_send_file (command, arg_num, args, ev, TGL_SEND_MSG_FLAG_DOCUMENT_AUTO | TGLMF_POST_AS_CHANNEL);
}

void do_post_audio (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_send_file (command, arg_num, args, ev, TGL_SEND_MSG_FLAG_DOCUMENT_AUDIO | TGLMF_POST_AS_CHANNEL);
}

void do_post_video (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_send_file (command, arg_num, args, ev, TGL_SEND_MSG_FLAG_DOCUMENT_VIDEO | TGLMF_POST_AS_CHANNEL);
}

void do_post_document (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_send_file (command, arg_num, args, ev, TGLMF_POST_AS_CHANNEL);
}

void _do_reply_file (struct command *command, int arg_num, struct arg args[], struct in_ev *ev, unsigned long long flags) {
  assert (arg_num >= 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_reply_document (TLS, &args[0].msg_id, args[1].str, arg_num == 2 ? NULL : args[2].str, arg_num == 2 ? 0 : strlen (args[2].str), flags, print_msg_success_gw, ev);
}

void do_reply_photo (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_reply_file (command, arg_num, args, ev, TGL_SEND_MSG_FLAG_DOCUMENT_PHOTO);
}

void do_reply_file (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_reply_file (command, arg_num, args, ev, TGL_SEND_MSG_FLAG_DOCUMENT_AUTO);
}

void do_reply_audio (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_reply_file (command, arg_num, args, ev, TGL_SEND_MSG_FLAG_DOCUMENT_AUDIO);
}

void do_reply_video (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_reply_file (command, arg_num, args, ev, TGL_SEND_MSG_FLAG_DOCUMENT_VIDEO);
}

void do_reply_document (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  _do_reply_file (command, arg_num, args, ev, 0);
}

void do_fwd (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num >= 2);
  if (ev) { ev->refcnt ++; }
  assert (arg_num <= 1000);
  //if (arg_num == 2) {
  //  tgl_do_forward_message (TLS, args[0].P->id, &args[1].msg_id, 0, print_msg_success_gw, ev);
  //} else {
    static tgl_message_id_t *list[1000];
    int i;
    for (i = 0; i < arg_num - 1; i++) {
      list[i] = &args[i + 1].msg_id;
    }
    tgl_do_forward_messages (TLS, args[0].peer_id, arg_num - 1, (void *)list, 0, print_msg_list_success_gw, ev);
  //}
}

void do_fwd_media (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_forward_media (TLS, args[0].peer_id, &args[1].msg_id, 0, print_msg_success_gw, ev);
}

void do_send_contact (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 4);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_contact (TLS, args[0].peer_id, ARG2STR (1), ARG2STR (2), ARG2STR (3), TGL_SEND_MSG_FLAG_REPLY(reply_id), print_msg_success_gw, ev);
}

void do_reply_contact (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 4);
  if (ev) { ev->refcnt ++; }
  tgl_do_reply_contact (TLS, &args[0].msg_id, ARG2STR (1), ARG2STR (2), ARG2STR (3), 0, print_msg_success_gw, ev);
}

void do_send_location (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_location (TLS, args[0].peer_id, args[1].dval, args[2].dval, TGL_SEND_MSG_FLAG_REPLY(reply_id), print_msg_success_gw, ev);
}

void do_post_location (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_location (TLS, args[0].peer_id, args[1].dval, args[2].dval, TGL_SEND_MSG_FLAG_REPLY(reply_id) | TGLMF_POST_AS_CHANNEL, print_msg_success_gw, ev);
}

void do_reply_location (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_reply_location (TLS, &args[0].msg_id, args[1].dval, args[2].dval, 0, print_msg_success_gw, ev);
}

void do_broadcast (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num >= 1 && arg_num <= 1000);
  static tgl_peer_id_t ids[1000];
  int i;
  for (i = 0; i < arg_num - 1; i++) {
    ids[i] = args[i].peer_id;
  }  
  if (ev) { ev->refcnt ++; }
  tgl_do_send_broadcast (TLS, arg_num - 1, ids, args[arg_num - 1].str, strlen (args[arg_num - 1].str), disable_msg_preview | do_html, print_msg_list_success_gw, ev);
}

/* }}} */

/* {{{ EDITING SELF PROFILE */

void do_get_self(struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  if (ev) { ev->refcnt ++; }
  tgl_do_get_user_info (TLS, TLS->our_id, 0, print_user_info_gw, ev);
}

void do_set_profile_photo (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_set_profile_photo (TLS, args[0].str, print_success_gw, ev);
}

void do_set_profile_name (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_set_profile_name (TLS, ARG2STR (0), ARG2STR (1), print_user_gw, ev);
}

void do_set_username (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_set_username (TLS, ARG2STR (0), print_user_gw, ev);
}

void do_set_phone_number (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_set_phone_number (TLS, ARG2STR (0), print_success_gw, ev);
}

void do_status_online (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  if (ev) { ev->refcnt ++; }
  tgl_do_update_status (TLS, 1, print_success_gw, ev);
}

void do_status_offline (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  if (ev) { ev->refcnt ++; }
  tgl_do_update_status (TLS, 0, print_success_gw, ev);
}

void do_export_card (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  if (ev) { ev->refcnt ++; }
  tgl_do_export_card (TLS, print_card_gw, ev);
}

/* }}} */

/* {{{ WORKING WITH GROUP CHATS */

void do_chat_set_photo (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_set_chat_photo (TLS, args[0].peer_id, args[1].str, print_success_gw, ev); 
}

void do_rename_chat (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_rename_chat (TLS, args[0].peer_id, ARG2STR (1), print_success_gw, ev);
}

void do_chat_info (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_get_chat_info (TLS, args[0].peer_id, offline_mode, print_chat_info_gw, ev);
}

void do_channel_info (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_get_channel_info (TLS, args[0].peer_id, offline_mode, print_channel_info_gw, ev);
}

void do_chat_add_user (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_add_user_to_chat (TLS, args[0].peer_id, args[1].peer_id, args[2].num != NOT_FOUND ? args[2].num : 100, print_success_gw, ev);
}

void do_chat_del_user (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_del_user_from_chat (TLS, args[0].peer_id, args[1].peer_id, print_success_gw, ev);
}
    
void do_create_group_chat (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num >= 1 && arg_num <= 1000);
  static tgl_peer_id_t ids[1000];
  int i;
  for (i = 0; i < arg_num - 1; i++) {
    ids[i] = args[i + 1].peer_id;
  }

  if (ev) { ev->refcnt ++; }
  tgl_do_create_group_chat (TLS, arg_num - 1, ids, ARG2STR (0), print_success_gw, ev);  
}

void do_export_chat_link (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_export_chat_link (TLS, args[0].peer_id, print_string_gw, ev);
}

void do_import_chat_link (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_import_chat_link (TLS, ARG2STR (0), print_success_gw, ev);
}

void do_channel_invite (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_channel_invite_user (TLS, args[0].peer_id, args[1].peer_id, print_success_gw, ev);
}

void do_channel_kick (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_channel_kick_user (TLS, args[0].peer_id, args[1].peer_id, print_success_gw, ev);
}

void do_channel_get_members (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_channel_get_members (TLS, args[0].peer_id, args[1].num == NOT_FOUND ? 100 : args[1].num, args[2].num == NOT_FOUND ? 0 : args[2].num, 0, print_user_list_gw, ev);
}

void do_channel_get_admins (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_channel_get_members (TLS, args[0].peer_id, args[1].num == NOT_FOUND ? 100 : args[1].num, args[2].num == NOT_FOUND ? 0 : args[2].num, 1, print_user_list_gw, ev);
}

void do_chat_upgrade (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_upgrade_group (TLS, args[0].peer_id, print_success_gw, ev);
}


/* }}} */

 /* {{{ WORKING WITH USERS */


void do_user_info (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_get_user_info (TLS, args[0].peer_id, offline_mode, print_user_info_gw, ev);
}

void do_add_contact (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_add_contact (TLS, ARG2STR (0), ARG2STR (1), ARG2STR (2), 0, print_user_list_gw, ev);
}

void do_rename_contact (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  
  tgl_peer_t *P = tgl_peer_get (TLS, args[0].peer_id);
  if (P && P->user.phone) {
    if (ev) { ev->refcnt ++; }
    tgl_do_add_contact (TLS, P->user.phone, strlen (P->user.phone), args[1].str, strlen (args[1].str), args[2].str, strlen (args[2].str), 0, print_user_list_gw, ev);
  } else {
    if (ev) { ev->refcnt ++; }
    print_success_gw (TLS, ev, 0);
  }
}

void do_del_contact (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_del_contact (TLS, args[0].peer_id, print_success_gw, ev);
}


void do_import_card (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  char *s = args[0].str;
  int l = strlen (s);
  if (l > 0) {
    int i;
    static int p[10];
    int pp = 0;
    int cur = 0;
    int ok = 1;
    for (i = 0; i < l; i ++) {
      if (s[i] >= '0' && s[i] <= '9') {
        cur = cur * 16 + s[i] - '0';
      } else if (s[i] >= 'a' && s[i] <= 'f') {
        cur = cur * 16 + s[i] - 'a' + 10;
      } else if (s[i] == ':') {
        if (pp >= 9) { 
          ok = 0;
          break;
        }
        p[pp ++] = cur;
        cur = 0;
      }
    }
    if (ok) {
      p[pp ++] = cur;
      if (ev) { ev->refcnt ++; }
      tgl_do_import_card (TLS, pp, p, print_user_gw, ev);
    }
  }
}

void do_block_user (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_block_user (TLS, args[0].peer_id, print_success_gw, ev);
}

void do_unblock_user (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_unblock_user (TLS, args[0].peer_id, print_success_gw, ev);
}
/* }}} */

/* {{{ WORKING WITH SECRET CHATS */

void do_accept_secret_chat (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }

  tgl_peer_t *P = tgl_peer_get (TLS, args[0].peer_id);
  if (P) {
    tgl_do_accept_encr_chat_request (TLS, &P->encr_chat, print_encr_chat_success_gw, ev);
  } else {
    print_success_gw (TLS, ev, 0);
  }
}

void do_set_ttl (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_peer_t *P = tgl_peer_get (TLS, args[0].peer_id);
  if (P && P->encr_chat.state == sc_ok) {
    tgl_do_set_encr_chat_ttl (TLS, &P->encr_chat, args[1].num, print_msg_success_gw, ev);
  } else {
    print_success_gw (TLS, ev, 0);
  }
}

void do_visualize_key (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  static char *colors[4] = {COLOR_GREY, COLOR_CYAN, COLOR_BLUE, COLOR_GREEN};
  static unsigned char buf[16];
  memset (buf, 0, sizeof (buf));
  tgl_do_visualize_key (TLS, args[0].peer_id, buf);
  mprint_start (ev);
  int i;
  for (i = 0; i < 16; i++) {
    int x = buf[i];
    int j;
    for (j = 0; j < 4; j ++) {    
      if (!ev) {
        mpush_color (ev, colors[x & 3]);
        mpush_color (ev, COLOR_INVERSE);
      }
      if (!disable_colors && !ev) {
        mprintf (ev, "  ");
      } else {
        switch (x & 3) {
        case 0:
          mprintf (ev, "  ");
          break;
        case 1:
          mprintf (ev, "--");
          break;
        case 2:
          mprintf (ev, "==");
          break;
        case 3:
          mprintf (ev, "||");
          break;
        }
      }
      if (!ev) {
        mpop_color (ev);
        mpop_color (ev);
      }
      x = x >> 2;
    }
    if (i & 1) { 
      mprintf (ev, "\n"); 
    }
  }
  mprint_end (ev);
}


void do_create_secret_chat (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_create_secret_chat (TLS, args[0].peer_id, print_secret_chat_gw, ev);
}

/* }}} */

/* WORKING WITH CHANNELS {{{ */

void do_rename_channel (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_rename_channel (TLS, args[0].peer_id, ARG2STR (1), print_success_gw, ev);
}

void do_channel_set_photo (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_set_channel_photo (TLS, args[0].peer_id, args[1].str, print_success_gw, ev); 
}

void do_channel_set_about (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_channel_set_about (TLS, args[0].peer_id, ARG2STR (1), print_success_gw, ev);
}

void do_channel_set_admin (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_channel_set_admin (TLS, args[0].peer_id, args[1].peer_id, args[2].num, print_success_gw, ev);
}

void do_channel_set_username (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_channel_set_username (TLS, args[0].peer_id, ARG2STR (1), print_success_gw, ev);
}
    
void do_create_channel (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num >= 2 && arg_num <= 1000);
  static tgl_peer_id_t ids[1000];
  int i;
  for (i = 0; i < arg_num - 2; i++) {
    ids[i] = args[i + 2].peer_id;
  }

  if (ev) { ev->refcnt ++; }
  tgl_do_create_channel (TLS, arg_num - 2, ids, ARG2STR (0), ARG2STR (1), 1, print_success_gw, ev);  
}

void do_join_channel (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_join_channel (TLS, args[0].peer_id, print_success_gw, ev);
}

void do_leave_channel (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_leave_channel (TLS, args[0].peer_id, print_success_gw, ev);
}

void do_export_channel_link (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_export_channel_link (TLS, args[0].peer_id, print_string_gw, ev);
}

/* }}} */

/* {{{ WORKING WITH DIALOG LIST */

void do_dialog_list (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num <= 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_get_dialog_list (TLS, args[0].num != NOT_FOUND ? args[0].num : 100, args[1].num != NOT_FOUND ? args[1].num : 0, print_dialog_list_gw, ev);
}

void do_channel_list (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num <= 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_get_channels_dialog_list (TLS, args[0].num != NOT_FOUND ? args[0].num : 100, args[1].num != NOT_FOUND ? args[1].num : 0, print_dialog_list_gw, ev);
}

void do_resolve_username (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_contact_search (TLS, args[0].str, strlen (args[0].str), print_peer_gw, ev);
}

void do_contact_list (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  if (ev) { ev->refcnt ++; }
  tgl_do_update_contact_list (TLS, print_user_list_gw, ev);  
}

/* }}} */

/* {{{ WORKING WITH ONE DIALOG */

void do_mark_read (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_mark_read (TLS, args[0].peer_id, print_success_gw, ev);
}

void do_history (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_get_history (TLS, args[0].peer_id, args[2].num != NOT_FOUND ? args[2].num : 0, args[1].num != NOT_FOUND ? args[1].num : 40, offline_mode, print_msg_list_history_gw, ev);
}

void print_fail (struct in_ev *ev);

void do_send_typing (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  enum tgl_typing_status status = tgl_typing_typing; //de
  if (args[1].num != NOT_FOUND) {
    if (args[1].num > 0 && args[1].num > 10) {
      fail_interface (TLS, ev, ENOSYS, "illegal typing status");
      return;
    }
    status = (enum tgl_typing_status) args[1].num;  // if the status parameter is given, and is in range.
  }
  if (ev) { ev->refcnt ++; }
  tgl_do_send_typing (TLS, args[0].peer_id, status, print_success_gw, ev);
}

void do_send_typing_abort (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_typing (TLS, args[0].peer_id, tgl_typing_cancel, print_success_gw, ev);
}

/* }}} */

/* {{{ WORKING WITH MEDIA */

#define DO_LOAD_PHOTO(tp,act,actf) \
void do_ ## act ## _ ## tp (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) { \
  assert (arg_num == 1);\
  struct tgl_message *M = tgl_message_get (TLS, &args[0].msg_id);\
  if (M && !(M->flags & TGLMF_SERVICE)) {\
    if (ev) { ev->refcnt ++; } \
    if (M->media.type == tgl_message_media_photo) { \
      tgl_do_load_photo (TLS, M->media.photo, actf, ev);\
    } else if (M->media.type == tgl_message_media_document) {\
      tgl_do_load_document (TLS, M->media.document, actf, ev);\
    } else if (M->media.type == tgl_message_media_video) {\
      tgl_do_load_video (TLS, M->media.document, actf, ev);\
    } else if (M->media.type == tgl_message_media_audio) {\
      tgl_do_load_audio (TLS, M->media.document, actf, ev);\
    } else if (M->media.type == tgl_message_media_document_encr) {\
      tgl_do_load_encr_document (TLS, M->media.encr_document, actf, ev); \
    } else if (M->media.type == tgl_message_media_webpage) {\
      actf (TLS, ev, 1, M->media.webpage->url);\
    } else if (M->media.type == tgl_message_media_geo || M->media.type == tgl_message_media_venue) { \
      static char s[1000]; \
      sprintf (s, "https://maps.google.com/?q=%.6lf,%.6lf", M->media.geo.latitude, M->media.geo.longitude);\
      actf (TLS, ev, 1, s);\
    }\
  }\
}

#define DO_LOAD_PHOTO_THUMB(tp,act,actf) \
void do_ ## act ## _ ## tp ## _thumb (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) { \
  assert (arg_num == 1);\
  struct tgl_message *M = tgl_message_get (TLS, &args[0].msg_id);\
  if (M && !(M->flags & TGLMF_SERVICE)) {\
    if (M->media.type == tgl_message_media_document) {\
      if (ev) { ev->refcnt ++; } \
      tgl_do_load_document_thumb (TLS, M->media.document, actf, ev);\
    }\
  }\
}

DO_LOAD_PHOTO(photo, load, print_filename_gw)
DO_LOAD_PHOTO(video, load, print_filename_gw)
DO_LOAD_PHOTO(audio, load, print_filename_gw)
DO_LOAD_PHOTO(document, load, print_filename_gw)
DO_LOAD_PHOTO(file, load, print_filename_gw)
DO_LOAD_PHOTO_THUMB(video, load, print_filename_gw)
DO_LOAD_PHOTO_THUMB(document, load, print_filename_gw)
DO_LOAD_PHOTO_THUMB(file, load, print_filename_gw)
DO_LOAD_PHOTO(photo, open, open_filename_gw)
DO_LOAD_PHOTO(video, open, open_filename_gw)
DO_LOAD_PHOTO(audio, open, open_filename_gw)
DO_LOAD_PHOTO(document, open, open_filename_gw)
DO_LOAD_PHOTO(file, open, open_filename_gw)
DO_LOAD_PHOTO_THUMB(video, open, open_filename_gw)
DO_LOAD_PHOTO_THUMB(document, open, open_filename_gw)
DO_LOAD_PHOTO_THUMB(file, open, open_filename_gw)
DO_LOAD_PHOTO(any, open, open_filename_gw)

void do_load_user_photo  (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }

  tgl_peer_t *P = tgl_peer_get (TLS, args[0].peer_id);
  if (P) {
    tgl_do_load_file_location (TLS, &P->user.photo_big, print_filename_gw, ev);
  } else {
    print_filename_gw (TLS, ev, 0, NULL);
  }
}

void do_view_user_photo  (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  
  tgl_peer_t *P = tgl_peer_get (TLS, args[0].peer_id);
  if (P) {
    tgl_do_load_file_location (TLS, &P->user.photo_big, print_filename_gw, ev);
  } else {
    open_filename_gw (TLS, ev, 0, NULL);
  }
}

/* }}} */

/* {{{ ANOTHER MESSAGES FUNCTIONS */

void do_search (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 6);
  int limit;
  if (args[1].num != NOT_FOUND) {
    limit = args[1].num; 
  } else {
    limit = 40;
  }
  int from;
  if (args[2].num != NOT_FOUND) {
    from = args[2].num; 
  } else {
    from = 0;
  }
  int to;
  if (args[3].num != NOT_FOUND) {
    to = args[3].num; 
  } else {
    to = 0;
  }
  int offset;
  if (args[4].num != NOT_FOUND) {
    offset = args[4].num; 
  } else {
    offset = 0;
  }
  if (ev) { ev->refcnt ++; }
  tgl_do_msg_search (TLS, args[0].peer_id, from, to, limit, offset, args[5].str, strlen (args[5].str), print_msg_list_gw, ev);
}

void do_delete_msg (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  if (ev) { ev->refcnt ++; }
  tgl_do_delete_msg (TLS, &args[0].msg_id, print_success_gw, ev);
}

void do_get_message (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_get_message (TLS, &args[0].msg_id, print_msg_gw, ev);
}

/* }}} */

/* {{{ BOT */

void do_start_bot (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_start_bot (TLS, args[0].peer_id, args[1].peer_id, ARG2STR(2), print_success_gw, ev);
}
/* }}} */

extern char *default_username;
extern char *config_filename;
extern char *prefix;
extern char *auth_file_name;
extern char *state_file_name;
extern char *secret_chat_file_name;
extern char *downloads_directory;
extern char *config_directory;
extern char *binlog_file_name;
extern char *lua_file;
extern char *python_file;
extern struct event *term_ev;

void do_clear (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  logprintf ("Do_clear\n");
  free (default_username);
  tfree_str (config_filename);
  //free (prefix);
  tfree_str (auth_file_name);
  tfree_str (state_file_name);
  tfree_str (secret_chat_file_name);
  tfree_str (downloads_directory);
  //tfree_str (config_directory);
  tfree_str (binlog_file_name);
  tfree_str (lua_file);
  tfree_str (python_file);
  if (home_directory) {
    tfree_str (home_directory);
  }
  clear_history ();
  event_free (term_ev);
  struct event_base *ev_base = TLS->ev_base;
  tgl_free_all (TLS);
  event_base_free (ev_base);
  logprintf ("Bytes left allocated: %lld\n", tgl_get_allocated_bytes ());
  do_halt (0);
}


#define MAX_COMMANDS_SIZE 1000
struct command commands[MAX_COMMANDS_SIZE] = {
  {"accept_secret_chat", {ca_secret_chat, ca_none}, do_accept_secret_chat, "accept_secret_chat <secret chat>\tAccepts secret chat. Only useful with -E option", NULL},
  {"add_contact", {ca_string, ca_string, ca_string, ca_none}, do_add_contact, "add_contact <phone> <first name> <last name>\tTries to add user to contact list", NULL},
  {"block_user", {ca_user, ca_none}, do_block_user, "block_user <user>\tBlocks user", NULL},
  {"broadcast", {ca_user, ca_period, ca_string_end, ca_none}, do_broadcast, "broadcast <user>+ <text>\tSends text to several users at once", NULL},
  {"channel_get_admins", {ca_channel, ca_number | ca_optional, ca_number | ca_optional, ca_none}, do_channel_get_admins, "channel_get_admins <channel> [limit=100] [offset=0]\tGets channel admins", NULL},
  {"channel_get_members", {ca_channel, ca_number | ca_optional, ca_number | ca_optional, ca_none}, do_channel_get_members, "channel_get_members <channel> [limit=100] [offset=0]\tGets channel members", NULL},
  {"channel_info", {ca_channel, ca_none}, do_channel_info, "channel_info <channel>\tPrints info about channel (id, members, admin, etc.)", NULL},
  {"channel_invite", {ca_channel, ca_user, ca_none}, do_channel_invite, "channel_invite <channel> <user>\tInvites user to channel", NULL},
  {"channel_join", {ca_channel, ca_none}, do_join_channel, "channel_join <channel>\tJoins to channel", NULL},
  {"channel_kick", {ca_channel, ca_user, ca_none}, do_channel_kick, "channel_kick <channel> <user>\tKicks user from channel", NULL},
  {"channel_leave", {ca_channel, ca_none}, do_leave_channel, "channel_leave <channel>\tLeaves from channel", NULL},
  {"channel_list", {ca_number | ca_optional, ca_number | ca_optional, ca_none}, do_channel_list, "channel_list [limit=100] [offset=0]\tList of last channels", NULL},
  {"channel_set_about", {ca_channel, ca_string, ca_none}, do_channel_set_about, "channel_set_about <channel> <about>\tSets channel about info.", NULL},
  {"channel_set_admin", {ca_channel, ca_user, ca_number, ca_none}, do_channel_set_admin, "channel_set_admin <channel> <admin> <type>\tSets channel admin. 0 - not admin, 1 - moderator, 2 - editor", NULL},
  {"channel_set_username", {ca_channel, ca_string, ca_none}, do_channel_set_username, "channel_set_username <channel> <username>\tSets channel username info.", NULL},
  {"channel_set_photo", {ca_channel, ca_file_name_end, ca_none}, do_channel_set_photo, "channel_set_photo <channel> <filename>\tSets channel photo. Photo will be cropped to square", NULL},
  {"chat_add_user", {ca_chat, ca_user, ca_number | ca_optional, ca_none}, do_chat_add_user, "chat_add_user <chat> <user> [msgs-to-forward]\tAdds user to chat. Sends him last msgs-to-forward message from this chat. Default 100", NULL},
  {"chat_del_user", {ca_chat, ca_user, ca_none}, do_chat_del_user, "chat_del_user <chat> <user>\tDeletes user from chat", NULL},
  {"chat_info", {ca_chat, ca_none}, do_chat_info, "chat_info <chat>\tPrints info about chat (id, members, admin, etc.)", NULL},
  {"chat_set_photo", {ca_chat, ca_file_name_end, ca_none}, do_chat_set_photo, "chat_set_photo <chat> <filename>\tSets chat photo. Photo will be cropped to square", NULL},
  {"chat_upgrade", {ca_chat, ca_none}, do_chat_upgrade, "chat_upgrade <chat>\tUpgrades chat to megagroup", NULL},
  {"chat_with_peer", {ca_peer, ca_none}, do_chat_with_peer, "chat_with_peer <peer>\tInterface option. All input will be treated as messages to this peer. Type /quit to end this mode", NULL},
  {"clear", {ca_none}, do_clear, "clear\tClears all data and exits. For debug.", NULL},
  {"contact_list", {ca_none}, do_contact_list, "contact_list\tPrints contact list", NULL},
  {"contact_search", {ca_string, ca_none}, do_resolve_username, "contact_search username\tSearches user by username", NULL},
  {"create_channel", {ca_string, ca_string, ca_user | ca_optional, ca_period, ca_none}, do_create_channel, "create_channel <name> <about> <user>+\tCreates channel with users", NULL},
  {"create_group_chat", {ca_string, ca_user, ca_period, ca_none}, do_create_group_chat, "create_group_chat <name> <user>+\tCreates group chat with users", NULL},
  {"create_secret_chat", {ca_user, ca_none}, do_create_secret_chat, "create_secret_chat <user>\tStarts creation of secret chat", NULL},
  {"del_contact", {ca_user, ca_none}, do_del_contact, "del_contact <user>\tDeletes contact from contact list", NULL},
  {"delete_msg", {ca_msg_id, ca_none}, do_delete_msg, "delete_msg <msg-id>\tDeletes message", NULL},
  {"dialog_list", {ca_number | ca_optional, ca_number | ca_optional, ca_none}, do_dialog_list, "dialog_list [limit=100] [offset=0]\tList of last conversations", NULL},
  {"export_card", {ca_none}, do_export_card, "export_card\tPrints card that can be imported by another user with import_card method", NULL},
  {"export_channel_link", {ca_channel, ca_none}, do_export_channel_link, "export_channel_link\tPrints channel link that can be used to join to channel", NULL},
  {"export_chat_link", {ca_chat, ca_none}, do_export_chat_link, "export_chat_link\tPrints chat link that can be used to join to chat", NULL},
  {"fwd", {ca_peer, ca_msg_id, ca_period, ca_none}, do_fwd, "fwd <peer> <msg-id>+\tForwards message to peer. Forward to secret chats is forbidden", NULL},
  {"fwd_media", {ca_peer, ca_msg_id, ca_none}, do_fwd_media, "fwd_media <peer> <msg-id>\tForwards message media to peer. Forward to secret chats is forbidden. Result slightly differs from fwd", NULL},
  {"get_terms_of_service", {ca_none}, do_get_terms_of_service, "get_terms_of_service\tPrints telegram's terms of service", NULL},
  {"get_message", {ca_msg_id, ca_none}, do_get_message, "get_message <msg-id>\tGet message by id", NULL},
  {"get_self", {ca_none}, do_get_self, "get_self \tGet our user info", NULL},
  {"help", {ca_command | ca_optional, ca_none}, do_help, "help [command]\tPrints this help", NULL},
  {"history", {ca_peer, ca_number | ca_optional, ca_number | ca_optional, ca_none}, do_history, "history <peer> [limit] [offset]\tPrints messages with this peer (most recent message lower). Also marks messages as read", NULL},
  {"import_card", {ca_string, ca_none}, do_import_card, "import_card <card>\tGets user by card and prints it name. You can then send messages to him as usual", NULL},
  {"import_chat_link", {ca_string, ca_none}, do_import_chat_link, "import_chat_link <hash>\tJoins to chat by link", NULL},
  {"import_channel_link", {ca_string, ca_none}, do_import_chat_link, "import_channel_link <hash>\tJoins to channel by link", NULL},
  {"load_audio", {ca_msg_id, ca_none}, do_load_audio, "load_audio <msg-id>\tDownloads file to downloads dirs. Prints file name after download end", NULL},
  {"load_channel_photo", {ca_channel, ca_none}, do_load_user_photo, "load_channel_photo <channel>\tDownloads file to downloads dirs. Prints file name after download end", NULL},
  {"load_chat_photo", {ca_chat, ca_none}, do_load_user_photo, "load_chat_photo <chat>\tDownloads file to downloads dirs. Prints file name after download end", NULL},
  {"load_document", {ca_msg_id, ca_none}, do_load_document, "load_document <msg-id>\tDownloads file to downloads dirs. Prints file name after download end", NULL},
  {"load_document_thumb", {ca_msg_id, ca_none}, do_load_document_thumb, "load_document_thumb <msg-id>\tDownloads file to downloads dirs. Prints file name after download end", NULL},
  {"load_file", {ca_msg_id, ca_none}, do_load_file, "load_file <msg-id>\tDownloads file to downloads dirs. Prints file name after download end", NULL},
  {"load_file_thumb", {ca_msg_id, ca_none}, do_load_file_thumb, "load_file_thumb <msg-id>\tDownloads file to downloads dirs. Prints file name after download end", NULL},
  {"load_photo", {ca_msg_id, ca_none}, do_load_photo, "load_photo <msg-id>\tDownloads file to downloads dirs. Prints file name after download end", NULL},
  {"load_user_photo", {ca_user, ca_none}, do_load_user_photo, "load_user_photo <user>\tDownloads file to downloads dirs. Prints file name after download end", NULL},
  {"load_video", {ca_msg_id, ca_none}, do_load_video, "load_video <msg-id>\tDownloads file to downloads dirs. Prints file name after download end", NULL},
  {"load_video_thumb", {ca_msg_id, ca_none}, do_load_video_thumb, "load_video_thumb <msg-id>\tDownloads file to downloads dirs. Prints file name after download end", NULL},
  {"main_session", {ca_none}, do_main_session, "main_session\tSends updates to this connection (or terminal). Useful only with listening socket", NULL},
  {"mark_read", {ca_peer, ca_none}, do_mark_read, "mark_read <peer>\tMarks messages with peer as read", NULL},
  {"msg", {ca_peer, ca_msg_string_end, ca_none}, do_msg, "msg <peer> <text>\tSends text message to peer", NULL},
  {"msg_kbd", {ca_peer, ca_string, ca_msg_string_end, ca_none}, do_msg_kbd, "msg <peer> <kbd> <text>\tSends text message to peer with custom kbd", NULL},
  {"post", {ca_peer, ca_msg_string_end, ca_none}, do_post, "post <peer> <text>\tSends text message to peer as admin", NULL},
  {"post_audio", {ca_peer, ca_file_name, ca_none}, do_post_audio, "post_audio <peer> <file>\tPosts audio to peer", NULL},
  {"post_document", {ca_peer, ca_file_name, ca_none}, do_post_document, "post_document <peer> <file>\tPosts document to peer", NULL},
  {"post_file", {ca_peer, ca_file_name, ca_none}, do_post_file, "post_file <peer> <file>\tSends document to peer", NULL},
  {"post_location", {ca_peer, ca_double, ca_double, ca_none}, do_post_location, "post_location <peer> <latitude> <longitude>\tSends geo location", NULL},
  {"post_photo", {ca_peer, ca_file_name, ca_string_end | ca_optional, ca_none}, do_post_photo, "post_photo <peer> <file> [caption]\tSends photo to peer", NULL},
  {"post_text", {ca_peer, ca_file_name_end, ca_none}, do_post_text, "post_text <peer> <file>\tSends contents of text file as plain text message", NULL},
  {"post_video", {ca_peer, ca_file_name, ca_string_end | ca_optional, ca_none}, do_post_video, "post_video <peer> <file> [caption]\tSends video to peer", NULL},
  {"quit", {ca_none}, do_quit, "quit\tQuits immediately", NULL},
  {"rename_channel", {ca_channel, ca_string_end, ca_none}, do_rename_channel, "rename_channel <channel> <new name>\tRenames channel", NULL},
  {"rename_chat", {ca_chat, ca_string_end, ca_none}, do_rename_chat, "rename_chat <chat> <new name>\tRenames chat", NULL},
  {"rename_contact", {ca_user, ca_string, ca_string, ca_none}, do_rename_contact, "rename_contact <user> <first name> <last name>\tRenames contact", NULL},
  {"reply", {ca_msg_id, ca_msg_string_end, ca_none}, do_reply, "reply <msg-id> <text>\tSends text reply to message", NULL},
  {"reply_audio", {ca_msg_id, ca_file_name, ca_none}, do_send_audio, "reply_audio <msg-id> <file>\tSends audio to peer", NULL},
  {"reply_contact", {ca_msg_id, ca_string, ca_string, ca_string, ca_none}, do_reply_contact, "reply_contact <msg-id> <phone> <first-name> <last-name>\tSends contact (not necessary telegram user)", NULL},
  {"reply_document", {ca_msg_id, ca_file_name, ca_none}, do_reply_document, "reply_document <msg-id> <file>\tSends document to peer", NULL},
  {"reply_file", {ca_msg_id, ca_file_name, ca_none}, do_reply_file, "reply_file <msg-id> <file>\tSends document to peer", NULL},
  {"reply_location", {ca_msg_id, ca_double, ca_double, ca_none}, do_reply_location, "reply_location <msg-id> <latitude> <longitude>\tSends geo location", NULL},
  {"reply_photo", {ca_msg_id, ca_file_name, ca_string_end | ca_optional, ca_none}, do_reply_photo, "reply_photo <msg-id> <file> [caption]\tSends photo to peer", NULL},
  //{"reply_text", {ca_number, ca_file_name_end, ca_none}, do_reply_text, "reply_text <msg-id> <file>\tSends contents of text file as plain text message", NULL},
  {"reply_video", {ca_msg_id, ca_file_name, ca_none}, do_reply_video, "reply_video <msg-id> <file>\tSends video to peer", NULL},
  {"resolve_username", {ca_string, ca_none}, do_resolve_username, "resolve_username username\tSearches user by username", NULL},
  //{"restore_msg", {ca_number, ca_none}, do_restore_msg, "restore_msg <msg-id>\tRestores message. Only available shortly (one hour?) after deletion", NULL},
  {"safe_quit", {ca_none}, do_safe_quit, "safe_quit\tWaits for all queries to end, then quits", NULL},
  {"search", {ca_peer | ca_optional, ca_number | ca_optional, ca_number | ca_optional, ca_number | ca_optional, ca_number | ca_optional, ca_string_end}, do_search, "search [peer] [limit] [from] [to] [offset] pattern\tSearch for pattern in messages from date from to date to (unixtime) in messages with peer (if peer not present, in all messages)", NULL},
  //{"secret_chat_rekey", { ca_secret_chat, ca_none}, do_secret_chat_rekey, "generate new key for active secret chat", NULL},
  {"send_audio", {ca_peer, ca_file_name, ca_string_end | ca_optional, ca_none}, do_send_audio, "send_audio <peer> <file>\tSends audio to peer", NULL},
  {"send_contact", {ca_peer, ca_string, ca_string, ca_string, ca_none}, do_send_contact, "send_contact <peer> <phone> <first-name> <last-name>\tSends contact (not necessary telegram user)", NULL},
  {"send_document", {ca_peer, ca_file_name, ca_string_end | ca_optional, ca_none}, do_send_document, "send_document <peer> <file>\tSends document to peer", NULL},
  {"send_file", {ca_peer, ca_file_name, ca_string_end | ca_optional, ca_none}, do_send_file, "send_file <peer> <file>\tSends document to peer", NULL},
  {"send_location", {ca_peer, ca_double, ca_double, ca_none}, do_send_location, "send_location <peer> <latitude> <longitude>\tSends geo location", NULL},
  {"send_photo", {ca_peer, ca_file_name, ca_string_end | ca_optional, ca_none}, do_send_photo, "send_photo <peer> <file> [caption]\tSends photo to peer", NULL},
  {"send_text", {ca_peer, ca_file_name_end, ca_none}, do_send_text, "send_text <peer> <file>\tSends contents of text file as plain text message", NULL},
  {"send_typing", {ca_peer, ca_number | ca_optional, ca_none}, do_send_typing, "send_typing <peer> [status]\tSends typing notification. You can supply a custom status (range 0-10): none, typing, cancel, record video, upload video, record audio, upload audio, upload photo, upload document, geo, choose contact.", NULL},
  {"send_typing_abort", {ca_peer, ca_none}, do_send_typing_abort, "send_typing_abort <peer>\tSends typing notification abort", NULL},
  {"send_video", {ca_peer, ca_file_name, ca_string_end | ca_optional, ca_none}, do_send_video, "send_video <peer> <file> [caption]\tSends video to peer", NULL},
  {"set", {ca_string, ca_number, ca_none}, do_set, "set <param> <value>\tSets value of param. Currently available: log_level, debug_verbosity, alarm, msg_num", NULL},
  {"set_password", {ca_string | ca_optional, ca_none}, do_set_password, "set_password <hint>\tSets password", NULL},
  {"set_profile_name", {ca_string, ca_string, ca_none}, do_set_profile_name, "set_profile_name <first-name> <last-name>\tSets profile name.", NULL},
  {"set_profile_photo", {ca_file_name_end, ca_none}, do_set_profile_photo, "set_profile_photo <filename>\tSets profile photo. Photo will be cropped to square", NULL},
  {"set_ttl", {ca_secret_chat, ca_number,  ca_none}, do_set_ttl, "set_ttl <secret chat>\tSets secret chat ttl. Client itself ignores ttl", NULL},
  {"set_username", {ca_string, ca_none}, do_set_username, "set_username <name>\tSets username.", NULL},
  {"set_phone_number", {ca_string, ca_none}, do_set_phone_number, "set_phone_number <phone>\tChanges the phone number of this account", NULL},
  {"show_license", {ca_none}, do_show_license, "show_license\tPrints contents of GPL license", NULL},
  {"start_bot", {ca_user, ca_chat, ca_string, ca_none}, do_start_bot, "start_bot <bot> <chat> <data>\tAdds bot to chat", NULL},
  {"stats", {ca_none}, do_stats, "stats\tFor debug purpose", NULL},
  {"status_online", {ca_none}, do_status_online, "status_online\tSets status as online", NULL},
  {"status_offline", {ca_none}, do_status_offline, "status_offline\tSets status as offline", NULL},
  {"unblock_user", {ca_user, ca_none}, do_unblock_user, "unblock_user <user>\tUnblocks user", NULL},
  {"user_info", {ca_user, ca_none}, do_user_info, "user_info <user>\tPrints info about user (id, last online, phone)", NULL},
  {"version", {ca_none}, do_version, "version\tPrints client and library version", NULL},
  {"view_audio", {ca_msg_id, ca_none}, do_open_audio, "view_audio <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action", NULL},
  {"view_channel_photo", {ca_channel, ca_none}, do_view_user_photo, "view_channel_photo <channel>\tDownloads file to downloads dirs. Then tries to open it with system default action", NULL},
  {"view_chat_photo", {ca_chat, ca_none}, do_view_user_photo, "view_chat_photo <chat>\tDownloads file to downloads dirs. Then tries to open it with system default action", NULL},
  {"view_document", {ca_msg_id, ca_none}, do_open_document, "view_document <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action", NULL},
  {"view_document_thumb", {ca_msg_id, ca_none}, do_open_document_thumb, "view_document_thumb <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action", NULL},
  {"view_file", {ca_msg_id, ca_none}, do_open_file, "view_file <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action", NULL},
  {"view_file_thumb", {ca_msg_id, ca_none}, do_open_file_thumb, "view_file_thumb <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action", NULL},
  {"view_photo", {ca_msg_id, ca_none}, do_open_photo, "view_photo <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action", NULL},
  {"view_user_photo", {ca_user, ca_none}, do_view_user_photo, "view_user_photo <user>\tDownloads file to downloads dirs. Then tries to open it with system default action", NULL},
  {"view_video", {ca_msg_id, ca_none}, do_open_video, "view_video <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action", NULL},
  {"view_video_thumb", {ca_msg_id, ca_none}, do_open_video_thumb, "view_video_thumb <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action", NULL},
  {"view", {ca_msg_id, ca_none}, do_open_any, "view <msg-id>\tTries to view message contents", NULL},
  {"visualize_key", {ca_secret_chat, ca_none}, do_visualize_key, "visualize_key <secret chat>\tPrints visualization of encryption key (first 16 bytes sha1 of it in fact)", NULL}
};

void register_new_command (struct command *cmd) {
  int i = 0;
  while (commands[i].name) {
    i ++;
  }
  assert (i < MAX_COMMANDS_SIZE - 1);
  commands[i] = *cmd;
}

tgl_peer_t *autocomplete_peer;
tgl_message_id_t autocomplete_id;

enum command_argument get_complete_mode (void) {
  force_end_mode = 0;
  line_ptr = rl_line_buffer;
  autocomplete_peer = NULL;
  autocomplete_id.peer_type = NOT_FOUND;

  while (1) {
    next_token ();
    if (cur_token_quoted) { return ca_none; }
    if (cur_token_len <= 0) { return ca_command; }
    if (*cur_token == '[') {
      if (cur_token_end_str) {
        return ca_modifier; 
      }
      if (cur_token[cur_token_len - 1] != ']') {
        return ca_none;
      }
      continue;
    }
    break;
  }
  if (cur_token_quoted) { return ca_none; }
  if (cur_token_end_str) { return ca_command; }
  if (*cur_token == '(') { return ca_extf; }
  
  struct command *command = commands;
  int n = 0;
  struct tgl_command;
  while (command->name) {
    if (is_same_word (cur_token, cur_token_len, command->name)) {
      break;
    }
    n ++;
    command ++;
  }
  
  if (!command->name) {
    return ca_none;
  }

  enum command_argument *flags = command->args;
  while (1) {
    int period = 0;
    if (*flags == ca_period) {
      flags --;
      period = 1;
    }
    enum command_argument op = (*flags) & 255;
    int opt = (*flags) & ca_optional;

    if (op == ca_none) { return ca_none; }
    if (op == ca_string_end || op == ca_file_name_end || op == ca_msg_string_end) {
      next_token_end_ac ();

      if (cur_token_len < 0 || !cur_token_end_str) { 
        return ca_none;
      } else {
        return op;
      }
    }
    
    char *save = line_ptr;
    next_token ();
    if (op == ca_user || op == ca_chat || op == ca_secret_chat || op == ca_peer || op == ca_number || op == ca_double || op == ca_msg_id || op == ca_command || op == ca_channel) {
      if (cur_token_quoted) {
        if (opt) {
          line_ptr = save;
          flags ++;
          continue;
        } else if (period) {
          line_ptr = save;
          flags += 2;
          continue;
        } else {
          return ca_none;
        }
      } else {
        if (cur_token_end_str) { return op; }
        
        int ok = 1;
        switch (op) {
        case ca_user:
          ok = (tgl_get_peer_type (cur_token_user ()) != NOT_FOUND);
          break;
        case ca_chat:
          ok = (tgl_get_peer_type (cur_token_chat ()) != NOT_FOUND);
          break;
        case ca_secret_chat:
          ok = (tgl_get_peer_type (cur_token_encr_chat ()) != NOT_FOUND);
          break;
        case ca_channel:
          ok = (tgl_get_peer_type (cur_token_channel ()) != NOT_FOUND);
          break;
        case ca_peer:
          ok = (tgl_get_peer_type (cur_token_peer ()) != NOT_FOUND);
          if (ok) {
            autocomplete_peer = tgl_peer_get (TLS, cur_token_peer ());
            autocomplete_id.peer_type = NOT_FOUND;
          }
          break;
        case ca_number:
          ok = (cur_token_int () != NOT_FOUND);
          break;
        case ca_msg_id:
          ok = (cur_token_msg_id ().peer_type != 0);
          if (ok) {
            autocomplete_peer = NULL;
            autocomplete_id = cur_token_msg_id ();
          }
          break;
        case ca_double:
          ok = (cur_token_double () != NOT_FOUND);
          break;
        case ca_command:
          ok = cur_token_len > 0;
          break;
        default:
          assert (0);
        }

        if (opt && !ok) {
          line_ptr = save;
          flags ++;
          continue;
        }
        if (period && !ok) {
          line_ptr = save;
          flags += 2;
          continue;
        }
        if (!ok) {
          return ca_none;
        }

        flags ++;
        continue;
      }
    }
    if (op == ca_string || op == ca_file_name) {
      if (cur_token_end_str) {
        return op;
      } else {
        flags ++;
        continue;
      }
    }
    assert (0);
  }
}

int complete_string_list (char **list, int index, const char *text, int len, char **R) {
  index ++;
  while (list[index] && strncmp (list[index], text, len)) {
    index ++;
  }
  if (list[index]) {
    *R = strdup (list[index]);
    assert (*R);
    return index;
  } else {
    *R = 0;
    return -1;
  }
}
void print_msg_success_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_message *M);
void print_encr_chat_success_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_secret_chat *E);;
void print_success_gw (struct tgl_state *TLS, void *extra, int success);

int complete_command_list (int index, const char *text, int len, char **R) {
  index ++;
  while (commands[index].name && strncmp (commands[index].name, text, len)) {
    index ++;
  }
  if (commands[index].name) {
    *R = strdup (commands[index].name);
    assert (*R);
    return index;
  } else {
    *R = 0;
    return -1;
  }
}


int complete_spec_message_answer (struct tgl_message *M, int index, const char *text, int len, char **R) {
  if (!M || !M->reply_markup || !M->reply_markup->rows) {
    *R = NULL;
    return -1;
  }
  index ++;

  int total = M->reply_markup->row_start[M->reply_markup->rows];
  while (index < total && strncmp (M->reply_markup->buttons[index], text, len)) {
    index ++;
  }
  
  if (index < total) {
    *R = strdup (M->reply_markup->buttons[index]);
    assert (*R);
    return index;
  } else {
    *R = NULL;
    return -1;
  }
}

int complete_message_answer (tgl_peer_t *P, int index, const char *text, int len, char **R) {
  struct tgl_message *M = P->last;
  while (M && (M->flags & TGLMF_OUT)) {
    M = M->next;
  }


  return complete_spec_message_answer (M, index, text, len, R);
}

int complete_user_command (tgl_peer_t *P, int index, const char *text, int len, char **R) {
  if (len <= 0 || *text != '/') {
    return complete_message_answer (P, index, text, len, R);
  }
  text ++;
  len --;
  struct tgl_user *U = (void *)P;
  if (!U->bot_info) {
    *R = NULL;
    return -1;
  }
  if (index >= U->bot_info->commands_num) {
    return U->bot_info->commands_num + complete_message_answer (P, index - U->bot_info->commands_num, text - 1, len + 1, R);
  }
  
  index ++;
  while (index < U->bot_info->commands_num && strncmp (U->bot_info->commands[index].command, text, len)) {
    index ++;
  }
  if (index < U->bot_info->commands_num) {
    *R = NULL;
    assert (asprintf (R, "/%s", U->bot_info->commands[index].command) >= 0);
    assert (*R);
    return index;
  } else {
    return U->bot_info->commands_num + complete_message_answer (P, index - U->bot_info->commands_num, text - 1, len + 1, R);
  }
}

int complete_chat_command (tgl_peer_t *P, int index, const char *text, int len, char **R) {
  if (len <= 0 || *text != '/') {
    return complete_message_answer (P, index, text, len, R);
  }
  text ++;
  len --;

  index ++;

  int tot = 0;
  int i;
  for (i = 0; i < P->chat.user_list_size; i++) { 
    struct tgl_user *U = (void *)tgl_peer_get (TLS, TGL_MK_USER (P->chat.user_list[i].user_id));
    if (!U) { continue; }
    if (!U->bot_info) { continue; }
    int p = len - 1;
    while (p >= 0 && text[p] != '@') { p --; }
    if (p < 0) { p = len; }
    while (index - tot < U->bot_info->commands_num && strncmp (U->bot_info->commands[index - tot].command, text, p)) {
      index ++;
    }
    if (index - tot < U->bot_info->commands_num) {
      *R = NULL;
      if (U->username) {
        assert (asprintf (R, "/%s@%s", U->bot_info->commands[index].command, U->username) >= 0);
      } else {
        assert (asprintf (R, "/%s", U->bot_info->commands[index].command) >= 0);
      }

      assert (*R);
      return index;
    }
    tot += U->bot_info->commands_num;
  }

  if (index == tot) {
    return tot + complete_message_answer (P, index - tot, text - 1, len + 1, R);
  } else {
    return tot + complete_message_answer (P, index - tot - 1, text - 1, len + 1, R);
  }
}

int complete_username (int mode, int index, const char *text, int len, char **R) {
  *R = NULL;
  if (len > 0 && *text == '@') {
    text ++;
    len --;
  }
  index ++;
  while (index < TLS->peer_num) {
    tgl_peer_t *P = TLS->Peers[index];
    if (mode && tgl_get_peer_type (P->id) != mode) {
      index ++;
      continue;
    }
    char *u = NULL;
    if (tgl_get_peer_type (P->id) == TGL_PEER_USER) {
      u = P->user.username;
    } else if (tgl_get_peer_type (P->id) == TGL_PEER_CHANNEL) {
      u = P->channel.username;
    }
    if (!u) {
      index ++;
      continue;
    }
    if ((int)strlen (u) < len || memcmp (u, text, len)) {
      index ++;
      continue;
    }
    *R = malloc (strlen (u) + 2);
    *R[0] = '@';
    memcpy (*R + 1, u, strlen (u) + 1);
    break;
  }
  if (index == TLS->peer_num) {
    return -1;
  }
  return index;
}

char *command_generator (const char *text, int state) {  
#ifndef DISABLE_EXTF
  static int len;
#endif
  static int index;
  static enum command_argument mode;
  static char *command_pos;
  static int command_len;

  if (in_chat_mode) {
    char *R = 0;
    index = complete_string_list (in_chat_commands, index, text, rl_point, &R);
    return R;
  }
 
  char c = 0;
  c = rl_line_buffer[rl_point];
  rl_line_buffer[rl_point] = 0;
  if (!state) {
#ifndef DISABLE_EXTF
    len = strlen (text);
#endif
    index = -1;
    
    mode = get_complete_mode ();
    command_pos = cur_token;
    command_len = cur_token_len;
  } else {
    if (mode != ca_file_name && mode != ca_file_name_end && index == -1) { return 0; }
  }
  
  if (mode == ca_none || mode == ca_string || mode == ca_string_end || mode == ca_number || mode == ca_double || mode == ca_msg_id) {   
    if (c) { rl_line_buffer[rl_point] = c; }
    return 0; 
  }
  assert (command_len >= 0);

  char *R = 0;
  switch (mode & 255) {
  case ca_command:
    index = complete_command_list (index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_user:
    if (command_len && command_pos[0] == '@') {
      index = complete_username (TGL_PEER_USER, index, command_pos, command_len, &R);
    } else {
      index = tgl_complete_user_list (TLS, index, command_pos, command_len, &R);    
    }
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_peer:
    if (command_len && command_pos[0] == '@') {
      index = complete_username (0, index, command_pos, command_len, &R);
    } else {
      index = tgl_complete_peer_list (TLS, index, command_pos, command_len, &R);
    }
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_file_name:
  case ca_file_name_end:
    if (c) { rl_line_buffer[rl_point] = c; }
    R = rl_filename_completion_function (command_pos, state);
    return R;
  case ca_chat:
    index = tgl_complete_chat_list (TLS, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_secret_chat:
    index = tgl_complete_encr_chat_list (TLS, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_channel:
    if (command_len && command_pos[0] == '@') {
      index = complete_username (TGL_PEER_CHANNEL, index, command_pos, command_len, &R);
    } else {
      index = tgl_complete_channel_list (TLS, index, command_pos, command_len, &R);    
    }
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_modifier:
    index = complete_string_list (modifiers, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_msg_string_end:
    if (autocomplete_peer) {
      if (tgl_get_peer_type (autocomplete_peer->id) == TGL_PEER_USER) {
        index = complete_user_command (autocomplete_peer, index, command_pos, command_len, &R);
      }
      if (tgl_get_peer_type (autocomplete_peer->id) == TGL_PEER_CHAT) {
        index = complete_chat_command (autocomplete_peer, index, command_pos, command_len, &R);
      }
    }
    if (autocomplete_id.peer_type != (unsigned)NOT_FOUND) {
      struct tgl_message *M = tgl_message_get (TLS, &autocomplete_id);
      if (M) {
        if (command_len > 0 && *command_pos == '/') {
          tgl_peer_t *P = tgl_peer_get (TLS, M->from_id);
          if (P) {
            index = complete_user_command (autocomplete_peer, index, command_pos, command_len, &R);
          }
        } else {
          index = complete_spec_message_answer (M, index, command_pos, command_len, &R);
        }
      }
    }
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
#ifndef DISABLE_EXTF
  case ca_extf:
    index = tglf_extf_autocomplete (TLS, text, len, index, &R, rl_line_buffer, rl_point);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
#endif
  default:
    if (c) { rl_line_buffer[rl_point] = c; }
    return 0;
  }
}

int count = 1;
void work_modifier (const char *s, int l) {
  if (is_same_word (s, l, "[offline]")) {
    offline_mode = 1;
  }
  if (sscanf (s, "[reply=%d]", &reply_id) >= 1) {
  }
  
  if (is_same_word (s, l, "[html]")) {
    do_html = TGLMF_HTML;
  }
  if (is_same_word (s, l, "[disable_preview]")) {
    disable_msg_preview = TGL_SEND_MSG_FLAG_DISABLE_PREVIEW;
  }
  if (is_same_word (s, l, "[enable_preview]")) {
    disable_msg_preview = TGL_SEND_MSG_FLAG_ENABLE_PREVIEW;
  }
#ifdef ALLOW_MULT
  if (sscanf (s, "[x%d]", &count) >= 1) {
  }
#endif
}

void print_fail (struct in_ev *ev) {
  mprint_start (ev);
  if (!enable_json) {
    mprintf (ev, "FAIL: %d: %s\n", TLS->error_code, TLS->error);
  } else {
  #ifdef USE_JSON
    json_t *res = json_object ();
    assert (json_object_set (res, "result", json_string ("FAIL")) >= 0);
    assert (json_object_set (res, "error_code", json_integer (TLS->error_code)) >= 0);
    assert (json_object_set (res, "error", json_string (TLS->error)) >= 0);
    char *s = json_dumps (res, 0);
    mprintf (ev, "%s\n", s);
    json_decref (res);
    free (s);
  #endif
  }
  mprint_end (ev);
}

void fail_interface (struct tgl_state *TLS, struct in_ev *ev, int error_code, const char *format, ...) {
  static char error[1001];

  va_list ap;
  va_start (ap, format);
  int error_len = vsnprintf (error, 1000, format, ap);
  va_end (ap);
  if (error_len > 1000) { error_len = 1000; }
  error[error_len] = 0;

  mprint_start (ev);
  if (!enable_json) {
    mprintf (ev, "FAIL: %d: %s\n", error_code, error);
  } else {
  #ifdef USE_JSON
    json_t *res = json_object ();
    assert (json_object_set (res, "result", json_string ("FAIL")) >= 0);
    assert (json_object_set (res, "error_code", json_integer (error_code)) >= 0);
    assert (json_object_set (res, "error", json_string (error)) >= 0);
    char *s = json_dumps (res, 0);
    mprintf (ev, "%s\n", s);
    json_decref (res);
    free (s);
  #endif
  }
  mprint_end (ev);
}

void print_success (struct in_ev *ev) {
  if (ev || enable_json) {
    mprint_start (ev);
    if (!enable_json) {
      mprintf (ev, "SUCCESS\n");
    } else {
      #ifdef USE_JSON
        json_t *res = json_object ();
        assert (json_object_set (res, "result", json_string ("SUCCESS")) >= 0);
        char *s = json_dumps (res, 0);
        mprintf (ev, "%s\n", s);
        json_decref (res);
        free (s);
      #endif
    }
    mprint_end (ev);
  }
}

void print_success_gw (struct tgl_state *TLSR, void *extra, int success) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  else { print_success (ev); return; }
}

void print_msg_success_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_message *M) {
  write_secret_chat_file ();
  print_success_gw (TLS, extra, success);
}

void print_msg_list_success_gw (struct tgl_state *TLSR, void *extra, int success, int num, struct tgl_message *ML[]) {
  assert (TLS == TLSR);
  print_success_gw (TLSR, extra, success);
}

void print_encr_chat_success_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_secret_chat *E) {
  write_secret_chat_file ();
  print_success_gw (TLS, extra, success);
}

void print_msg_list_gw (struct tgl_state *TLSR, void *extra, int success, int num, struct tgl_message *ML[]) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }

  mprint_start (ev);
  if (!enable_json) {
    int i;
    for (i = num - 1; i >= 0; i--) {
      print_message (ev, ML[i]);
    }
  } else {
    #ifdef USE_JSON
      json_t *res = json_array ();
      int i;
      for (i = num - 1; i >= 0; i--) {
        json_t *a = json_pack_message (ML[i]);
        assert (json_array_append (res, a) >= 0);        
      }
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}

void print_msg_list_history_gw (struct tgl_state *TLSR, void *extra, int success, int num, struct tgl_message *ML[]) {
  print_msg_list_gw (TLSR, extra, success, num, ML);
  if (num > 0) {
    if (tgl_cmp_peer_id (ML[0]->to_id, TLS->our_id)) {
      tgl_do_messages_mark_read (TLS, ML[0]->to_id, ML[0]->server_id, 0, NULL, NULL);
    } else {
      tgl_do_messages_mark_read (TLS, ML[0]->from_id, ML[0]->server_id, 0, NULL, NULL);
    }
  }
}

void print_msg_gw (struct tgl_state *TLSR, void *extra, int success, struct tgl_message *M) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  if (!enable_json) {
    print_message (ev, M);
  } else {
    #ifdef USE_JSON
      json_t *res = json_pack_message (M);
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}

void print_user_list_gw (struct tgl_state *TLSR, void *extra, int success, int num, struct tgl_user *UL[]) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  if (!enable_json) {
    int i;
    for (i = num - 1; i >= 0; i--) {
      print_user_name (ev, UL[i]->id, (void *)UL[i]);
      mprintf (ev, "\n");
    }
  } else {
    #ifdef USE_JSON
      json_t *res = json_array ();
      int i;
      for (i = num - 1; i >= 0; i--) {
        json_t *a = json_pack_peer (UL[i]->id);
        assert (json_array_append (res, a) >= 0);
      }
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}

void print_user_gw (struct tgl_state *TLSR, void *extra, int success, struct tgl_user *U) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  if (!enable_json) {
    print_user_name (ev, U->id, (void *)U);
    mprintf (ev, "\n");
  } else {
    #ifdef USE_JSON
      json_t *res = json_pack_peer (U->id);
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}

void print_chat_gw (struct tgl_state *TLSR, void *extra, int success, struct tgl_chat *U) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  if (!enable_json) {
    print_chat_name (ev, U->id, (void *)U);
    mprintf (ev, "\n");
  } else {
    #ifdef USE_JSON
      json_t *res = json_pack_peer (U->id);
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}

void print_channel_gw (struct tgl_state *TLSR, void *extra, int success, struct tgl_channel *U) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  if (!enable_json) {
    print_channel_name (ev, U->id, (void *)U);
    mprintf (ev, "\n");
  } else {
    #ifdef USE_JSON
      json_t *res = json_pack_peer (U->id);
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}


void print_peer_gw (struct tgl_state *TLSR, void *extra, int success, tgl_peer_t *U) {
  if (!success) { 
    print_user_gw (TLSR, extra, success, (void *)U);
    return;
  }
  switch (tgl_get_peer_type (U->id)) {
  case TGL_PEER_USER:
    print_user_gw (TLSR, extra, success, (void *)U);
    break;
  case TGL_PEER_CHAT:
    print_chat_gw (TLSR, extra, success, (void *)U);
    break;
  case TGL_PEER_CHANNEL:
    print_channel_gw (TLSR, extra, success, (void *)U);
    break;
  default:
    assert (0);
  }
}

void print_filename_gw (struct tgl_state *TLSR, void *extra, int success, const char *name) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  if (!enable_json) {
    mprintf (ev, "Saved to %s\n", name);
  } else {
    #ifdef USE_JSON
      json_t *res = json_object ();
      assert (json_object_set (res, "result", json_string (name)) >= 0);
      assert (json_object_set (res, "event", json_string ("download")) >= 0);
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}

void print_string_gw (struct tgl_state *TLSR, void *extra, int success, const char *name) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  if (!enable_json) {
    mprintf (ev, "%s\n", name);
  } else {
    #ifdef USE_JSON
      json_t *res = json_object ();
      assert (json_object_set (res, "result", json_string (name)) >= 0);
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}

void open_filename_gw (struct tgl_state *TLSR, void *extra, int success, const char *name) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (ev) { return; }
  if (!success) { print_fail (ev); return; }
  static char buf[PATH_MAX];
  if (snprintf (buf, sizeof (buf), OPEN_BIN, name) >= (int) sizeof (buf)) {
    logprintf ("Open image command buffer overflow\n");
  } else {
    int pid = fork ();
    if (!pid) {
      execl("/bin/sh", "sh", "-c", buf, (char *) 0);
      exit (0);
    }
  }
}

void print_chat_info_gw (struct tgl_state *TLSR, void *extra, int success, struct tgl_chat *C) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  
  if (!enable_json) {
    tgl_peer_t *U = (void *)C;
    mpush_color (ev, COLOR_YELLOW);
    mprintf (ev, "Chat ");
    print_chat_name (ev, U->id, U);
    mprintf (ev, " (id %d) members:\n", tgl_get_peer_id (U->id));
    int i;
    for (i = 0; i < C->user_list_size; i++) {
      mprintf (ev, "\t\t");
      print_user_name (ev, TGL_MK_USER (C->user_list[i].user_id), tgl_peer_get (TLS, TGL_MK_USER (C->user_list[i].user_id)));
      mprintf (ev, " invited by ");
      print_user_name (ev, TGL_MK_USER (C->user_list[i].inviter_id), tgl_peer_get (TLS, TGL_MK_USER (C->user_list[i].inviter_id)));
      mprintf (ev, " at ");
      print_date_full (ev, C->user_list[i].date);
      if (C->user_list[i].user_id == C->admin_id) {
        mprintf (ev, " admin");
      }
      mprintf (ev, "\n");
    }
    mpop_color (ev);
  } else {
    #ifdef USE_JSON
      json_t *res = json_pack_peer (C->id);
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }

  mprint_end (ev);
}

void print_channel_info_gw (struct tgl_state *TLSR, void *extra, int success, struct tgl_channel *C) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  
  if (!enable_json) {
    tgl_peer_t *U = (void *)C;
    mpush_color (ev, COLOR_YELLOW);
    mprintf (ev, "Channel ");
    if (U->flags & TGLCHF_OFFICIAL) {
      mprintf (ev, "[verified] ");
    }
    if (U->flags & TGLCHF_BROADCAST) {
      mprintf (ev, "[broadcast] ");
    }
    if (U->flags & TGLCHF_MEGAGROUP) {
      mprintf (ev, "[megagroup] ");
    }
    if (U->flags & TGLCHF_DEACTIVATED) {
      mprintf (ev, "[deactivated] ");
    }
    print_channel_name (ev, U->id, U);
    if (C->username) {
      mprintf (ev, " @%s", C->username);
    }
    mprintf (ev, " (#%d):\n", tgl_get_peer_id (U->id));
    mprintf (ev, "\tabout: %s\n", C->about);
    mprintf (ev, "\t%d participants, %d admins, %d kicked\n", C->participants_count, C->admins_count, C->kicked_count);
    mpop_color (ev);
  } else {
    #ifdef USE_JSON
      json_t *res = json_pack_peer (C->id);
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }

  mprint_end (ev);
}

void print_user_status (struct tgl_user_status *S, struct in_ev *ev) {
  assert(!enable_json); //calling functions print_user_info_gw() and user_status_upd() already check.
  if (S->online > 0) {
    mprintf (ev, "online (was online ");
    print_date_full (ev, S->when);
    mprintf (ev, ")");
  } else {
    if (S->online == 0) {
      mprintf (ev, "offline");
    } else if (S->online == -1) {
      mprintf (ev, "offline (was online ");
      print_date_full (ev, S->when);
      mprintf (ev, ")");
    } else if (S->online == -2) {
      mprintf (ev, "offline (was online recently)");
    } else if (S->online == -3) {
      mprintf (ev, "offline (was online last week)");
    } else if (S->online == -4) {
      mprintf (ev, "offline (was online last month)");
    }
  }
}

void print_user_info_gw (struct tgl_state *TLSR, void *extra, int success, struct tgl_user *U) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  tgl_peer_t *C = (void *)U;
  if (!enable_json) {
    mpush_color (ev, COLOR_YELLOW);
    mprintf (ev, "User ");
    print_user_name (ev, U->id, C);
    if (U->username) {
      mprintf (ev, " @%s", U->username);
    }
    mprintf (ev, " (#%d):\n", tgl_get_peer_id (U->id));
    mprintf (ev, "\tphone: %s\n", U->phone);
    mprintf (ev, "\t");
    print_user_status (&U->status, ev);
    mprintf (ev, "\n");

    if (U->bot_info) {
      mprintf (ev, "\tshare_text:  %s\n", U->bot_info->share_text);
      mprintf (ev, "\tdescription: %s\n", U->bot_info->description);
      mprintf (ev, "\tcommands:\n");

      int i;
      for (i = 0; i < U->bot_info->commands_num; i++) {
        mprintf (ev, "\t\t/%s: %s\n", U->bot_info->commands[i].command, U->bot_info->commands[i].description);
      }
    }
    mpop_color (ev);
  } else {
    #ifdef USE_JSON
      json_t *res = json_pack_peer (U->id);
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}

void print_secret_chat_gw (struct tgl_state *TLSR, void *extra, int success, struct tgl_secret_chat *E) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  if (!enable_json) {
    mpush_color (ev, COLOR_YELLOW);
    mprintf (ev, " Encrypted chat ");
    print_encr_chat_name (ev, E->id, (void *)E);
    mprintf (ev, " is now in wait state\n");
    mpop_color (ev);
  } else {
    #ifdef USE_JSON
      json_t *res = json_pack_peer (E->id);
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}

void print_dialog_list_gw (struct tgl_state *TLSR, void *extra, int success, int size, tgl_peer_id_t peers[], tgl_message_id_t *last_msg_id[], int unread_count[]) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  if (!enable_json)  {
    mpush_color (ev, COLOR_YELLOW);
    int i;
    for (i = size - 1; i >= 0; i--) {
      tgl_peer_t *UC;
      switch (tgl_get_peer_type (peers[i])) {
        case TGL_PEER_USER:
          UC = tgl_peer_get (TLS, peers[i]);
          mprintf (ev, "User ");
          print_user_name (ev, peers[i], UC);
          mprintf (ev, ": %d unread\n", unread_count[i]);
          break;
        case TGL_PEER_CHAT:
          UC = tgl_peer_get (TLS, peers[i]);
          mprintf (ev, "Chat ");
          print_chat_name (ev, peers[i], UC);
          mprintf (ev, ": %d unread\n", unread_count[i]);
          break;
        case TGL_PEER_CHANNEL:
          UC = tgl_peer_get (TLS, peers[i]);
          mprintf (ev, "Channel ");
          print_channel_name (ev, peers[i], UC);
          mprintf (ev, ": %d unread\n", unread_count[i]);
          break;
      }
    }
    mpop_color (ev);
  } else {
    #ifdef USE_JSON
      json_t *res = json_array ();
      int i;
      for (i = size - 1; i >= 0; i--) {
        json_t *a = json_pack_peer (peers[i]);
        assert (json_array_append (res, a) >= 0);
      }
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}

void interpreter_chat_mode (char *line) {
  if (line == NULL || /* EOF received */
          !strncmp (line, "/exit", 5) || !strncmp (line, "/quit", 5)) {
    in_chat_mode = 0;
    update_prompt ();
    return;
  }
  if (!strncmp (line, "/history", 8)) {
    int limit = 40;
    sscanf (line, "/history %99d", &limit);
    if (limit < 0 || limit > 1000) { limit = 40; }
    tgl_do_get_history (TLS, chat_mode_id, 0, limit, offline_mode, print_msg_list_gw, 0);
    return;
  }
  if (!strncmp (line, "/read", 5)) {
    tgl_do_mark_read (TLS, chat_mode_id, 0, 0);
    return;
  }
  if (strlen (line) > 0) {
    tgl_do_send_message (TLS, chat_mode_id, line, strlen (line), 0, NULL, 0, 0);
  }
}

#define MAX_UNREAD_MESSAGE_COUNT 10000
struct tgl_message *unread_message_list[MAX_UNREAD_MESSAGE_COUNT];
int unread_message_count;
struct event *unread_message_event;


void print_read_list (int num, struct tgl_message *list[]) {
  struct in_ev *ev = notify_ev;
  int i;
  mprint_start (ev);
  for (i = 0; i < num; i++) if (list[i]) {
    if (enable_json) {
      #ifdef USE_JSON
        json_t *res = json_pack_read (list[i]);
        char *s = json_dumps (res, 0);
        mprintf (ev, "%s\n", s);
        json_decref (res);
        free (s);
      #endif
    }
    tgl_peer_id_t to_id;
    if (!tgl_cmp_peer_id (list[i]->to_id, TLS->our_id)) {
      to_id = list[i]->from_id;
    } else {
      to_id = list[i]->to_id;
    }
    int j;
    int c1 = 0;
    int c2 = 0;
    for (j = i; j < num; j++) if (list[j]) {
      tgl_peer_id_t end_id;
      if (!tgl_cmp_peer_id (list[j]->to_id, TLS->our_id)) {
        end_id = list[j]->from_id;
      } else {
        end_id = list[j]->to_id;
      }
      if (!tgl_cmp_peer_id (to_id, end_id)) {
        if (list[j]->flags & TGLMF_OUT) {
          c1 ++;
        } else {
          c2 ++;
        }
        list[j] = 0;
      }
    }

    assert (c1 + c2 > 0);
    if (!enable_json)  {
      mpush_color (ev, COLOR_YELLOW);
      switch (tgl_get_peer_type (to_id)) {
      case TGL_PEER_USER:
        mprintf (ev, "User ");
        print_user_name (ev, to_id, tgl_peer_get (TLS, to_id));
        break;
      case TGL_PEER_CHAT:
        mprintf (ev, "Chat ");
        print_chat_name (ev, to_id, tgl_peer_get (TLS, to_id));
        break;
      case TGL_PEER_ENCR_CHAT:
        mprintf (ev, "Secret chat ");
        print_encr_chat_name (ev, to_id, tgl_peer_get (TLS, to_id));
        break;
      default:
        assert (0);
      }
      mprintf (ev, " marked read %d outbox and %d inbox messages\n", c1, c2);
      mpop_color (ev);
    }
  }
  mprint_end (ev);
}

void unread_message_alarm (evutil_socket_t fd, short what, void *arg) {
  print_read_list (unread_message_count, unread_message_list);
  unread_message_count = 0;
  event_free (unread_message_event);
  unread_message_event = 0;
}

void mark_read_upd (struct tgl_state *TLSR, int num, struct tgl_message *list[]) {
  assert (TLSR == TLS);
  if (!binlog_read) { return; }
  if (log_level < 1) { return; }

  if (unread_message_count + num <= MAX_UNREAD_MESSAGE_COUNT) {
    memcpy (unread_message_list + unread_message_count, list, num * sizeof (void *));
    unread_message_count += num;

    if (!unread_message_event) {
      unread_message_event = evtimer_new (TLS->ev_base, unread_message_alarm, 0);
      static struct timeval ptimeout = { 1, 0};
      event_add (unread_message_event, &ptimeout);
    }
  } else {
    print_read_list (unread_message_count, unread_message_list);
    print_read_list (num, list);
    unread_message_count = 0;
    if (unread_message_event) {
      event_free (unread_message_event);
      unread_message_event = 0;
    }
  }
}

void print_typing (struct in_ev *ev, enum tgl_typing_status status) {
  switch (status) {
  case tgl_typing_none:
    mprintf (ev, "doing nothing");
    break;
  case tgl_typing_typing:
    mprintf (ev, "typing");
    break;
  case tgl_typing_cancel:
    mprintf (ev, "deleting typed message");
    break;
  case tgl_typing_record_video:
    mprintf (ev, "recording video");
    break;
  case tgl_typing_upload_video:
    mprintf (ev, "uploading video");
    break;
  case tgl_typing_record_audio:
    mprintf (ev, "recording audio");
    break;
  case tgl_typing_upload_audio:
    mprintf (ev, "uploading audio");
    break;
  case tgl_typing_upload_photo:
    mprintf (ev, "uploading photo");
    break;
  case tgl_typing_upload_document:
    mprintf (ev, "uploading document");
    break;
  case tgl_typing_geo:
    mprintf (ev, "choosing location");
    break;
  case tgl_typing_choose_contact:
    mprintf (ev, "choosing contact");
    break;
  }
}

void type_notification_upd (struct tgl_state *TLSR, struct tgl_user *U, enum tgl_typing_status status) {
  assert (TLSR == TLS);
  if (log_level < 2 || (disable_output && !notify_ev)) { return; }
  if (enable_json) { return; }
  struct in_ev *ev = notify_ev;
  mprint_start (ev);
  mpush_color (ev, COLOR_YELLOW);
  mprintf (ev, "User ");
  print_user_name (ev, U->id, (void *)U);
  mprintf (ev, " is ");
  print_typing (ev, status);
  mprintf (ev, "\n");
  mpop_color (ev);
  mprint_end (ev);
}

void type_in_chat_notification_upd (struct tgl_state *TLSR, struct tgl_user *U, struct tgl_chat *C, enum tgl_typing_status status) {
  assert (TLSR == TLS);
  if (log_level < 2 || (disable_output && !notify_ev)) { return; }
  if (enable_json) { return; }
  struct in_ev *ev = notify_ev;
  mprint_start (ev);
  mpush_color (ev, COLOR_YELLOW);
  mprintf (ev, "User ");
  print_user_name (ev, U->id, (void *)U);
  mprintf (ev, " is ");
  print_typing (ev, status);
  mprintf (ev, " in chat ");
  print_chat_name (ev, C->id, (void *)C);
  mprintf (ev, "\n");
  mpop_color (ev);
  mprint_end (ev);
}


void print_message_gw (struct tgl_state *TLSR, struct tgl_message *M) {
  assert (TLSR == TLS);
  #ifdef USE_LUA
    lua_new_msg (M);
  #endif
  #ifdef USE_PYTHON
    py_new_msg (M);
  #endif
  if (!binlog_read) { return; }
  if (tgl_get_peer_type (M->to_id) == TGL_PEER_ENCR_CHAT) { 
    write_secret_chat_file ();
  }
  if (alert_sound) {
    play_sound ();
  }
  if (disable_output && !notify_ev) { return; }
  struct in_ev *ev = notify_ev;
  mprint_start (ev);
  if (!enable_json) {
    print_message (ev, M);
  } else {
    #ifdef USE_JSON
      json_t *res = json_pack_message (M);
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}

void our_id_gw (struct tgl_state *TLSR, tgl_peer_id_t id) {
  assert (TLSR == TLS);
  #ifdef USE_LUA
    lua_our_id (id);
  #endif
  #ifdef USE_PYTHON
    py_our_id (id);
  #endif
}

void print_peer_updates (struct in_ev *ev, int flags) {
  if (flags & TGL_UPDATE_PHONE) {
    mprintf (ev, " phone");
  }
  if (flags & TGL_UPDATE_CONTACT) {
    mprintf (ev, " contact");
  }
  if (flags & TGL_UPDATE_PHOTO) {
    mprintf (ev, " photo");
  }
  if (flags & TGL_UPDATE_BLOCKED) {
    mprintf (ev, " blocked");
  }
  if (flags & TGL_UPDATE_REAL_NAME) {
    mprintf (ev, " name");
  }
  if (flags & TGL_UPDATE_NAME) {
    mprintf (ev, " contact_name");
  }
  if (flags & TGL_UPDATE_REQUESTED) {
    mprintf (ev, " status");
  }
  if (flags & TGL_UPDATE_WORKING) {
    mprintf (ev, " status");
  }
  if (flags & TGL_UPDATE_FLAGS) {
    mprintf (ev, " flags");
  }
  if (flags & TGL_UPDATE_TITLE) {
    mprintf (ev, " title");
  }
  if (flags & TGL_UPDATE_ADMIN) {
    mprintf (ev, " admin");
  }
  if (flags & TGL_UPDATE_MEMBERS) {
    mprintf (ev, " members");
  }
  if (flags & TGL_UPDATE_ACCESS_HASH) {
    mprintf (ev, " access_hash");
  }
  if (flags & TGL_UPDATE_USERNAME) {
    mprintf (ev, " username");
  }
}

void json_peer_update (struct in_ev *ev, tgl_peer_t *P, unsigned flags) {
  #ifdef USE_JSON
    json_t *res = json_object ();
    assert (json_object_set (res, "event", json_string ("updates")) >= 0);
    assert (json_object_set (res, "peer", json_pack_peer (P->id)) >= 0);
    assert (json_object_set (res, "updates", json_pack_updates (flags)) >= 0);
    char *s = json_dumps (res, 0);
    mprintf (ev, "%s\n", s);
    json_decref (res);
    free (s);
  #endif
}

void peer_update_username (tgl_peer_t *P, const char *username) {
  if (!username) {
    if (P->extra) {
      struct username_peer_pair *p = tree_lookup_username_peer_pair (username_peer_pair, (void *)&P->extra);      
      assert (p);
      username_peer_pair = tree_delete_username_peer_pair (username_peer_pair, p);
      tfree_str (P->extra);
      tfree (p, sizeof (*p));
      P->extra = NULL;
    }
    return;
  }
  assert (username);
  if (P->extra && !strcmp (P->extra, username)) {
    return;
  }
  if (P->extra) {
    struct username_peer_pair *p = tree_lookup_username_peer_pair (username_peer_pair, (void *)&P->extra);
    assert (p);
    username_peer_pair = tree_delete_username_peer_pair (username_peer_pair, p);
    tfree_str (P->extra);
    tfree (p, sizeof (*p));
    P->extra = NULL;
  }

  P->extra = tstrdup (username);
  struct username_peer_pair *p = talloc (sizeof (*p));
  p->peer = P;
  p->username = P->extra;
  
  username_peer_pair = tree_insert_username_peer_pair (username_peer_pair, p, rand ());
}

void user_update_gw (struct tgl_state *TLSR, struct tgl_user *U, unsigned flags) {
  assert (TLSR == TLS);
  #ifdef USE_LUA
    lua_user_update (U, flags);
  #endif
  #ifdef USE_PYTHON
    py_user_update (U, flags);
  #endif

  peer_update_username ((void *)U, U->username);
 
  if (disable_output && !notify_ev) { return; }
  if (!binlog_read) { return; }
  struct in_ev *ev = notify_ev;

  if (!(flags & TGL_UPDATE_CREATED)) {
    mprint_start (ev);
    if (!enable_json) {
      mpush_color (ev, COLOR_YELLOW);
      mprintf (ev, "User ");
      print_user_name (ev, U->id, (void *)U);
      if (!(flags & TGL_UPDATE_DELETED)) {
        mprintf (ev, " updated");
        print_peer_updates (ev, flags);
      } else {
        mprintf (ev, " deleted");
      }
      mprintf (ev, "\n");
      mpop_color (ev);
    } else {
      json_peer_update (ev, (void *)U, flags);
    }
    mprint_end (ev);
  }
}

void chat_update_gw (struct tgl_state *TLSR, struct tgl_chat *U, unsigned flags) {
  assert (TLSR == TLS);
  #ifdef USE_LUA
    lua_chat_update (U, flags);
  #endif
  #ifdef USE_PYTHON
    py_chat_update (U, flags);
  #endif
 
  if (disable_output && !notify_ev) { return; }
  if (!binlog_read) { return; }
  struct in_ev *ev = notify_ev;

  if (!(flags & TGL_UPDATE_CREATED)) {
    mprint_start (ev);
    if (!enable_json) {
      mpush_color (ev, COLOR_YELLOW);
      mprintf (ev, "Chat ");
      print_chat_name (ev, U->id, (void *)U);
      if (!(flags & TGL_UPDATE_DELETED)) {
        mprintf (ev, " updated");
        print_peer_updates (ev, flags);
      } else {
        mprintf (ev, " deleted");
      }
      mprintf (ev, "\n");
      mpop_color (ev);
    } else {
      json_peer_update (ev, (void *)U, flags);
    }
    mprint_end (ev);
  }
}

void secret_chat_update_gw (struct tgl_state *TLSR, struct tgl_secret_chat *U, unsigned flags) {
  assert (TLSR == TLS);
  #ifdef USE_LUA
    lua_secret_chat_update (U, flags);
  #endif
  #ifdef USE_PYTHON
    py_secret_chat_update (U, flags);
  #endif

  if ((flags & TGL_UPDATE_WORKING) || (flags & TGL_UPDATE_DELETED)) {
    write_secret_chat_file ();
  }
  
  if (!binlog_read) { return; }

  if ((flags & TGL_UPDATE_REQUESTED) && !disable_auto_accept)  {
    //tgl_do_accept_encr_chat_request (TLS, U, 0, 0);
    tgl_do_accept_encr_chat_request (TLS, U, print_encr_chat_success_gw, 0);
  }
  
  if (disable_output && !notify_ev) { return; }
  struct in_ev *ev = notify_ev;


  if (!(flags & TGL_UPDATE_CREATED)) {
    mprint_start (ev);
    if (!enable_json) {
      mpush_color (ev, COLOR_YELLOW);
      mprintf (ev, "Secret chat ");
      print_encr_chat_name (ev, U->id, (void *)U);
      if (!(flags & TGL_UPDATE_DELETED)) {
        mprintf (ev, " updated");
        print_peer_updates (ev, flags);
      } else {
        mprintf (ev, " deleted");
      }
      mprintf (ev, "\n");
      mpop_color (ev);
    } else {
      json_peer_update (ev, (void *)U, flags);
    }
    mprint_end (ev);
  }
}

void channel_update_gw (struct tgl_state *TLSR, struct tgl_channel *U, unsigned flags) {
  assert (TLSR == TLS);
  
  peer_update_username ((void *)U, U->username);
 
  if (disable_output && !notify_ev) { return; }
  if (!binlog_read) { return; }
}

void print_card_gw (struct tgl_state *TLSR, void *extra, int success, int size, int *card) {
  assert (TLSR == TLS);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  if (!enable_json) {
    mprintf (ev, "Card: ");
    int i;
    for (i = 0; i < size; i++) {
      mprintf (ev, "%08x%c", card[i], i == size - 1 ? '\n' : ':');
    }
  } else {
    #ifdef USE_JSON
    static char q[1000];
    int pos = 0;
    int i;
    for (i = 0; i < size; i++) {
      pos += sprintf (q + pos, "%08x%s", card[i], i == size - 1 ? "" : ":");
    }
    json_t *res = json_object ();
    assert (json_object_set (res, "result", json_string (q)) >= 0);
    char *s = json_dumps (res, 0);
    mprintf (ev, "%s\n", s);
    json_decref (res);
    free (s);
    #endif
  }
  mprint_end (ev);
}

void callback_extf (struct tgl_state *TLS, void *extra, int success, const char *buf) {
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  if (!enable_json) {
    mprintf (ev, "%s\n", buf);
  } else {
    #ifdef USE_JSON
    json_t *res = json_object ();
    assert (json_object_set (res, "result", json_string (buf)) >= 0);
    char *s = json_dumps (res, 0);
    mprintf (ev, "%s\n", s);
    json_decref (res);
    free (s);
    #endif
  }
  mprint_end (ev);
}

void user_status_upd (struct tgl_state *TLS, struct tgl_user *U) {
  if (disable_output && !notify_ev) { return; }
  if (!binlog_read) { return; }
  if (log_level < 3) { return; }
  struct in_ev *ev = notify_ev;
  mprint_start (ev);
  if (!enable_json)
  {
    mpush_color (ev, COLOR_YELLOW);
    mprintf (ev, "User ");
    print_user_name(ev, U->id, (void *) U);
    mprintf (ev, " ");
    print_user_status(&U->status, ev);
    mprintf (ev, "\n");
    mpop_color (ev);
  } else {
    #ifdef USE_JSON
      json_t *res = json_pack_user_status(U);
      char *s = json_dumps (res, 0);
      mprintf (ev, "%s\n", s);
      json_decref (res);
      free (s);
    #endif
  }
  mprint_end (ev);
}

void on_login (struct tgl_state *TLS);
void on_failed_login (struct tgl_state *TLS);
void on_started (struct tgl_state *TLS);
void do_get_values (struct tgl_state *TLS, enum tgl_value_type type, const char *prompt, int num_values,
          void (*callback)(struct tgl_state *TLS, const char *string[], void *arg), void *arg);

struct tgl_update_callback upd_cb = {
  .new_msg = print_message_gw,
  .marked_read = mark_read_upd,
  .logprintf = logprintf,
  .get_values = do_get_values,
  .logged_in = on_login,
  .started = on_started,
  .type_notification = type_notification_upd,
  .type_in_chat_notification = type_in_chat_notification_upd,
  .type_in_secret_chat_notification = 0,
  .status_notification = 0,
  .user_registered = 0,
  .user_activated = 0,
  .new_authorization = 0,
  .user_update = user_update_gw,
  .chat_update = chat_update_gw,
  .secret_chat_update = secret_chat_update_gw,
  .channel_update = channel_update_gw,
  .msg_receive = print_message_gw,
  .our_id = our_id_gw,
  .user_status_update = user_status_upd,
  .on_failed_login = on_failed_login
};


void interpreter_ex (char *line, void *ex) {  
  force_end_mode = 1;
  assert (!in_readline);
  in_readline = 1;
  if (in_chat_mode) {
    interpreter_chat_mode (line);
    in_readline = 0;
    return;
  }

  do_html = 0;
  line_ptr = line;
  offline_mode = 0;
  reply_id = 0;
  disable_msg_preview = 0;
  count = 1;
  if (!line) { 
    do_safe_quit (NULL, 0, NULL, NULL);
    in_readline = 0;
    return; 
  }
  if (!*line) {
    in_readline = 0;
    return;
  }

  if (line && *line) {
    add_history (line);
  }
  
  if (*line == '(') { 
    struct in_ev *ev = ex;
    if (ev) { ev->refcnt ++; }
    tgl_do_send_extf (TLS, line, strlen (line), callback_extf, ev);
    in_readline = 0;
    return; 
  }

  while (1) {
    next_token ();
    if (cur_token_quoted) { 
      in_readline = 0;
      fail_interface (TLS, ex, ENOSYS, "can not parse modifier");
      return; 
    }

    if (cur_token_len <= 0) { 
      in_readline = 0;
      fail_interface (TLS, ex, ENOSYS, "can not parse modifier");
      return; 
    }
    
    if (*cur_token == '[') {
      if (cur_token_end_str) {
        in_readline = 0;
        fail_interface (TLS, ex, ENOSYS, "can not parse modifier");
        return; 
      }
      if (cur_token[cur_token_len - 1] != ']') {
        in_readline = 0;
        fail_interface (TLS, ex, ENOSYS, "can not parse modifier");
        return; 
      }
      work_modifier (cur_token, cur_token_len);
      continue;
    }
    break;
  }
  if (cur_token_quoted || cur_token_end_str) { 
    fail_interface (TLS, ex, ENOSYS, "can not parse command name");
    in_readline = 0;
    return; 
  }
    
    
  
  struct command *command = commands;
  int n = 0;
  struct tgl_command;
  while (command->name) {
    if (is_same_word (cur_token, cur_token_len, command->name)) {
      break;
    }
    n ++;
    command ++;
  }
  
  if (!command->name) {
    fail_interface (TLS, ex, ENOSYS, "can not find command '%.*s'", cur_token_len, cur_token);
    in_readline = 0;
    return; 
  }

  enum command_argument *flags = command->args;
  void (*fun)(struct command *, int, struct arg[], struct in_ev *) = command->fun;
  int args_num = 0;
  static struct arg args[1000];
  while (1) {
    assert (args_num < 1000);
    args[args_num].flags = 0;
    int period = 0;
    if (*flags == ca_period) {
      flags --;
    }
    if (*flags != ca_none && *(flags + 1) == ca_period) {
      period = 1;
    }
    enum command_argument op = (*flags) & 255;
    int opt = (*flags) & ca_optional;

    if (op == ca_none) { 
      next_token ();
      if (cur_token_end_str) {
        int z;
        for (z = 0; z < count; z ++) {
          fun (command, args_num, args, ex);
        }
      } else {
        fail_interface (TLS, ex, ENOSYS, "too many args #%d", args_num);
      }
      break;
    }
      
    if (op == ca_string_end || op == ca_file_name_end || op == ca_msg_string_end) {
      next_token_end ();
      if (cur_token_len < 0) { 
        fail_interface (TLS, ex, ENOSYS, "can not parse string_end arg #%d", args_num);
        break;
      } else {
        args[args_num].flags = 1;
        args[args_num ++].str = strndup (cur_token, cur_token_len);
        int z;
        for (z = 0; z < count; z ++) {
          fun (command, args_num, args, ex);
        }
        break;
      }
    }

    char *save = line_ptr;
    next_token ();

    if (period && cur_token_end_str) {
      int z;
      for (z = 0; z < count; z ++) {
        fun (command, args_num, args, ex);
      }
      break;
    }

    if (op == ca_user || op == ca_chat || op == ca_secret_chat || op == ca_peer || op == ca_number || op == ca_double || op == ca_msg_id || op == ca_channel) {
      if (cur_token_quoted) {
        if (opt) {
          if (op != ca_number && op != ca_double && op != ca_msg_id) {
            args[args_num ++].peer_id = TGL_PEER_NOT_FOUND;
          } else {
            if (op == ca_number) {
              args[args_num ++].num = NOT_FOUND;
            } else if (op == ca_msg_id) {
              args[args_num ++].msg_id.peer_type = 0;
            } else {
              args[args_num ++].dval = NOT_FOUND;
            }
          }
          line_ptr = save;
          flags ++;
          continue;
        } else if (period) {
          line_ptr = save;
          flags += 2;
          continue;
        } else {
          break;
        }
      } else {
        if (cur_token_end_str) { 
          if (opt) {
            if (op != ca_number && op != ca_double && op != ca_msg_id) {
              args[args_num ++].peer_id = TGL_PEER_NOT_FOUND;
            } else {
              if (op == ca_number) {
                args[args_num ++].num = NOT_FOUND;
              } else if (op == ca_msg_id) {
                args[args_num ++].msg_id.peer_type = 0;
              } else {
                args[args_num ++].dval = NOT_FOUND;
              }
            }
            line_ptr = save;
            flags ++;
            continue;
          } else if (period) {
            line_ptr = save;
            flags += 2;
            continue;
          } else {
            break;
          }
        }
        int ok = 1;
        switch (op) {
        case ca_user:
          args[args_num ++].peer_id = cur_token_user (); 
          ok = tgl_get_peer_id (args[args_num - 1].peer_id) != NOT_FOUND;
          break;
        case ca_chat:
          args[args_num ++].peer_id = cur_token_chat (); 
          ok = tgl_get_peer_id (args[args_num - 1].peer_id) != NOT_FOUND;
          break;
        case ca_secret_chat:
          args[args_num ++].peer_id = cur_token_encr_chat (); 
          ok = tgl_get_peer_id (args[args_num - 1].peer_id) != NOT_FOUND;
          break;
        case ca_channel:
          args[args_num ++].peer_id = cur_token_channel (); 
          ok = tgl_get_peer_id (args[args_num - 1].peer_id) != NOT_FOUND;
          break;
        case ca_peer:
          args[args_num ++].peer_id = cur_token_peer (); 
          ok = tgl_get_peer_id (args[args_num - 1].peer_id) != NOT_FOUND;
          break;
        case ca_number:
          args[args_num ++].num = cur_token_int ();
          ok = (args[args_num - 1].num != NOT_FOUND);
          break;
        case ca_msg_id:
          args[args_num ++].msg_id = cur_token_msg_id ();
          ok = (args[args_num - 1].msg_id.peer_type != 0);
          break;
        case ca_double:
          args[args_num ++].dval = cur_token_double ();
          ok = (args[args_num - 1].dval != NOT_FOUND);
          break;
        default:
          assert (0);
        }

        if (period && !ok) {
          line_ptr = save;
          flags += 2;
          args_num --;
          continue;
        }
        if (opt && !ok) {
          line_ptr = save;
          flags ++;
          continue;
        }
        if (!ok) {
          fail_interface (TLS, ex, ENOSYS, "can not parse arg #%d", args_num);
          break;
        }

        flags ++;
        continue;
      }
    }
    if (op == ca_string || op == ca_file_name || op == ca_command) {
      if (cur_token_end_str || cur_token_len < 0) {
        if (opt) {
          args[args_num ++].str = NULL;
          flags ++;
          continue;
        }
        fail_interface (TLS, ex, ENOSYS, "can not parse string arg #%d", args_num);
        break;
      } else {
        args[args_num].flags = 1;
        args[args_num ++].str = strndup (cur_token, cur_token_len);
        flags ++;
        continue;
      }
    }
    //assert (0);
  }
  int i;
  for (i = 0; i < args_num; i++) {
    if (args[i].flags & 1) {
      free (args[i].str);
    }
  }
  
  update_prompt ();
  in_readline = 0;
}

void interpreter (char *line) {
  interpreter_ex (line, 0);
}

int readline_active;
/*void rprintf (const char *format, ...) {
  mprint_start (ev);
  va_list ap;
  va_start (ap, format);
  vfprintf (stdout, format, ap);
  va_end (ap);
  print_end();
}*/

int saved_point;
char *saved_line;
static int prompt_was;


void deactivate_readline (void) {
  if (read_one_string) {
    printf ("\033[2K\r");
    fflush (stdout);
  } else {
    saved_point = rl_point;
    saved_line = malloc (rl_end + 1);
    assert (saved_line);
    saved_line[rl_end] = 0;
    memcpy (saved_line, rl_line_buffer, rl_end);

    rl_save_prompt();
    rl_replace_line("", 0);
    rl_redisplay();
  }
}


void reactivate_readline (void) {
  if (read_one_string) {
    printf ("%s ", one_string_prompt);
    if (!(one_string_flags & 1)) {
      printf ("%.*s", one_string_len, one_string);
    }
    fflush (stdout);
  } else {
    set_prompt (get_default_prompt ());
    rl_replace_line(saved_line, 0);
    rl_point = saved_point;
    rl_redisplay();
    free (saved_line);
  }
}

void print_start (void) {
  if (in_readline) { return; }
  if (readline_disabled) { return; }
  assert (!prompt_was);
  if (readline_active) {
    deactivate_readline ();
  }
  prompt_was = 1;
}

void print_end (void) {
  if (in_readline) { return; }
  if (readline_disabled) { 
    fflush (stdout);
    return; 
  }
  assert (prompt_was);
  if (readline_active) {
    reactivate_readline ();
  }
  prompt_was = 0;
}

/*void hexdump (int *in_ptr, int *in_end) {
  mprint_start (ev);
  int *ptr = in_ptr;
  while (ptr < in_end) { mprintf (ev, " %08x", *(ptr ++)); }
  mprintf (ev, "\n");
  mprint_end (ev); 
}*/

void logprintf (const char *format, ...) {
  int x = 0;
  if (!prompt_was) {
    x = 1;
    print_start ();
  }
  if (!disable_colors) {
    printf (COLOR_GREY);
  }
  printf (" *** ");


  double T = tglt_get_double_time ();
  printf ("%.6lf ", T);

  va_list ap;
  va_start (ap, format);
  vfprintf (stdout, format, ap);
  va_end (ap);
  if (!disable_colors) {
    printf (COLOR_NORMAL);
  }
  if (x) {
    print_end ();
  }
}

int color_stack_pos;
const char *color_stack[10];

void push_color (const char *color) {
  if (disable_colors) { return; }
  assert (color_stack_pos < 10);
  color_stack[color_stack_pos ++] = color;
  printf ("%s", color);
}

void pop_color (void) {
  if (disable_colors) { return; }
  assert (color_stack_pos > 0);
  color_stack_pos --;
  if (color_stack_pos >= 1) {
    printf ("%s", color_stack[color_stack_pos - 1]);
  } else {
    printf ("%s", COLOR_NORMAL);
  }
}

void print_media (struct in_ev *ev, struct tgl_message_media *M) {
  assert (M);
  switch (M->type) {
    case tgl_message_media_none:
      return;
    case tgl_message_media_photo:
      if (!M->photo) {
        mprintf (ev, "[photo bad]");
      } else if (M->photo->caption && strlen (M->photo->caption)) {
        mprintf (ev, "[photo %s]", M->photo->caption);
      } else {
        mprintf (ev, "[photo]");
      }
      if (M->caption) {
        mprintf (ev, " %s", M->caption);
      }
      return;
    case tgl_message_media_document:
    case tgl_message_media_audio:
    case tgl_message_media_video:
      mprintf (ev, "[");
      assert (M->document);
      if (M->document->flags & TGLDF_IMAGE) {
        mprintf (ev, "image");
      } else if (M->document->flags & TGLDF_AUDIO) {
        mprintf (ev, "audio");
      } else if (M->document->flags & TGLDF_VIDEO) {
        mprintf (ev, "video");
      } else if (M->document->flags & TGLDF_STICKER) {
        mprintf (ev, "sticker");
      } else {
        mprintf (ev, "document");
      }

      if (M->document->caption && strlen (M->document->caption)) {
        mprintf (ev, " %s:", M->document->caption);
      } else {
        mprintf (ev, ":");
      }
      
      if (M->document->mime_type) {
        mprintf (ev, " type=%s", M->document->mime_type);
      }

      if (M->document->w && M->document->h) {
        mprintf (ev, " size=%dx%d", M->document->w, M->document->h);
      }

      if (M->document->duration) {
        mprintf (ev, " duration=%d", M->document->duration);
      }
      
      mprintf (ev, " size=");
      if (M->document->size < (1 << 10)) {
        mprintf (ev, "%dB", M->document->size);
      } else if (M->document->size < (1 << 20)) {
        mprintf (ev, "%dKiB", M->document->size >> 10);
      } else if (M->document->size < (1 << 30)) {
        mprintf (ev, "%dMiB", M->document->size >> 20);
      } else {
        mprintf (ev, "%dGiB", M->document->size >> 30);
      }
      
      mprintf (ev, "]");
      
      if (M->caption) {
        mprintf (ev, " %s", M->caption);
      }

      return;
    case tgl_message_media_document_encr:
      mprintf (ev, "[");
      if (M->encr_document->flags & TGLDF_IMAGE) {
        mprintf (ev, "image");
      } else if (M->encr_document->flags & TGLDF_AUDIO) {
        mprintf (ev, "audio");
      } else if (M->encr_document->flags & TGLDF_VIDEO) {
        mprintf (ev, "video");
      } else if (M->encr_document->flags & TGLDF_STICKER) {
        mprintf (ev, "sticker");
      } else {
        mprintf (ev, "document");
      }

      if (M->encr_document->caption && strlen (M->encr_document->caption)) {
        mprintf (ev, " %s:", M->encr_document->caption);
      } else {
        mprintf (ev, ":");
      }
      
      if (M->encr_document->mime_type) {
        mprintf (ev, " type=%s", M->encr_document->mime_type);
      }

      if (M->encr_document->w && M->encr_document->h) {
        mprintf (ev, " size=%dx%d", M->encr_document->w, M->encr_document->h);
      }

      if (M->encr_document->duration) {
        mprintf (ev, " duration=%d", M->encr_document->duration);
      }
      
      mprintf (ev, " size=");
      if (M->encr_document->size < (1 << 10)) {
        mprintf (ev, "%dB", M->encr_document->size);
      } else if (M->encr_document->size < (1 << 20)) {
        mprintf (ev, "%dKiB", M->encr_document->size >> 10);
      } else if (M->encr_document->size < (1 << 30)) {
        mprintf (ev, "%dMiB", M->encr_document->size >> 20);
      } else {
        mprintf (ev, "%dGiB", M->encr_document->size >> 30);
      }
      
      mprintf (ev, "]");

      return;
    case tgl_message_media_geo:
      mprintf (ev, "[geo https://maps.google.com/?q=%.6lf,%.6lf]", M->geo.latitude, M->geo.longitude);
      return;
    case tgl_message_media_contact:
      mprintf (ev, "[contact] ");
      mpush_color (ev, COLOR_RED);
      mprintf (ev, "%s %s ", M->first_name, M->last_name);
      mpop_color (ev);
      mprintf (ev, "%s", M->phone);
      return;
    case tgl_message_media_unsupported:
      mprintf (ev, "[unsupported]");
      return;
    case tgl_message_media_webpage:
      mprintf (ev, "[webpage:");
      assert (M->webpage);
      if (M->webpage->url) {
        mprintf (ev, " url:'%s'", M->webpage->url);
      }
      if (M->webpage->title) {
        mprintf (ev, " title:'%s'", M->webpage->title);
      }
      if (M->webpage->description) {
        mprintf (ev, " description:'%s'", M->webpage->description);
      }
      if (M->webpage->author) {
        mprintf (ev, " author:'%s'", M->webpage->author);
      }
      mprintf (ev, "]");
      break;
    case tgl_message_media_venue:
      mprintf (ev, "[geo https://maps.google.com/?q=%.6lf,%.6lf", M->venue.geo.latitude, M->venue.geo.longitude);
      
      if (M->venue.title) {
        mprintf (ev, " title:'%s'", M->venue.title);
      }
      
      if (M->venue.address) {
        mprintf (ev, " address:'%s'", M->venue.address);
      }
      if (M->venue.provider) {
        mprintf (ev, " provider:'%s'", M->venue.provider);
      }
      if (M->venue.venue_id) {
        mprintf (ev, " id:'%s'", M->venue.venue_id);
      }

      mprintf (ev, "]");
      return;
      
    default:
      mprintf (ev, "x = %d\n", M->type);
      assert (0);
  }
}

int unknown_user_list_pos;
int unknown_user_list[1000];

void print_peer_permanent_name (struct in_ev *ev, tgl_peer_id_t id) {
  mprintf (ev, "%s", print_permanent_peer_id (id));
}

void print_user_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *U) {
  assert (tgl_get_peer_type (id) == TGL_PEER_USER);
  mpush_color (ev, COLOR_RED);
  if (permanent_peer_id_mode) {
    print_peer_permanent_name (ev, id);
    mpop_color (ev);
    return;
  }
  if (!U) {
    mprintf (ev, "user#%d", tgl_get_peer_id (id));
    int i;
    int ok = 1;
    for (i = 0; i < unknown_user_list_pos; i++) {
      if (unknown_user_list[i] == tgl_get_peer_id (id)) {
        ok = 0;
        break;
      }
    }
    if (ok) {
      assert (unknown_user_list_pos < 1000);
      unknown_user_list[unknown_user_list_pos ++] = tgl_get_peer_id (id);
    }
  } else {
    if (U->flags & (TGLUF_SELF | TGLUF_CONTACT)) {
      mpush_color (ev, COLOR_REDB);
    }
    if ((U->flags & TGLUF_DELETED)) {
      mprintf (ev, "deleted user#%d", tgl_get_peer_id (id));
    } else if (!(U->flags & TGLUF_CREATED)) {
      mprintf (ev, "user#%d", tgl_get_peer_id (id));
    } else if (use_ids) {
      mprintf (ev, "user#%d", tgl_get_peer_id (id));
    } else if (!U->user.first_name || !strlen (U->user.first_name)) {
      mprintf (ev, "%s", U->user.last_name);
    } else if (!U->user.last_name || !strlen (U->user.last_name)) {
      mprintf (ev, "%s", U->user.first_name);
    } else {
      mprintf (ev, "%s %s", U->user.first_name, U->user.last_name); 
    }
    if (U->flags & (TGLUF_SELF | TGLUF_CONTACT)) {
      mpop_color (ev);
    }
  }
  mpop_color (ev);
}

void print_chat_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C) {
  assert (tgl_get_peer_type (id) == TGL_PEER_CHAT);
  mpush_color (ev, COLOR_MAGENTA);
  if (permanent_peer_id_mode) {
    print_peer_permanent_name (ev, id);
    mpop_color (ev);
    return;
  }
  if (!C || use_ids) {
    mprintf (ev, "chat#%d", tgl_get_peer_id (id));
  } else {
    mprintf (ev, "%s", C->chat.title);
  }
  mpop_color (ev);
}

void print_channel_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C) {
  assert (tgl_get_peer_type (id) == TGL_PEER_CHANNEL);
  mpush_color (ev, COLOR_CYAN);
  if (permanent_peer_id_mode) {
    print_peer_permanent_name (ev, id);
    mpop_color (ev);
    return;
  }
  if (!C || use_ids) {
    mprintf (ev, "channel#%d", tgl_get_peer_id (id));
  } else {
    mprintf (ev, "%s", C->channel.title);
  }
  mpop_color (ev);
}

void print_encr_chat_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C) {
  assert (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT);
  mpush_color (ev, COLOR_MAGENTA);
  if (permanent_peer_id_mode) {
    print_peer_permanent_name (ev, id);
    mpop_color (ev);
    return;
  }
  if (!C || use_ids) {
    mprintf (ev, "encr_chat#%d", tgl_get_peer_id (id));
  } else {
    mprintf (ev, "%s", C->print_name);
  }
  mpop_color (ev);
}

void print_peer_name  (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C) {
  switch (tgl_get_peer_type (id)) {
  case TGL_PEER_USER:
    print_user_name (ev, id, C);
    return;
  case TGL_PEER_CHAT:
    print_chat_name (ev, id, C);
    return;
  case TGL_PEER_CHANNEL:
    print_channel_name (ev, id, C);
    return;
  case TGL_PEER_ENCR_CHAT:
    print_encr_chat_name (ev, id, C);
    return;
  default:
    assert (0);
  }
}

static char *monthes[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
void print_date (struct in_ev *ev, long t) {
  struct tm *tm = localtime ((void *)&t);
  if (time (0) - t < 12 * 60 * 60) {
    mprintf (ev, "[%02d:%02d] ", tm->tm_hour, tm->tm_min);
  } else if (time (0) - t < 24 * 60 * 60 * 180) {
    mprintf (ev, "[%02d %s]", tm->tm_mday, monthes[tm->tm_mon]);
  } else {
    mprintf (ev, "[%02d %s %d]", tm->tm_mday, monthes[tm->tm_mon], tm->tm_year + 1900);
  }
}

void print_date_full (struct in_ev *ev, long t) {
  struct tm *tm = localtime ((void *)&t);
  mprintf (ev, "[%04d/%02d/%02d %02d:%02d:%02d]", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

void print_msg_id (struct in_ev *ev, tgl_message_id_t msg_id, struct tgl_message *M) {
  if (msg_num_mode) {
    if (!permanent_msg_id_mode) {
      if (M) {
        mprintf (ev, "%d", M->temp_id);
      } else {
        mprintf (ev, "???");
      }
    } else {
      mprintf (ev, "%s", print_permanent_msg_id (msg_id));
    }
  }
}

void print_service_message (struct in_ev *ev, struct tgl_message *M) {
  assert (M);
  //print_start ();
  mpush_color (ev, COLOR_GREY);

  if (tgl_get_peer_type (M->to_id) == TGL_PEER_CHANNEL) {
    mpush_color (ev, COLOR_CYAN);
  } else {
    mpush_color (ev, COLOR_MAGENTA);
  }
  print_msg_id (ev, M->permanent_id, M);
  mprintf (ev, " ");
  print_date (ev, M->date);
  mpop_color (ev);
  mprintf (ev, " ");
  if (tgl_get_peer_type (M->to_id) == TGL_PEER_CHAT) {
    print_chat_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
  } else if (tgl_get_peer_type (M->to_id) == TGL_PEER_CHANNEL) {
    print_channel_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
  } else {
    assert (tgl_get_peer_type (M->to_id) == TGL_PEER_ENCR_CHAT);
    print_encr_chat_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
  }

  if (tgl_get_peer_type (M->from_id) == TGL_PEER_USER) {
    mprintf (ev, " ");
    print_user_name (ev, M->from_id, tgl_peer_get (TLS, M->from_id));
  }
 
  switch (M->action.type) {
  case tgl_message_action_none:
    mprintf (ev, "\n");
    break;
  case tgl_message_action_geo_chat_create:
    mprintf (ev, "Created geo chat\n");
    break;
  case tgl_message_action_geo_chat_checkin:
    mprintf (ev, "Checkin in geochat\n");
    break;
  case tgl_message_action_chat_create:
    mprintf (ev, " created chat %s. %d users\n", M->action.title, M->action.user_num);
    break;
  case tgl_message_action_chat_edit_title:
    mprintf (ev, " changed title to %s\n", 
      M->action.new_title);
    break;
  case tgl_message_action_chat_edit_photo:
    mprintf (ev, " changed photo\n");
    break;
  case tgl_message_action_chat_delete_photo:
    mprintf (ev, " deleted photo\n");
    break;
  case tgl_message_action_chat_add_users:
    mprintf (ev, " added users:");
    {
      int i;
      for (i = 0; i < M->action.user_num; i++) {
        print_user_name (ev, tgl_set_peer_id (TGL_PEER_USER, M->action.users[i]), tgl_peer_get (TLS, tgl_set_peer_id (TGL_PEER_USER, M->action.users[i])));
      }
    }
    mprintf (ev, "\n");
    break;
  case tgl_message_action_chat_add_user_by_link:
    mprintf (ev, " added by link from ");
    print_user_name (ev, tgl_set_peer_id (TGL_PEER_USER, M->action.user), tgl_peer_get (TLS, tgl_set_peer_id (TGL_PEER_USER, M->action.user)));
    mprintf (ev, "\n");
    break;
  case tgl_message_action_chat_delete_user:
    mprintf (ev, " deleted user ");
    print_user_name (ev, tgl_set_peer_id (TGL_PEER_USER, M->action.user), tgl_peer_get (TLS, tgl_set_peer_id (TGL_PEER_USER, M->action.user)));
    mprintf (ev, "\n");
    break;
  case tgl_message_action_set_message_ttl:
    mprintf (ev, " set ttl to %d seconds. Unsupported yet\n", M->action.ttl);
    break;
  case tgl_message_action_read_messages:
    mprintf (ev, " %d messages marked read\n", M->action.read_cnt);
    break;
  case tgl_message_action_delete_messages:
    mprintf (ev, " %d messages deleted\n", M->action.delete_cnt);
    break;
  case tgl_message_action_screenshot_messages:
    mprintf (ev, " %d messages screenshoted\n", M->action.screenshot_cnt);
    break;
  case tgl_message_action_flush_history:
    mprintf (ev, " cleared history\n");
    break;
  case tgl_message_action_resend:
    mprintf (ev, " resend query\n");
    break;
  case tgl_message_action_notify_layer:
    mprintf (ev, " updated layer to %d\n", M->action.layer);
    break;
  case tgl_message_action_typing:
    mprintf (ev, " is ");
    print_typing (ev, M->action.typing);
    break;
  case tgl_message_action_noop:
    mprintf (ev, " noop\n");
    break;
  case tgl_message_action_request_key:
    mprintf (ev, " request rekey #%016llx\n", M->action.exchange_id);
    break;
  case tgl_message_action_accept_key:
    mprintf (ev, " accept rekey #%016llx\n", M->action.exchange_id);
    break;
  case tgl_message_action_commit_key:
    mprintf (ev, " commit rekey #%016llx\n", M->action.exchange_id);
    break;
  case tgl_message_action_abort_key:
    mprintf (ev, " abort rekey #%016llx\n", M->action.exchange_id);
    break;
  case tgl_message_action_channel_create:
    mprintf (ev, " created channel %s\n", M->action.title);
    break;
  case tgl_message_action_migrated_to:
    mprintf (ev, " migrated to channel\n");
    break;
  case tgl_message_action_migrated_from:
    mprintf (ev, " migrated from group '%s'\n", M->action.title);
    break;
  }
  mpop_color (ev);
  //print_end ();
}

tgl_peer_id_t last_from_id;
tgl_peer_id_t last_to_id;

void print_message (struct in_ev *ev, struct tgl_message *M) {
  assert (M);
  if (M->flags & (TGLMF_EMPTY | TGLMF_DELETED)) {
    return;
  }
  if (!(M->flags & TGLMF_CREATED)) { return; }
  if (M->flags & TGLMF_SERVICE) {
    print_service_message (ev, M);
    return;
  }
  if (!tgl_get_peer_type (M->to_id)) {
    logprintf ("Bad msg\n");
    return;
  }

  last_from_id = M->from_id;
  last_to_id = M->to_id;

  //print_start ();
  if (tgl_get_peer_type (M->to_id) == TGL_PEER_USER) {
    if (M->flags & TGLMF_OUT) {
      mpush_color (ev, COLOR_GREEN);
      print_msg_id (ev, M->permanent_id, M);
      mprintf (ev, " ");
      print_date (ev, M->date);
      mpop_color (ev);
      mprintf (ev, " ");
      print_user_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
      mpush_color (ev, COLOR_GREEN);
      if (M->flags & TGLMF_UNREAD) {
        mprintf (ev, " <<< ");
      } else {
        mprintf (ev, " ««« ");
      }
    } else {
      mpush_color (ev, COLOR_BLUE);
      print_msg_id (ev, M->permanent_id, M);
      mprintf (ev, " ");
      print_date (ev, M->date);
      mpop_color (ev);
      mprintf (ev, " ");
      print_user_name (ev, M->from_id, tgl_peer_get (TLS, M->from_id));
      mpush_color (ev, COLOR_BLUE);
      if (M->flags & TGLMF_UNREAD) {
        mprintf (ev, " >>> ");
      } else {
        mprintf (ev, " »»» ");
      }
    }
  } else if (tgl_get_peer_type (M->to_id) == TGL_PEER_ENCR_CHAT) {
    tgl_peer_t *P = tgl_peer_get (TLS, M->to_id);
    assert (P);
    if (M->flags & TGLMF_UNREAD) {
      mpush_color (ev, COLOR_GREEN);
      print_msg_id (ev, M->permanent_id, M);
      mprintf (ev, " ");
      print_date (ev, M->date);
      mprintf (ev, " ");
      mpush_color (ev, COLOR_CYAN);
      mprintf (ev, " %s", P->print_name);
      mpop_color (ev);
      if (M->flags & TGLMF_UNREAD) {
        mprintf (ev, " <<< ");
      } else {
        mprintf (ev, " ««« ");
      }
    } else {
      mpush_color (ev, COLOR_BLUE);
      print_msg_id (ev, M->permanent_id, M);
      mprintf (ev, " ");
      print_date (ev, M->date);
      mpush_color (ev, COLOR_CYAN);
      mprintf (ev, " %s", P->print_name);
      mpop_color (ev);
      if (M->flags & TGLMF_UNREAD) {
        mprintf (ev, " >>> ");
      } else {
        mprintf (ev, " »»» ");
      }
    }
  } else if (tgl_get_peer_type (M->to_id) == TGL_PEER_CHAT) {
    mpush_color (ev, COLOR_MAGENTA);
    print_msg_id (ev, M->permanent_id, M);
    mprintf (ev, " ");
    print_date (ev, M->date);
    mpop_color (ev);
    mprintf (ev, " ");
    print_chat_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
    mprintf (ev, " ");
    print_user_name (ev, M->from_id, tgl_peer_get (TLS, M->from_id));
    if (!tgl_cmp_peer_id (M->from_id, TLS->our_id)) {
      mpush_color (ev, COLOR_GREEN);
    } else {
      mpush_color (ev, COLOR_BLUE);
    }
    if (M->flags & TGLMF_UNREAD) {
      mprintf (ev, " >>> ");
    } else {
      mprintf (ev, " »»» ");
    }
  } else {
    assert (tgl_get_peer_type (M->to_id) == TGL_PEER_CHANNEL);
    
    mpush_color (ev, COLOR_CYAN);
    print_msg_id (ev, M->permanent_id, M);
    mprintf (ev, " ");
    print_date (ev, M->date);
    mpop_color (ev);
    mprintf (ev, " ");
    print_channel_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));

    if (tgl_get_peer_type (M->from_id) == TGL_PEER_USER) {
      mprintf (ev, " ");
      print_user_name (ev, M->from_id, tgl_peer_get (TLS, M->from_id));
      if (!tgl_cmp_peer_id (M->from_id, TLS->our_id)) {
        mpush_color (ev, COLOR_GREEN);
      } else {
        mpush_color (ev, COLOR_BLUE);
      }
    } else {
      mpush_color (ev, COLOR_BLUE);
    }
    if (M->flags & TGLMF_UNREAD) {
      mprintf (ev, " >>> ");
    } else {
      mprintf (ev, " »»» ");
    }
  }
  if (tgl_get_peer_type (M->fwd_from_id) > 0) {
    mprintf (ev, "[fwd from ");
    print_peer_name (ev, M->fwd_from_id, tgl_peer_get (TLS, M->fwd_from_id));
    mprintf (ev, " ");
    print_date (ev, M->date);
    mprintf (ev, "] ");
  }
  if (M->reply_id) {
    mprintf (ev, "[reply to ");
    tgl_message_id_t msg_id = M->permanent_id;
    msg_id.id = M->reply_id;
    struct tgl_message *N = tgl_message_get (TLS, &msg_id);
    print_msg_id (ev, msg_id, N);
    mprintf (ev, "] ");
  }
  if (M->flags & TGLMF_MENTION) {
    mprintf (ev, "[mention] ");
  }
  if (M->message && strlen (M->message)) {
    mprintf (ev, "%s", M->message);
  }
  if (M->media.type != tgl_message_media_none) {
    if (M->message && strlen (M->message)) {
      mprintf (ev, " ");
    }
    print_media (ev, &M->media);
  }
  mpop_color (ev);
  assert (!color_stack_pos);
  mprintf (ev, "\n");
  //print_end();
}

void play_sound (void) {
  printf ("\a");
}

void set_interface_callbacks (void) {
  if (readline_disabled) { return; }
  readline_active = 1;
  rl_filename_quote_characters = strdup (" ");
  rl_basic_word_break_characters = strdup (" ");
  
  
  rl_callback_handler_install (get_default_prompt (), interpreter);
  rl_completion_entry_function = command_generator;
}
