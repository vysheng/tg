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

    Copyright Vitaly Valtman 2013-2014
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE 

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

#include "include.h"
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

#include "tgl.h"
#include "loop.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef __APPLE__
#define OPEN_BIN "open %s"
#else
#define OPEN_BIN "xdg-open %s"
#endif

#define ALLOW_MULT 1
char *default_prompt = "> ";

int disable_auto_accept;
int msg_num_mode;
int disable_colors;
int alert_sound;
extern int binlog_read;

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

extern struct tgl_state *TLS;

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
  if (ev->bev && socket_answer_pos > 0) {
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

#define NOT_FOUND (int)0x80000000
tgl_peer_id_t TGL_PEER_NOT_FOUND = {.id = NOT_FOUND};

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

tgl_peer_id_t cur_token_user (void) {
  if (cur_token_len <= 0) { return TGL_PEER_NOT_FOUND; }
  int l = cur_token_len;
  char *s = cur_token;

  char c = cur_token[cur_token_len];
  cur_token[cur_token_len] = 0;

  if (l >= 8 && !memcmp (s, "user#id", 7)) {
    s += 7;    
    l -= 7;
    int r = atoi (s);
    cur_token[cur_token_len] = c;
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_USER, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }
  if (l >= 6 && !memcmp (s, "user#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    cur_token[cur_token_len] = c;
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_USER, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }

  tgl_peer_t *P = tgl_peer_get_by_name (TLS, s); 
  cur_token[cur_token_len] = c;
  
  if (P && tgl_get_peer_type (P->id) == TGL_PEER_USER) {
    return P->id;
  } else {
    return TGL_PEER_NOT_FOUND;
  }
}

tgl_peer_id_t cur_token_chat (void) {
  if (cur_token_len <= 0) { return TGL_PEER_NOT_FOUND; }
  int l = cur_token_len;
  char *s = cur_token;

  char c = cur_token[cur_token_len];
  cur_token[cur_token_len] = 0;
  
  if (l >= 8 && !memcmp (s, "chat#id", 7)) {
    s += 7;    
    l -= 7;
    int r = atoi (s);
    cur_token[cur_token_len] = c;
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_CHAT, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }
  if (l >= 6 && !memcmp (s, "chat#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    cur_token[cur_token_len] = c;
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_CHAT, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }

  tgl_peer_t *P = tgl_peer_get_by_name (TLS, s); 
  cur_token[cur_token_len] = c;
  
  if (P && tgl_get_peer_type (P->id) == TGL_PEER_CHAT) {
    return P->id;
  } else {
    return TGL_PEER_NOT_FOUND;
  }
}

tgl_peer_id_t cur_token_encr_chat (void) {
  if (cur_token_len <= 0) { return TGL_PEER_NOT_FOUND; }
  char *s = cur_token;
  char c = cur_token[cur_token_len];
  cur_token[cur_token_len] = 0;

  tgl_peer_t *P = tgl_peer_get_by_name (TLS, s); 
  cur_token[cur_token_len] = c;
  
  if (P && tgl_get_peer_type (P->id) == TGL_PEER_ENCR_CHAT) {
    return P->id;
  } else {
    return TGL_PEER_NOT_FOUND;
  }
}

tgl_peer_id_t cur_token_peer (void) {
  if (cur_token_len <= 0) { return TGL_PEER_NOT_FOUND; }
  int l = cur_token_len;
  char *s = cur_token;
  char c = cur_token[cur_token_len];
  cur_token[cur_token_len] = 0;
  
  if (l >= 8 && !memcmp (s, "user#id", 7)) {
    s += 7;    
    l -= 7;
    int r = atoi (s);
    cur_token[cur_token_len] = c;
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_USER, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }
  if (l >= 8 && !memcmp (s, "chat#id", 7)) {
    s += 7;    
    l -= 7;
    int r = atoi (s);
    cur_token[cur_token_len] = c;
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_CHAT, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }
  if (l >= 6 && !memcmp (s, "user#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    cur_token[cur_token_len] = c;
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_USER, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }
  if (l >= 6 && !memcmp (s, "chat#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    cur_token[cur_token_len] = c;
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_CHAT, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }
  
  tgl_peer_t *P = tgl_peer_get_by_name (TLS, s); 
  cur_token[cur_token_len] = c;
  
  if (P) {
    return P->id;
  } else {
    return TGL_PEER_NOT_FOUND;
  }
}

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
}

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

char *complete_none (const char *text UU, int state UU) {
  return 0;
}


void set_prompt (const char *s) {
  if (readline_disabled) { return; }
  rl_set_prompt (s);
}

void update_prompt (void) {
  if (readline_disabled) { return; }
  print_start ();
  set_prompt (get_default_prompt ());
  if (readline_active) {
    rl_redisplay ();
  }
  print_end ();
}

char *modifiers[] = {
  "[offline]",
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
  ca_peer,
  ca_file_name,
  ca_file_name_end,
  ca_period,
  ca_number,
  ca_double,
  ca_string_end,
  ca_string,
  ca_modifier,
  ca_command,
  ca_extf,


  ca_optional = 256
};

struct arg {
  int flags;
  struct {
    tgl_peer_t *P;
    struct tgl_message *M;
    char *str;
    long long num;
    double dval;
  };
};

struct command {
  char *name;
  enum command_argument args[10];
  void (*fun)(int arg_num, struct arg args[], struct in_ev *ev);
  char *desc;
};


int offline_mode;
void print_user_list_gw (struct tgl_state *TLS, void *extra, int success, int num, struct tgl_user *UL[]);
void print_msg_list_gw (struct tgl_state *TLS, void *extra, int success, int num, struct tgl_message *ML[]);
void print_dialog_list_gw (struct tgl_state *TLS, void *extra, int success, int size, tgl_peer_id_t peers[], int last_msg_id[], int unread_count[]);
void print_chat_info_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_chat *C);
void print_user_info_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_user *C);
void print_filename_gw (struct tgl_state *TLS, void *extra, int success, char *name);
void open_filename_gw (struct tgl_state *TLS, void *extra, int success, char *name);
void print_secret_chat_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_secret_chat *E);
void print_card_gw (struct tgl_state *TLS, void *extra, int success, int size, int *card);
void print_user_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_user *U);
void print_msg_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_message *M);
void print_msg_success_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_message *M);
void print_encr_chat_success_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_secret_chat *E);;
void print_success_gw (struct tgl_state *TLS, void *extra, int success);

struct command commands[];
void do_help (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  if (ev) { mprint_start (ev); }
  mpush_color (ev, COLOR_YELLOW);
  struct command *cmd = commands;
  while (cmd->name) {
    mprintf (ev, "%s\n", cmd->desc);
    cmd ++;
  }
  mpop_color (ev);
  if (ev) { mprint_end (ev); }
  if (!ev) {
    fflush (stdout);
  }
}

void do_contact_list (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  if (ev) { ev->refcnt ++; }
  tgl_do_update_contact_list (TLS, print_user_list_gw, ev);  
}

void do_stats (int arg_num, struct arg args[], struct in_ev *ev) {
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

void do_history (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_get_history_ext (TLS, args[0].P->id, args[2].num != NOT_FOUND ? args[2].num : 0, args[1].num != NOT_FOUND ? args[1].num : 40, offline_mode, print_msg_list_gw, ev);
}

void do_dialog_list (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 0);
  if (ev) { ev->refcnt ++; }
  tgl_do_get_dialog_list (TLS, print_dialog_list_gw, ev);
}

void do_send_photo (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_photo (TLS, tgl_message_media_photo, args[0].P->id, args[1].str, print_msg_success_gw, ev);
}

void do_send_audio (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_photo (TLS, tgl_message_media_audio, args[0].P->id, args[1].str, print_msg_success_gw, ev);
}

void do_send_video (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_photo (TLS, tgl_message_media_video, args[0].P->id, args[1].str, print_msg_success_gw, ev);
}

void do_send_document (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_photo (TLS, tgl_message_media_document, args[0].P->id, args[1].str, print_msg_success_gw, ev);
}

void do_send_text (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_text (TLS, args[0].P->id, args[1].str, print_msg_success_gw, ev);
}

void do_chat_info (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_get_chat_info (TLS, args[0].P->id, offline_mode, print_chat_info_gw, ev);
}

void do_user_info (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_get_user_info (TLS, args[0].P->id, offline_mode, print_user_info_gw, ev);
}

void do_fwd (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_forward_message (TLS, args[0].P->id, args[1].num, print_msg_success_gw, ev);
}

void do_fwd_media (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_forward_message (TLS, args[0].P->id, args[1].num, print_msg_success_gw, ev);
}

void do_msg (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_message (TLS, args[0].P->id, args[1].str, strlen (args[1].str), print_msg_success_gw, ev);
}

void do_rename_chat (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_rename_chat (TLS, args[0].P->id, args[1].str, print_msg_success_gw, ev);
}

#define DO_LOAD_PHOTO(tp,act,actf) \
void do_ ## act ## _ ## tp (int arg_num, struct arg args[], struct in_ev *ev) { \
  assert (arg_num == 1);\
  struct tgl_message *M = tgl_message_get (TLS, args[0].num);\
  if (M && !M->service && M->media.type == tgl_message_media_ ## tp) {\
    if (ev) { ev->refcnt ++; } \
    tgl_do_load_ ## tp (TLS, &M->media.tp, actf, ev); \
  } else if (M && !M->service && M->media.type == tgl_message_media_ ## tp ## _encr) { \
    if (ev) { ev->refcnt ++; } \
    tgl_do_load_encr_video (TLS, &M->media.encr_video, actf, ev); \
  } \
}

#define DO_LOAD_PHOTO_THUMB(tp,act,actf) \
void do_ ## act ## _ ## tp ## _thumb (int arg_num, struct arg args[], struct in_ev *ev) { \
  assert (arg_num == 1);\
  struct tgl_message *M = tgl_message_get (TLS, args[0].num);\
  if (M && !M->service && M->media.type == tgl_message_media_ ## tp) { \
    if (ev) { ev->refcnt ++; } \
    tgl_do_load_ ## tp ## _thumb (TLS, &M->media.tp, actf, ev); \
  }\
}

DO_LOAD_PHOTO(photo, load, print_filename_gw)
DO_LOAD_PHOTO(video, load, print_filename_gw)
DO_LOAD_PHOTO(audio, load, print_filename_gw)
DO_LOAD_PHOTO(document, load, print_filename_gw)
DO_LOAD_PHOTO_THUMB(video, load, print_filename_gw)
DO_LOAD_PHOTO_THUMB(document, load, print_filename_gw)
DO_LOAD_PHOTO(photo, open, open_filename_gw)
DO_LOAD_PHOTO(video, open, open_filename_gw)
DO_LOAD_PHOTO(audio, open, open_filename_gw)
DO_LOAD_PHOTO(document, open, open_filename_gw)
DO_LOAD_PHOTO_THUMB(video, open, open_filename_gw)
DO_LOAD_PHOTO_THUMB(document, open, open_filename_gw)

void do_add_contact (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_add_contact (TLS, args[0].str, strlen (args[0].str), args[1].str, strlen (args[1].str), args[2].str, strlen (args[2].str), 0, print_user_list_gw, ev);
}

void do_del_contact (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_del_contact (TLS, args[0].P->id, print_success_gw, ev);
}

void do_rename_contact (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (args[0].P->user.phone) {
    if (ev) { ev->refcnt ++; }
    tgl_do_add_contact (TLS, args[0].P->user.phone, strlen (args[0].P->user.phone), args[1].str, strlen (args[1].str), args[2].str, strlen (args[2].str), 0, print_user_list_gw, ev);
  } else {
    if (ev) { ev->refcnt ++; }
    print_success_gw (TLS, ev, 0);
  }
}

void do_show_license (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  static char *b = 
#include "LICENSE.h"
  ;
  if (ev) { mprint_start (ev); }
  mprintf (ev, "%s", b);
  if (ev) { mprint_end (ev); }
}

void do_search (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 6);
  tgl_peer_id_t id;
  if (args[0].P) {
    id = args[0].P->id;
  } else {
    id = TGL_PEER_NOT_FOUND;
  }
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
  tgl_do_msg_search (TLS, id, from, to, limit, offset, args[5].str, print_msg_list_gw, ev);
}

void do_mark_read (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_mark_read (TLS, args[0].P->id, print_success_gw, ev);
}

void do_visualize_key (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  static char *colors[4] = {COLOR_GREY, COLOR_CYAN, COLOR_BLUE, COLOR_GREEN};
  static unsigned char buf[16];
  memset (buf, 0, sizeof (buf));
  tgl_peer_id_t id = args[0].P->id;
  tgl_do_visualize_key (TLS, id, buf);
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

void do_create_secret_chat (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_create_secret_chat (TLS, args[0].P->id, print_secret_chat_gw, ev);
}

void do_chat_add_user (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_add_user_to_chat (TLS, args[0].P->id, args[1].P->id, args[2].num != NOT_FOUND ? args[2].num : 100, print_msg_success_gw, ev);
}

void do_chat_del_user (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_del_user_from_chat (TLS, args[0].P->id, args[1].P->id, print_msg_success_gw, ev);
}

void do_status_online (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  if (ev) { ev->refcnt ++; }
  tgl_do_update_status (TLS, 1, print_success_gw, ev);
}

void do_status_offline (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  if (ev) { ev->refcnt ++; }
  tgl_do_update_status (TLS, 0, print_success_gw, ev);
}

void do_quit (int arg_num, struct arg args[], struct in_ev *ev) {
  do_halt (0);
}

void do_safe_quit (int arg_num, struct arg args[], struct in_ev *ev) {
  safe_quit = 1;
}

void do_set (int arg_num, struct arg args[], struct in_ev *ev) {
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

void do_chat_with_peer (int arg_num, struct arg args[], struct in_ev *ev) {
  if (!ev) {
    in_chat_mode = 1;
    chat_mode_id = args[0].P->id;
  }
}

void do_delete_msg (int arg_num, struct arg args[], struct in_ev *ev) {
  if (ev) { ev->refcnt ++; }
  tgl_do_delete_msg (TLS, args[0].num, print_success_gw, ev);
}

void do_restore_msg (int arg_num, struct arg args[], struct in_ev *ev) {
  if (ev) { ev->refcnt ++; }
  tgl_do_restore_msg (TLS, args[0].num, print_success_gw, ev);
}
    
void do_create_group_chat (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num >= 1 && arg_num <= 1000);
  static tgl_peer_id_t ids[1000];
  int i;
  for (i = 0; i < arg_num - 1; i++) {
    ids[i] = args[i + 1].P->id;
  }

  if (ev) { ev->refcnt ++; }
  tgl_do_create_group_chat_ex (TLS, arg_num - 1, ids, args[0].str, print_msg_success_gw, ev);  
}

void do_chat_set_photo (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_set_chat_photo (TLS, args[0].P->id, args[1].str, print_msg_success_gw, ev); 
}

void do_set_profile_photo (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_set_profile_photo (TLS, args[0].str, print_success_gw, ev);
}

void do_set_profile_name (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_set_profile_name (TLS, args[0].str, args[1].str, print_user_gw, ev);
}

void do_set_username (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_set_username (TLS, args[0].str, print_user_gw, ev);
}

void do_contact_search (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (ev) { ev->refcnt ++; }
  tgl_do_contact_search (TLS, args[0].str, args[1].num == NOT_FOUND ? args[1].num : 10, print_user_list_gw, ev);
}

void do_accept_secret_chat (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 1);
  if (ev) { ev->refcnt ++; }
  tgl_do_accept_encr_chat_request (TLS, &args[0].P->encr_chat, print_encr_chat_success_gw, ev);
}

void do_set_ttl (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 2);
  if (args[0].P->encr_chat.state == sc_ok) {
    if (ev) { ev->refcnt ++; }
    tgl_do_set_encr_chat_ttl (TLS, &args[0].P->encr_chat, args[1].num, print_msg_success_gw, ev);
  } else {
    if (ev) { ev->refcnt ++; }
    print_success_gw (TLS, ev, 0);
  }
}

void do_export_card (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (!arg_num);
  if (ev) { ev->refcnt ++; }
  tgl_do_export_card (TLS, print_card_gw, ev);
}

void do_import_card (int arg_num, struct arg args[], struct in_ev *ev) {
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

void do_send_contact (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 4);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_contact (TLS, args[0].P->id, args[1].str, strlen (args[1].str), args[2].str, strlen (args[2].str), args[3].str, strlen (args[3].str), print_msg_gw, ev);
}

void do_main_session (int arg_num, struct arg args[], struct in_ev *ev) {
  if (notify_ev && !--notify_ev->refcnt) {
    free (notify_ev);
  }
  notify_ev = ev;
  if (ev) { ev->refcnt ++; }
}

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
extern struct event *term_ev;

void do_clear (int arg_num, struct arg args[], struct in_ev *ev) {
  logprintf ("Do_clear\n");
  tgl_free_all (TLS);
  free (default_username);
  free (config_filename);
  free (prefix);
  free (auth_file_name);
  free (state_file_name);
  free (secret_chat_file_name);
  free (downloads_directory);
  free (config_directory);
  free (binlog_file_name);
  free (lua_file);
  clear_history ();
  event_free (term_ev);
  event_base_free (TLS->ev_base);
  do_halt (0);
}

void do_send_location (int arg_num, struct arg args[], struct in_ev *ev) {
  assert (arg_num == 3);
  if (ev) { ev->refcnt ++; }
  tgl_do_send_location (TLS, args[0].P->id, args[1].dval, args[2].dval, print_msg_success_gw, ev);
}


struct command commands[] = {
  {"help", {ca_none}, do_help, "help\tPrints this help"},
  {"contact_list", {ca_none}, do_contact_list, "contact_list\tPrints contact list"},
  {"stats", {ca_none}, do_stats, "stats\tFor debug purpose"},
  {"history", {ca_peer, ca_number | ca_optional, ca_number | ca_optional, ca_none}, do_history, "history <peer> [limit] [offset]\tPrints messages with this peer (most recent message lower). Also marks messages as read"},
  {"dialog_list", {ca_none}, do_dialog_list, "dialog_list\tList of last conversations"},
  {"send_photo", {ca_peer, ca_file_name_end, ca_none}, do_send_photo, "send_photo <peer> <file>\tSends photo to peer"},
  {"send_video", {ca_peer, ca_file_name_end, ca_none}, do_send_video, "send_video <peer> <file>\tSends video to peer"},
  {"send_audio", {ca_peer, ca_file_name_end, ca_none}, do_send_audio, "send_audio <peer> <file>\tSends audio to peer"},
  {"send_document", {ca_peer, ca_file_name_end, ca_none}, do_send_document, "send_document <peer> <file>\tSends document to peer"},
  {"send_text", {ca_peer, ca_file_name_end, ca_none}, do_send_text, "send_text <peer> <file>\tSends contents of text file as plain text message"},
  {"chat_info", {ca_chat, ca_none}, do_chat_info, "chat_info <chat>\tPrints info about chat (id, members, admin, etc.)"},
  {"user_info", {ca_user, ca_none}, do_user_info, "user_info <user>\tPrints info about user (id, last online, phone)"},
  {"fwd", {ca_peer, ca_number, ca_none}, do_fwd, "fwd <peer> <msg-id>\tForwards message to peer. Forward to secret chats is forbidden"},
  {"fwd_media", {ca_peer, ca_number, ca_none}, do_fwd_media, "fwd <peer> <msg-id>\tForwards message media to peer. Forward to secret chats is forbidden. Result slightly differs from fwd"},
  {"msg", {ca_peer, ca_string_end, ca_none}, do_msg, "msg <peer> <text>\tSends text message to peer"},
  {"rename_chat", {ca_chat, ca_string_end, ca_none}, do_rename_chat, "rename_chat <chat> <new name>\tRenames chat"},
  {"load_photo", {ca_number, ca_none}, do_load_photo, "load_photo <msg-id>\tDownloads file to downloads dirs. Prints file name after download end"},
  {"load_video", {ca_number, ca_none}, do_load_video, "load_video <msg-id>\tDownloads file to downloads dirs. Prints file name after download end"},
  {"load_audio", {ca_number, ca_none}, do_load_audio, "load_audio <msg-id>\tDownloads file to downloads dirs. Prints file name after download end"},
  {"load_document", {ca_number, ca_none}, do_load_document, "load_document <msg-id>\tDownloads file to downloads dirs. Prints file name after download end"},
  {"load_video_thumb", {ca_number, ca_none}, do_load_video_thumb, "load_video_thumb <msg-id>\tDownloads file to downloads dirs. Prints file name after download end"},
  {"load_document_thumb", {ca_number, ca_none}, do_load_document_thumb, "load_document_thumb <msg-id>\tDownloads file to downloads dirs. Prints file name after download end"},
  {"view_photo", {ca_number, ca_none}, do_open_photo, "view_photo <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action"},
  {"view_video", {ca_number, ca_none}, do_open_video, "view_video <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action"},
  {"view_audio", {ca_number, ca_none}, do_open_audio, "view_audio <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action"},
  {"view_document", {ca_number, ca_none}, do_open_document, "view_document <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action"},
  {"view_video_thumb", {ca_number, ca_none}, do_open_video_thumb, "view_video_thumb <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action"},
  {"view_document_thumb", {ca_number, ca_none}, do_open_document_thumb, "view_document_thumb <msg-id>\tDownloads file to downloads dirs. Then tries to open it with system default action"},
  {"add_contact", {ca_string, ca_string, ca_string, ca_none}, do_add_contact, "add_contact <phone> <first name> <last name>\tTries to add user to contact list"},
  {"del_contact", {ca_user, ca_none}, do_del_contact, "del_contact <user>\tDeletes contact from contact list"},
  {"rename_contact", {ca_user, ca_string, ca_string, ca_none}, do_rename_contact, "rename_contact <user> <first name> <last name>\tRenames contact"},
  {"show_license", {ca_none}, do_show_license, "show_license\tPrints contents of GPL license"},
  {"search", {ca_peer | ca_optional, ca_number | ca_optional, ca_number | ca_optional, ca_number | ca_optional, ca_number | ca_optional, ca_string_end}, do_search, "search [peer] [limit] [from] [to] [offset] pattern\tSearch for pattern in messages from date from to date to (unixtime) in messages with peer (if peer not present, in all messages)"},
  {"mark_read", {ca_peer, ca_none}, do_mark_read, "mark_read <peer>\tMarks messages with peer as read"},
  {"contact_search", {ca_string, ca_number | ca_optional, ca_none}, do_contact_search, "contact_search username [limit]\tSearches contacts by username"},
  {"visualize_key", {ca_secret_chat, ca_none}, do_visualize_key, "visualize_key <secret chat>\tPrints visualization of encryption key (first 16 bytes sha1 of it in fact}"},
  {"create_secret_chat", {ca_user, ca_none}, do_create_secret_chat, "create_secret_chat <user>\tStarts creation of secret chat"},
  {"chat_add_user", {ca_chat, ca_user, ca_number | ca_optional, ca_none}, do_chat_add_user, "chat_add_user <chat> <user> [msgs-to-forward]\tAdds user to chat. Sends him last msgs-to-forward message from this chat. Default 100"},
  {"chat_del_user", {ca_chat, ca_user, ca_none}, do_chat_del_user, "chat_del_user <chat> <user>\tDeletes user from chat"},
  {"status_online", {ca_none}, do_status_online, "status_online\tSets status as online"},
  {"status_offline", {ca_none}, do_status_offline, "status_offline\tSets status as offline"},
  {"quit", {ca_none}, do_quit, "quit\tQuits immediately"},
  {"safe_quit", {ca_none}, do_safe_quit, "safe_quit\tWaits for all queries to end, then quits"},
  {"set", {ca_string, ca_number, ca_none}, do_set, "set <param> <value>\tSets value of param. Currently available: log_level, debug_verbosity, alarm, msg_num"},
  {"chat_with_peer", {ca_peer, ca_none}, do_chat_with_peer, "chat_with_peer <peer>\tInterface option. All input will be treated as messages to this peer. Type /quit to end this mode"},
  {"delete_msg", {ca_number, ca_none}, do_delete_msg, "delete_msg <msg-id>\tDeletes message"},
  {"restore_msg", {ca_number, ca_none}, do_restore_msg, "restore_msg <msg-id>\tRestores message. Only available shortly (one hour?) after deletion"},
  {"create_group_chat", {ca_string, ca_user, ca_period, ca_none}, do_create_group_chat, "create_group_chat <name> <user>+\tCreates group chat with users"},
  {"chat_set_photo", {ca_chat, ca_file_name_end, ca_none}, do_chat_set_photo, "chat_set_photo <chat> <filename>\tSets chat photo. Photo will be cropped to square"},
  {"set_profile_photo", {ca_file_name_end, ca_none}, do_set_profile_photo, "set_profile_photo <filename>\tSets profile photo. Photo will be cropped to square"},
  {"set_profile_name", {ca_string, ca_string, ca_none}, do_set_profile_name, "set_profile_name <first-name> <last-name>\tSets profile name."},
  {"set_username", {ca_string, ca_none}, do_set_username, "set_username <name>\tSets username."},
  {"accept_secret_chat", {ca_secret_chat, ca_none}, do_accept_secret_chat, "accept_secret_chat <secret chat>\tAccepts secret chat. Only useful with -E option"},
  {"set_ttl", {ca_secret_chat, ca_number,  ca_none}, do_set_ttl, "set_ttl <secret chat>\tSets secret chat ttl. Client itself ignores ttl"},
  {"export_card", {ca_none}, do_export_card, "export_card\tPrints card that can be imported by another user with import_card method"},
  {"import_card", {ca_string, ca_none}, do_import_card, "import_card <card>\tGets user by card and prints it name. You can then send messages to him as usual"},
  {"send_contact", {ca_peer, ca_string, ca_string, ca_string, ca_none}, do_send_contact, "send_contact <peer> <phone> <first-name> <last-name>\tSends contact (not necessary telegram user)"},
  {"main_session", {ca_none}, do_main_session, "main_session\tSends updates to this connection (or terminal). Useful only with listening socket"},
  {"clear", {ca_none}, do_clear, "clear\tClears all data and exits. For debug."},
  {"send_location", {ca_peer, ca_double, ca_double, ca_none}, do_send_location, "send_location <peer> <latitude> <longitude>\tSends geo location"},
  {0, {ca_none}, 0, ""}
};


enum command_argument get_complete_mode (void) {
  force_end_mode = 0;
  line_ptr = rl_line_buffer;

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
    if (*flags == ca_period) {
      flags --;
    }
    enum command_argument op = (*flags) & 255;
    int opt = (*flags) & ca_optional;

    if (op == ca_none) { return ca_none; }
    if (op == ca_string_end || op == ca_file_name_end) {
      next_token_end ();
      if (cur_token_len < 0 || !cur_token_end_str) { 
        return ca_none;
      } else {
        return op;
      }
    }
    
    char *save = line_ptr;
    next_token ();
    if (op == ca_user || op == ca_chat || op == ca_secret_chat || op == ca_peer || op == ca_number || op == ca_double) {
      if (cur_token_quoted) {
        if (opt) {
          line_ptr = save;
          flags ++;
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
        case ca_peer:
          ok = (tgl_get_peer_type (cur_token_user ()) != NOT_FOUND);
          break;
        case ca_number:
          ok = (cur_token_int () != NOT_FOUND);
          break;
        case ca_double:
          ok = (cur_token_double () != NOT_FOUND);
          break;
        default:
          assert (0);
        }

        if (opt && !ok) {
          line_ptr = save;
          flags ++;
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

int complete_command_list (int index, const char *text, int len, char **R) {
  index ++;
  while (commands[index].name && strncmp (commands[index].name, text, len)) {
    index ++;
  }
  if (commands[index].name) {
void print_msg_success_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_message *M);
void print_encr_chat_success_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_secret_chat *E);;
void print_success_gw (struct tgl_state *TLS, void *extra, int success);
    *R = strdup (commands[index].name);
    assert (*R);
    return index;
  } else {
    *R = 0;
    return -1;
  }
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
    if (index == -1) { return 0; }
  }
  
  if (mode == ca_none || mode == ca_string || mode == ca_string_end || mode == ca_number || mode == ca_double) { 
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
    index = tgl_complete_user_list (TLS, index, command_pos, command_len, &R);    
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_peer:
    index = tgl_complete_peer_list (TLS, index, command_pos, command_len, &R);
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
  case ca_modifier:
    index = complete_string_list (modifiers, index, command_pos, command_len, &R);
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

char **complete_text (char *text, int start UU, int end UU) {
  return (char **) rl_completion_matches (text, command_generator);
}

int count = 1;
void work_modifier (const char *s, int l) {
  if (is_same_word (s, l, "[offline]")) {
    offline_mode = 1;
  }
#ifdef ALLOW_MULT
  if (sscanf (s, "[x%d]", &count) >= 1) {
  }
#endif
}

void print_fail (struct in_ev *ev) {
  if (ev) {
    mprint_start (ev);
    mprintf (ev, "FAIL\n");
    mprint_end (ev);
  }
}

void print_success (struct in_ev *ev) {
  if (ev) {
    mprint_start (ev);
    mprintf (ev, "SUCCESS\n");
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
  print_success_gw (TLS, extra, success);
}

void print_encr_chat_success_gw (struct tgl_state *TLS, void *extra, int success, struct tgl_secret_chat *E) {
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
  int i;
  for (i = num - 1; i >= 0; i--) {
    print_message (ev, ML[i]);
  }
  mprint_end (ev);
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
  print_message (ev, M);
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
  int i;
  for (i = num - 1; i >= 0; i--) {
    print_user_name (ev, UL[i]->id, (void *)UL[i]);
    mprintf (ev, "\n");
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
  print_user_name (ev, U->id, (void *)U);
  mprintf (ev, "\n");
  mprint_end (ev);
}

void print_filename_gw (struct tgl_state *TLSR, void *extra, int success, char *name) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  mprintf (ev, "Saved to %s\n", name);
  mprint_end (ev);
}

void open_filename_gw (struct tgl_state *TLSR, void *extra, int success, char *name) {
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
    int x = system (buf);
    if (x < 0) {
      logprintf ("Can not open image viewer: %m\n");
      logprintf ("Image is at %s\n", name);
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
  mprint_end (ev);
}

void print_user_info_gw (struct tgl_state *TLSR, void *extra, int success, struct tgl_user *U) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  tgl_peer_t *C = (void *)U;
  mprint_start (ev);
  mpush_color (ev, COLOR_YELLOW);
  mprintf (ev, "User ");
  print_user_name (ev, U->id, C);
  if (U->username) {
    mprintf (ev, " @%s", U->username);
  }
  mprintf (ev, " (#%d):\n", tgl_get_peer_id (U->id));
  mprintf (ev, "\treal name: %s %s\n", U->real_first_name, U->real_last_name);
  mprintf (ev, "\tphone: %s\n", U->phone);
  if (U->status.online > 0) {
    mprintf (ev, "\tonline\n");
  } else {
    mprintf (ev, "\toffline (was online ");
    print_date_full (ev, U->status.when);
    mprintf (ev, ")\n");
  }
  mpop_color (ev);
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
  mpush_color (ev, COLOR_YELLOW);
  mprintf (ev, " Encrypted chat ");
  print_encr_chat_name (ev, E->id, (void *)E);
  mprintf (ev, " is now in wait state\n");
  mpop_color (ev);
  mprint_end (ev);
}

void print_dialog_list_gw (struct tgl_state *TLSR, void *extra, int success, int size, tgl_peer_id_t peers[], int last_msg_id[], int unread_count[]) {
  assert (TLS == TLSR);
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
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
    }
  }
  mpop_color (ev);
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
    tgl_do_get_history (TLS, chat_mode_id, limit, offline_mode, print_msg_list_gw, 0);
    return;
  }
  if (!strncmp (line, "/read", 5)) {
    tgl_do_mark_read (TLS, chat_mode_id, 0, 0);
    return;
  }
  if (strlen (line) > 0) {
    tgl_do_send_message (TLS, chat_mode_id, line, strlen (line), 0, 0);
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
    tgl_peer_id_t to_id;
    if (tgl_get_peer_type (list[i]->to_id) == TGL_PEER_USER && tgl_get_peer_id (list[i]->to_id) == TLS->our_id) {
      to_id = list[i]->from_id;
    } else {
      to_id = list[i]->to_id;
    }
    int j;
    int c1 = 0;
    int c2 = 0;
    for (j = i; j < num; j++) if (list[j]) {
      tgl_peer_id_t end_id;
      if (tgl_get_peer_type (list[j]->to_id) == TGL_PEER_USER && tgl_get_peer_id (list[j]->to_id) == TLS->our_id) {
        end_id = list[j]->from_id;
      } else {
        end_id = list[j]->to_id;
      }
      if (!tgl_cmp_peer_id (to_id, end_id)) {
        if (list[j]->out) {
          c1 ++;
        } else {
          c2 ++;
        }
        list[j] = 0;
      }
    }

    assert (c1 + c2 > 0);
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
  print_message (ev, M);
  mprint_end (ev);
}

void our_id_gw (struct tgl_state *TLSR, int id) {
  assert (TLSR == TLS);
  #ifdef USE_LUA
    lua_our_id (id);
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

void user_update_gw (struct tgl_state *TLSR, struct tgl_user *U, unsigned flags) {
  assert (TLSR == TLS);
  #ifdef USE_LUA
    lua_user_update (U, flags);
  #endif
  
  if (disable_output && !notify_ev) { return; }
  if (!binlog_read) { return; }
  struct in_ev *ev = notify_ev;

  if (!(flags & TGL_UPDATE_CREATED)) {
    mprint_start (ev);
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
    mprint_end (ev);
  }
}

void chat_update_gw (struct tgl_state *TLSR, struct tgl_chat *U, unsigned flags) {
  assert (TLSR == TLS);
  #ifdef USE_LUA
    lua_chat_update (U, flags);
  #endif
  
  if (disable_output && !notify_ev) { return; }
  if (!binlog_read) { return; }
  struct in_ev *ev = notify_ev;

  if (!(flags & TGL_UPDATE_CREATED)) {
    mprint_start (ev);
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
    mprint_end (ev);
  }
}

void secret_chat_update_gw (struct tgl_state *TLSR, struct tgl_secret_chat *U, unsigned flags) {
  assert (TLSR == TLS);
  #ifdef USE_LUA
    lua_secret_chat_update (U, flags);
  #endif
  
  if ((flags & TGL_UPDATE_WORKING) || (flags & TGL_UPDATE_DELETED)) {
    write_secret_chat_file ();
  }
  
  if (!binlog_read) { return; }

  if ((flags & TGL_UPDATE_REQUESTED) && !disable_auto_accept)  {
    tgl_do_accept_encr_chat_request (TLS, U, 0, 0);
  }
  
  if (disable_output && !notify_ev) { return; }
  struct in_ev *ev = notify_ev;


  if (!(flags & TGL_UPDATE_CREATED)) {
    mprint_start (ev);
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
    mprint_end (ev);
  }
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
  mprintf (ev, "Card: ");
  int i;
  for (i = 0; i < size; i++) {
    mprintf (ev, "%08x%c", card[i], i == size - 1 ? '\n' : ':');
  }
  mprint_end (ev);
}

void callback_extf (struct tgl_state *TLS, void *extra, int success, char *buf) {
  struct in_ev *ev = extra;
  if (ev && !--ev->refcnt) {
    free (ev);
    return;
  }
  if (!success) { print_fail (ev); return; }
  mprint_start (ev);
  mprintf (ev, "%s\n", buf);
  mprint_end (ev);
}

struct tgl_update_callback upd_cb = {
  .new_msg = print_message_gw,
  .marked_read = mark_read_upd,
  .logprintf = logprintf,
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
  .msg_receive = print_message_gw,
  .our_id = our_id_gw
};


void interpreter_ex (char *line UU, void *ex) {
  force_end_mode = 1;
  assert (!in_readline);
  in_readline = 1;
  if (in_chat_mode) {
    interpreter_chat_mode (line);
    in_readline = 0;
    return;
  }

  line_ptr = line;
  offline_mode = 0;
  count = 1;
  if (!line) { 
    do_safe_quit (0, NULL, NULL);
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
      return; 
    }

    if (cur_token_len <= 0) { 
      in_readline = 0;
      return; 
    }
    
    if (*cur_token == '[') {
      if (cur_token_end_str) {
        in_readline = 0;
        return; 
      }
      if (cur_token[cur_token_len - 1] != ']') {
        in_readline = 0;
        return; 
      }
      work_modifier (cur_token, cur_token_len);
      continue;
    }
    break;
  }
  if (cur_token_quoted || cur_token_end_str) { 
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
    in_readline = 0;
    return; 
  }

  enum command_argument *flags = command->args;
  void (*fun)(int, struct arg[], struct in_ev *) = command->fun;
  int args_num = 0;
  static struct arg args[1000];
  while (1) {
    assert (args_num < 1000);
    args[args_num].flags = 0;
    int period = 0;
    if (*flags == ca_period) {
      period = 1;
      flags --;
    }
    enum command_argument op = (*flags) & 255;
    int opt = (*flags) & ca_optional;

    if (op == ca_none) { 
      next_token ();
      if (cur_token_end_str) {
        int z;
        for (z = 0; z < count; z ++) {
          fun (args_num, args, ex);
        }
      }
      break;
    }
      
    if (op == ca_string_end || op == ca_file_name_end) {
      next_token_end ();
      if (cur_token_len < 0) { 
        break;
      } else {
        args[args_num].flags = 1;
        args[args_num ++].str = strndup (cur_token, cur_token_len);
        int z;
        for (z = 0; z < count; z ++) {
          fun (args_num, args, ex);
        }
        break;
      }
    }

    char *save = line_ptr;
    next_token ();

    if (period && cur_token_end_str) {
      int z;
      for (z = 0; z < count; z ++) {
        fun (args_num, args, ex);
      }
      break;
    }

    if (op == ca_user || op == ca_chat || op == ca_secret_chat || op == ca_peer || op == ca_number || op == ca_double) {
      if (cur_token_quoted) {
        if (opt) {
          if (op != ca_number && op != ca_double) {
            args[args_num ++].P = 0;
          } else {
            if (op == ca_number) {
              args[args_num ++].num = NOT_FOUND;
            } else {
              args[args_num ++].dval = NOT_FOUND;
            }
          }
          line_ptr = save;
          flags ++;
          continue;
        } else {
          break;
        }
      } else {
        if (cur_token_end_str) { 
          if (opt) {
            if (op != ca_number && op != ca_double) {
              args[args_num ++].P = 0;
            } else {
              if (op == ca_number) {
                args[args_num ++].num = NOT_FOUND;
              } else {
                args[args_num ++].dval = NOT_FOUND;
              }
            }
            line_ptr = save;
            flags ++;
            continue;
          } else {
            break;
          }
        }
        int ok = 1;
        switch (op) {
        case ca_user:
          args[args_num ++].P = mk_peer (cur_token_user ()); 
          ok = args[args_num - 1].P != NULL;
          break;
        case ca_chat:
          args[args_num ++].P = mk_peer (cur_token_chat ()); 
          ok = args[args_num - 1].P != NULL;
          break;
        case ca_secret_chat:
          args[args_num ++].P = mk_peer (cur_token_encr_chat ()); 
          ok = args[args_num - 1].P != NULL;
          break;
        case ca_peer:
          args[args_num ++].P = mk_peer (cur_token_peer ()); 
          ok = args[args_num - 1].P != NULL;
          break;
        case ca_number:
          args[args_num ++].num = cur_token_int ();
          ok = (args[args_num - 1].num != NOT_FOUND);
          break;
        case ca_double:
          args[args_num ++].dval = cur_token_double ();
          ok = (args[args_num - 1].dval != NOT_FOUND);
          break;
        default:
          assert (0);
        }

        if (opt && !ok) {
          line_ptr = save;
          flags ++;
          continue;
        }
        if (!ok) {
          break;
        }

        flags ++;
        continue;
      }
    }
    if (op == ca_string || op == ca_file_name) {
      if (cur_token_end_str || cur_token_len < 0) {
        break;
      } else {
        args[args_num].flags = 1;
        args[args_num ++].str = strndup (cur_token, cur_token_len);
        flags ++;
        continue;
      }
    }
    assert (0);
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

void interpreter (char *line UU) {
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
int prompt_was;
void print_start (void) {
  if (in_readline) { return; }
  if (readline_disabled) { return; }
  assert (!prompt_was);
  if (readline_active) {
    saved_point = rl_point;
#ifdef READLINE_GNU
    saved_line = malloc (rl_end + 1);
    assert (saved_line);
    saved_line[rl_end] = 0;
    memcpy (saved_line, rl_line_buffer, rl_end);

    rl_save_prompt();
    rl_replace_line("", 0);
#else
    assert (rl_end >= 0);
    saved_line = malloc (rl_end + 1);
    assert (saved_line);
    memcpy (saved_line, rl_line_buffer, rl_end + 1);
    rl_line_buffer[0] = 0;
    set_prompt ("");
#endif
    rl_redisplay();
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
    set_prompt (get_default_prompt ());
#if READLINE_GNU
    rl_replace_line(saved_line, 0);
#else
    memcpy (rl_line_buffer, saved_line, rl_end + 1); // not safe, but I hope this would work. 
#endif
    rl_point = saved_point;
    rl_redisplay();
    free (saved_line);
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
      if (M->photo.caption && strlen (M->photo.caption)) {
        mprintf (ev, "[photo %s]", M->photo.caption);
      } else {
        mprintf (ev, "[photo]");
      }
      return;
    case tgl_message_media_video:
      if (M->video.mime_type) {
        mprintf (ev, "[video: type %s]", M->video.mime_type);
      } else {
        mprintf (ev, "[video]");
      }
      return;
    case tgl_message_media_audio:
      if (M->audio.mime_type) {
        mprintf (ev, "[audio: type %s]", M->audio.mime_type);
      } else {
        mprintf (ev, "[audio]");
      }
      return;
    case tgl_message_media_document:
      if (M->document.mime_type && M->document.caption) {
        mprintf (ev, "[document %s: type %s]", M->document.caption, M->document.mime_type);
      } else {
        mprintf (ev, "[document]");
      }
      return;
    case tgl_message_media_photo_encr:
      mprintf (ev, "[photo]");
      return;
    case tgl_message_media_video_encr:
      if (M->encr_video.mime_type) {
        mprintf (ev, "[video: type %s]", M->encr_video.mime_type);
      } else {
        mprintf (ev, "[video]");
      }
      return;
    case tgl_message_media_audio_encr:
      if (M->encr_audio.mime_type) {
        mprintf (ev, "[audio: type %s]", M->encr_audio.mime_type);
      } else {
        mprintf (ev, "[audio]");
      }
      return;
    case tgl_message_media_document_encr:
      if (M->encr_document.mime_type && M->encr_document.file_name) {
        mprintf (ev, "[document %s: type %s]", M->encr_document.file_name, M->encr_document.mime_type);
      } else {
        mprintf (ev, "[document]");
      }
      return;
    case tgl_message_media_geo:
      mprintf (ev, "[geo] https://maps.google.com/?q=%.6lf,%.6lf", M->geo.latitude, M->geo.longitude);
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
    default:
      mprintf (ev, "x = %d\n", M->type);
      assert (0);
  }
}

int unknown_user_list_pos;
int unknown_user_list[1000];

void print_user_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *U) {
  assert (tgl_get_peer_type (id) == TGL_PEER_USER);
  mpush_color (ev, COLOR_RED);
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
    if (U->flags & (FLAG_USER_SELF | FLAG_USER_CONTACT)) {
      mpush_color (ev, COLOR_REDB);
    }
    if ((U->flags & FLAG_DELETED)) {
      mprintf (ev, "deleted user#%d", tgl_get_peer_id (id));
    } else if (!(U->flags & FLAG_CREATED)) {
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
    if (U->flags & (FLAG_USER_SELF | FLAG_USER_CONTACT)) {
      mpop_color (ev);
    }
  }
  mpop_color (ev);
}

void print_chat_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C) {
  assert (tgl_get_peer_type (id) == TGL_PEER_CHAT);
  mpush_color (ev, COLOR_MAGENTA);
  if (!C || use_ids) {
    mprintf (ev, "chat#%d", tgl_get_peer_id (id));
  } else {
    mprintf (ev, "%s", C->chat.title);
  }
  mpop_color (ev);
}

void print_encr_chat_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C) {
  assert (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT);
  mpush_color (ev, COLOR_MAGENTA);
  if (!C || use_ids) {
    mprintf (ev, "encr_chat#%d", tgl_get_peer_id (id));
  } else {
    mprintf (ev, "%s", C->print_name);
  }
  mpop_color (ev);
}

void print_encr_chat_name_full (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C) {
  assert (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT);
  mpush_color (ev, COLOR_MAGENTA);
  if (!C || use_ids) {
    mprintf (ev, "encr_chat#%d", tgl_get_peer_id (id));
  } else {
    mprintf (ev, "%s", C->print_name);
  }
  mpop_color (ev);
}

static char *monthes[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
void print_date (struct in_ev *ev, long t) {
  struct tm *tm = localtime ((void *)&t);
  if (time (0) - t < 12 * 60 * 60) {
    mprintf (ev, "[%02d:%02d] ", tm->tm_hour, tm->tm_min);
  } else {
    mprintf (ev, "[%02d %s]", tm->tm_mday, monthes[tm->tm_mon]);
  }
}

void print_date_full (struct in_ev *ev, long t) {
  struct tm *tm = localtime ((void *)&t);
  mprintf (ev, "[%04d/%02d/%02d %02d:%02d:%02d]", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

void print_service_message (struct in_ev *ev, struct tgl_message *M) {
  assert (M);
  //print_start ();
  mpush_color (ev, COLOR_GREY);
  
  mpush_color (ev, COLOR_MAGENTA);
  if (msg_num_mode) {
    mprintf (ev, "%lld ", M->id);
  }
  print_date (ev, M->date);
  mpop_color (ev);
  mprintf (ev, " ");
  if (tgl_get_peer_type (M->to_id) == TGL_PEER_CHAT) {
    print_chat_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
  } else {
    assert (tgl_get_peer_type (M->to_id) == TGL_PEER_ENCR_CHAT);
    print_encr_chat_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
  }
  mprintf (ev, " ");
  print_user_name (ev, M->from_id, tgl_peer_get (TLS, M->from_id));
 
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
  case tgl_message_action_chat_add_user:
    mprintf (ev, " added user ");
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
  case tgl_message_action_notify_layer:
    mprintf (ev, " updated layer to %d\n", M->action.layer);
    break;
  case tgl_message_action_typing:
    mprintf (ev, " is ");
    print_typing (ev, M->action.typing);
    break;
  default:
    assert (0);
  }
  mpop_color (ev);
  //print_end ();
}

tgl_peer_id_t last_from_id;
tgl_peer_id_t last_to_id;

void print_message (struct in_ev *ev, struct tgl_message *M) {
  assert (M);
  if (M->flags & (FLAG_MESSAGE_EMPTY | FLAG_DELETED)) {
    return;
  }
  if (!(M->flags & FLAG_CREATED)) { return; }
  if (M->service) {
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
    if (M->out) {
      mpush_color (ev, COLOR_GREEN);
      if (msg_num_mode) {
        mprintf (ev, "%lld ", M->id);
      }
      print_date (ev, M->date);
      mpop_color (ev);
      mprintf (ev, " ");
      print_user_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
      mpush_color (ev, COLOR_GREEN);
      if (M->unread) {
        mprintf (ev, " <<< ");
      } else {
        mprintf (ev, " ««« ");
      }
    } else {
      mpush_color (ev, COLOR_BLUE);
      if (msg_num_mode) {
        mprintf (ev, "%lld ", M->id);
      }
      print_date (ev, M->date);
      mpop_color (ev);
      mprintf (ev, " ");
      print_user_name (ev, M->from_id, tgl_peer_get (TLS, M->from_id));
      mpush_color (ev, COLOR_BLUE);
      if (M->unread) {
        mprintf (ev, " >>> ");
      } else {
        mprintf (ev, " »»» ");
      }
    }
  } else if (tgl_get_peer_type (M->to_id) == TGL_PEER_ENCR_CHAT) {
    tgl_peer_t *P = tgl_peer_get (TLS, M->to_id);
    assert (P);
    if (M->out) {
      mpush_color (ev, COLOR_GREEN);
      if (msg_num_mode) {
        mprintf (ev, "%lld ", M->id);
      }
      print_date (ev, M->date);
      mprintf (ev, " ");
      mpush_color (ev, COLOR_CYAN);
      mprintf (ev, " %s", P->print_name);
      mpop_color (ev);
      if (M->unread) {
        mprintf (ev, " <<< ");
      } else {
        mprintf (ev, " ««« ");
      }
    } else {
      mpush_color (ev, COLOR_BLUE);
      if (msg_num_mode) {
        mprintf (ev, "%lld ", M->id);
      }
      print_date (ev, M->date);
      mpush_color (ev, COLOR_CYAN);
      mprintf (ev, " %s", P->print_name);
      mpop_color (ev);
      if (M->unread) {
        mprintf (ev, " >>> ");
      } else {
        mprintf (ev, " »»» ");
      }
    }
  } else {
    assert (tgl_get_peer_type (M->to_id) == TGL_PEER_CHAT);
    mpush_color (ev, COLOR_MAGENTA);
    if (msg_num_mode) {
      mprintf (ev, "%lld ", M->id);
    }
    print_date (ev, M->date);
    mpop_color (ev);
    mprintf (ev, " ");
    print_chat_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
    mprintf (ev, " ");
    print_user_name (ev, M->from_id, tgl_peer_get (TLS, M->from_id));
    if ((tgl_get_peer_type (M->from_id) == TGL_PEER_USER) && (tgl_get_peer_id (M->from_id) == TLS->our_id)) {
      mpush_color (ev, COLOR_GREEN);
    } else {
      mpush_color (ev, COLOR_BLUE);
    }
    if (M->unread) {
      mprintf (ev, " >>> ");
    } else {
      mprintf (ev, " »»» ");
    }
  }
  if (tgl_get_peer_type (M->fwd_from_id) == TGL_PEER_USER) {
    mprintf (ev, "[fwd from ");
    print_user_name (ev, M->fwd_from_id, tgl_peer_get (TLS, M->fwd_from_id));
    mprintf (ev, "] ");
  }
  if (M->message && strlen (M->message)) {
    mprintf (ev, "%s", M->message);
  }
  if (M->media.type != tgl_message_media_none) {
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
  rl_callback_handler_install (get_default_prompt (), interpreter);
  //rl_attempted_completion_function = (void *) complete_text;
  rl_completion_entry_function = command_generator;
}
