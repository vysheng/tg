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

#include "include.h"
//#include "queries.h"

#include "interface.h"
#include "telegram.h"

#ifdef EVENT_V2
#include <event2/event.h>
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

tgl_peer_id_t cur_token_user (void) {
  if (cur_token_len <= 0) { return TGL_PEER_NOT_FOUND; }
  int l = cur_token_len;
  char *s = cur_token;

  char c = cur_token[cur_token_len];
  cur_token[cur_token_len] = 0;

  if (l >= 6 && !memcmp (s, "user#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    cur_token[cur_token_len] = c;
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_USER, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }
  if (l >= 8 && !memcmp (s, "user#id", 7)) {
    s += 7;    
    l -= 7;
    int r = atoi (s);
    cur_token[cur_token_len] = c;
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_USER, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }

  tgl_peer_t *P = tgl_peer_get_by_name (s); 
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
  
  if (l >= 6 && !memcmp (s, "chat#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    cur_token[cur_token_len] = c;
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_CHAT, r); }
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

  tgl_peer_t *P = tgl_peer_get_by_name (s); 
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

  tgl_peer_t *P = tgl_peer_get_by_name (s); 
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
  
  tgl_peer_t *P = tgl_peer_get_by_name (s); 
  cur_token[cur_token_len] = c;
  
  if (P) {
    return P->id;
  } else {
    return TGL_PEER_NOT_FOUND;
  }
}

static tgl_peer_t *mk_peer (tgl_peer_id_t id) {
  if (tgl_get_peer_type (id) == NOT_FOUND) { return 0; }
  tgl_peer_t *P = tgl_peer_get (id);
  return P;
}

char *get_default_prompt (void) {
  static char buf[1000];
  int l = 0;
  if (in_chat_mode) {
    tgl_peer_t *U = tgl_peer_get (chat_mode_id);
    assert (U && U->print_name);
    l += snprintf (buf + l, 999 - l, COLOR_RED "%.*s " COLOR_NORMAL, 100, U->print_name);
  }
  if (tgl_state.unread_messages || tgl_state.cur_uploading_bytes || tgl_state.cur_downloading_bytes) {
    l += snprintf (buf + l, 999 - l, COLOR_RED "[");
    int ok = 0;
    if (tgl_state.unread_messages) {
      l += snprintf (buf + l, 999 - l, "%d unread", tgl_state.unread_messages);
      ok = 1;
    }
    if (tgl_state.cur_uploading_bytes) {
      if (ok) { *(buf + l) = ' '; l ++; }
      ok = 1;
      l += snprintf (buf + l, 999 - l, "%lld%%Up", 100 * tgl_state.cur_uploaded_bytes / tgl_state.cur_uploading_bytes);
    }
    if (tgl_state.cur_downloading_bytes) {
      if (ok) { *(buf + l) = ' '; l ++; }
      ok = 1;
      l += snprintf (buf + l, 999 - l, "%lld%%Down", 100 * tgl_state.cur_downloaded_bytes / tgl_state.cur_downloading_bytes);
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
  };
};

struct command {
  char *name;
  enum command_argument args[10];
  void (*fun)(int arg_num, struct arg args[]);
};


int offline_mode;
void print_user_list_gw (void *extra, int success, int num, struct tgl_user *UL[]);
void print_msg_list_gw (void *extra, int success, int num, struct tgl_message *ML[]);
void print_dialog_list_gw (void *extra, int success, int size, tgl_peer_id_t peers[], int last_msg_id[], int unread_count[]);
void print_chat_info_gw (void *extra, int success, struct tgl_chat *C);
void print_user_info_gw (void *extra, int success, struct tgl_user *C);
void print_filename_gw (void *extra, int success, char *name);
void open_filename_gw (void *extra, int success, char *name);
void print_secret_chat_gw (void *extra, int success, struct tgl_secret_chat *E);
void print_card_gw (void *extra, int success, int size, int *card);
void print_user_gw (void *extra, int success, struct tgl_user *U);
void print_msg_gw (void *extra, int success, struct tgl_message *M);

void do_help (int arg_num, struct arg args[]) {
  assert (!arg_num);
  push_color (COLOR_YELLOW);
  printf (
      "help - prints this help\n"
      "msg <peer> Text - sends message to this peer\n"
      "contact_list - prints info about users in your contact list\n"
      "stats - just for debugging \n"
      "history <peer> [limit] - prints history (and marks it as read). Default limit = 40\n"
      "dialog_list - prints info about your dialogs\n"
      "send_photo <peer> <photo-file-name> - sends photo to peer\n"
      "send_video <peer> <video-file-name> - sends video to peer\n"
      "send_text <peer> <text-file-name> - sends text file as plain messages\n"
      "chat_info <chat> - prints info about chat\n"
      "user_info <user> - prints info about user\n"
      "fwd <user> <msg-seqno> - forward message to user. You can see message numbers starting client with -N\n"
      "rename_chat <chat> <new-name>\n"
      "load_photo/load_video/load_video_thumb <msg-seqno> - loads photo/video to download dir. You can see message numbers starting client with -N\n"
      "view_photo/view_video/view_video_thumb <msg-seqno> - loads photo/video to download dir and starts system default viewer. You can see message numbers starting client with -N\n"
      "show_license - prints contents of GPLv2\n"
      "search <peer> pattern - searches pattern in messages with peer\n"
      "global_search pattern - searches pattern in all messages\n"
      "mark_read <peer> - mark read all received messages with peer\n"
      "add_contact <phone-number> <first-name> <last-name> - tries to add contact to contact-list by phone\n"
      "create_secret_chat <user> - creates secret chat with this user\n"
      "create_group_chat <user> <chat-topic> - creates group chat with this user, add more users with chat_add_user <user>\n"
      "rename_contact <user> <first-name> <last-name> - tries to rename contact. If you have another device it will be a fight\n"
      "suggested_contacts - print info about contacts, you have max common friends\n"
      "visualize_key <secret_chat> - prints visualization of encryption key. You should compare it to your partner's one\n"
      "set <param> <param-value>. Possible <param> values are:\n"
      "\tdebug_verbosity - just as it sounds. Debug verbosity\n"
      "\tlog_level - level of logging of new events. Lower is less verbose:\n"
      "\t\tLevel 1: prints info about read messages\n"
      "\t\tLevel 2: prints line, when somebody is typing in chat\n"
      "\t\tLevel 3: prints line, when somebody changes online status\n"
      "\tmsg_num - enables/disables numeration of messages\n"
      "\talert - enables/disables alert sound notifications\n"
      "chat_with_peer <peer> - starts chat with this peer. Every command after is message to this peer. Type /exit or /quit to end this mode\n"
      );
  pop_color ();
  fflush (stdout);
}

void do_contact_list (int arg_num, struct arg args[]) {
  assert (!arg_num);
  tgl_do_update_contact_list (print_user_list_gw, 0);
}

void do_stats (int arg_num, struct arg args[]) {
  assert (!arg_num);
  static char stat_buf[1 << 15];
  tgl_print_stat (stat_buf, (1 << 15) - 1);
  printf ("%s\n", stat_buf);
  fflush (stdout);
}

void do_history (int arg_num, struct arg args[]) {
  assert (arg_num == 3);
  tgl_do_get_history_ext (args[0].P->id, args[2].num != NOT_FOUND ? args[2].num : 0, args[1].num != NOT_FOUND ? args[1].num : 40, offline_mode, print_msg_list_gw, 0);
}

void do_dialog_list (int arg_num, struct arg args[]) {
  assert (arg_num == 0);
  tgl_do_get_dialog_list (print_dialog_list_gw, 0);
}

void do_send_photo (int arg_num, struct arg args[]) {
  assert (arg_num == 2);
  tgl_do_send_photo (tgl_message_media_photo, args[0].P->id, args[1].str, 0, 0);
}

void do_send_audio (int arg_num, struct arg args[]) {
  assert (arg_num == 2);
  tgl_do_send_photo (tgl_message_media_audio, args[0].P->id, args[1].str, 0, 0);
}

void do_send_video (int arg_num, struct arg args[]) {
  assert (arg_num == 2);
  tgl_do_send_photo (tgl_message_media_video, args[0].P->id, args[1].str, 0, 0);
}

void do_send_document (int arg_num, struct arg args[]) {
  assert (arg_num == 2);
  tgl_do_send_photo (tgl_message_media_document, args[0].P->id, args[1].str, 0, 0);
}

void do_send_text (int arg_num, struct arg args[]) {
  assert (arg_num == 2);
  tgl_do_send_text (args[0].P->id, args[1].str, 0, 0);
}

void do_chat_info (int arg_num, struct arg args[]) {
  assert (arg_num == 1);
  tgl_do_get_chat_info (args[0].P->id, offline_mode, print_chat_info_gw, 0);
}

void do_user_info (int arg_num, struct arg args[]) {
  assert (arg_num == 1);
  tgl_do_get_user_info (args[0].P->id, offline_mode, print_user_info_gw, 0);
}

void do_fwd (int arg_num, struct arg args[]) {
  assert (arg_num == 2);
  tgl_do_forward_message (args[0].P->id, args[1].num, 0, 0);
}

void do_fwd_media (int arg_num, struct arg args[]) {
  assert (arg_num == 2);
  tgl_do_forward_message (args[0].P->id, args[1].num, 0, 0);
}

void do_msg (int arg_num, struct arg args[]) {
  assert (arg_num == 2);
  tgl_do_send_message (args[0].P->id, args[1].str, strlen (args[1].str), 0, 0);
}

void do_rename_chat (int arg_num, struct arg args[]) {
  assert (arg_num == 2);
  tgl_do_rename_chat (args[0].P->id, args[1].str, 0, 0);
}

#define DO_LOAD_PHOTO(tp,act,actf) \
void do_ ## act ## _ ## tp (int arg_num, struct arg args[]) { \
  assert (arg_num == 1);\
  struct tgl_message *M = tgl_message_get (args[0].num);\
  if (M && !M->service && M->media.type == tgl_message_media_ ## tp) {\
    tgl_do_load_ ## tp (&M->media.tp, actf, 0); \
  } else if (M && !M->service && M->media.type == tgl_message_media_ ## tp ## _encr) { \
    tgl_do_load_encr_video (&M->media.encr_video, actf, 0); \
  } \
}

#define DO_LOAD_PHOTO_THUMB(tp,act,actf) \
void do_ ## act ## _ ## tp ## _thumb (int arg_num, struct arg args[]) { \
  assert (arg_num == 1);\
  struct tgl_message *M = tgl_message_get (args[0].num);\
  if (M && !M->service && M->media.type == tgl_message_media_ ## tp) { \
    tgl_do_load_ ## tp ## _thumb (&M->media.tp, actf, 0); \
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

void do_add_contact (int arg_num, struct arg args[]) {
  assert (arg_num == 3);
  tgl_do_add_contact (args[0].str, strlen (args[0].str), args[1].str, strlen (args[1].str), args[2].str, strlen (args[2].str), 0, print_user_list_gw, 0);
}

void do_del_contact (int arg_num, struct arg args[]) {
  assert (arg_num == 1);
  tgl_do_del_contact (args[0].P->id, 0, 0);
}

void do_rename_contact (int arg_num, struct arg args[]) {
  assert (arg_num == 3);
  if (args[0].P->user.phone) {
    tgl_do_add_contact (args[0].P->user.phone, strlen (args[0].P->user.phone), args[1].str, strlen (args[1].str), args[2].str, strlen (args[2].str), 0, print_user_list_gw, 0);
  }
}

void do_show_license (int arg_num, struct arg args[]) {
  assert (!arg_num);
  char *b = 
#include "LICENSE.h"
  ;
  printf ("%s", b);
}

void do_search (int arg_num, struct arg args[]) {
  assert (arg_num == 5);
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
  tgl_do_msg_search (id, from, to, limit, args[4].str, print_msg_list_gw, 0);
}

void do_mark_read (int arg_num, struct arg args[]) {
  assert (arg_num == 1);
  tgl_do_mark_read (args[0].P->id, 0, 0);
}

void do_visualize_key (int arg_num, struct arg args[]) {
  assert (arg_num == 1);
  static char *colors[4] = {COLOR_GREY, COLOR_CYAN, COLOR_BLUE, COLOR_GREEN};
  static unsigned char buf[16];
  memset (buf, 0, sizeof (buf));
  tgl_peer_id_t id = args[0].P->id;
  tgl_do_visualize_key (id, buf);
  print_start ();
  int i;
  for (i = 0; i < 16; i++) {
    int x = buf[i];
    int j;
    for (j = 0; j < 4; j ++) {    
      push_color (colors[x & 3]);
      push_color (COLOR_INVERSE);
      if (!disable_colors) {
        printf ("  ");
      } else {
        switch (x & 3) {
          case 0:
            printf ("  ");
            break;
          case 1:
            printf ("--");
            break;
          case 2:
            printf ("==");
            break;
          case 3:
            printf ("||");
            break;
        }
      }
      pop_color ();
      pop_color ();
      x = x >> 2;
    }
    if (i & 1) { printf ("\n"); }
  }
  print_end ();
}

void do_create_secret_chat (int arg_num, struct arg args[]) {
  assert (arg_num == 1);
  tgl_do_create_secret_chat (args[0].P->id, print_secret_chat_gw, 0);
}

void do_chat_add_user (int arg_num, struct arg args[]) {
  assert (arg_num == 3);
  tgl_do_add_user_to_chat (args[0].P->id, args[1].P->id, args[2].num != NOT_FOUND ? args[2].num : 100, 0, 0);
}

void do_chat_del_user (int arg_num, struct arg args[]) {
  assert (arg_num == 2);
  tgl_do_del_user_from_chat (args[0].P->id, args[1].P->id, 0, 0);
}

void do_status_online (int arg_num, struct arg args[]) {
  assert (!arg_num);
  tgl_do_update_status (1, 0, 0);
}

void do_status_offline (int arg_num, struct arg args[]) {
  assert (!arg_num);
  tgl_do_update_status (0, 0, 0);
}

void do_quit (int arg_num, struct arg args[]) {
  exit (0);
}

void do_safe_quit (int arg_num, struct arg args[]) {
  safe_quit = 1;
}

void do_set (int arg_num, struct arg args[]) {
  int num = args[1].num;
  if (!strcmp (args[0].str, "debug_verbosity")) {
    tgl_set_verbosity (num); 
  } else if (!strcmp (args[0].str, "log_level")) {
    log_level = num;
  } else if (!strcmp (args[0].str, "msg_num")) {
    msg_num_mode = num;
  } else if (!strcmp (args[0].str, "alert")) {
    alert_sound = num;
  }
}

void do_chat_with_peer (int arg_num, struct arg args[]) {
  in_chat_mode = 1;
  chat_mode_id = args[0].P->id;
}

void do_delete_msg (int arg_num, struct arg args[]) {
  tgl_do_delete_msg (args[0].num, 0, 0);
}

void do_restore_msg (int arg_num, struct arg args[]) {
  tgl_do_restore_msg (args[0].num, 0, 0);
}
    
void do_create_group_chat (int arg_num, struct arg args[]) {
  assert (arg_num >= 1 && arg_num <= 1000);
  static tgl_peer_id_t ids[1000];
  int i;
  for (i = 0; i < arg_num - 1; i++) {
    ids[i] = args[i + 1].P->id;
  }

  tgl_do_create_group_chat_ex (arg_num - 1, ids, args[0].str, 0, 0);  
}

void do_chat_set_photo (int arg_num, struct arg args[]) {
  assert (arg_num == 2);
  tgl_do_set_chat_photo (args[0].P->id, args[1].str, 0, 0); 
}

void do_set_profile_photo (int arg_num, struct arg args[]) {
  assert (arg_num == 1);
  tgl_do_set_profile_photo (args[0].str, 0, 0);
}

void do_accept_secret_chat (int arg_num, struct arg args[]) {
  assert (arg_num == 1);
  tgl_do_accept_encr_chat_request (&args[0].P->encr_chat, 0, 0);
}

void do_set_ttl (int arg_num, struct arg args[]) {
  assert (arg_num == 2);
  if (args[0].P->encr_chat.state == sc_ok) {
    tgl_do_set_encr_chat_ttl (&args[0].P->encr_chat, args[1].num, 0, 0);
  }
}

void do_export_card (int arg_num, struct arg args[]) {
  assert (!arg_num);
  tgl_do_export_card (print_card_gw, 0);
}

void do_import_card (int arg_num, struct arg args[]) {
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
      tgl_do_import_card (pp, p, print_user_gw, 0);
    }
  }
}

void do_send_contact (int arg_num, struct arg args[]) {
  assert (arg_num == 4);
  tgl_do_send_contact (args[0].P->id, args[1].str, strlen (args[1].str), args[2].str, strlen (args[2].str), args[3].str, strlen (args[3].str), print_msg_gw, 0);
}

struct command commands[] = {
  {"help", {ca_none}, do_help},
  {"contact_list", {ca_none}, do_contact_list},
  {"stats", {ca_none}, do_stats},
  {"history", {ca_peer, ca_number | ca_optional, ca_number | ca_optional, ca_none}, do_history},
  {"dialog_list", {ca_none}, do_dialog_list},
  {"send_photo", {ca_peer, ca_file_name_end, ca_none}, do_send_photo},
  {"send_video", {ca_peer, ca_file_name_end, ca_none}, do_send_video},
  {"send_audio", {ca_peer, ca_file_name_end, ca_none}, do_send_audio},
  {"send_document", {ca_peer, ca_file_name_end, ca_none}, do_send_document},
  {"send_text", {ca_peer, ca_file_name_end, ca_none}, do_send_text},
  {"chat_info", {ca_chat, ca_none}, do_chat_info},
  {"user_info", {ca_user, ca_none}, do_user_info},
  {"fwd", {ca_peer, ca_number, ca_none}, do_fwd},
  {"fwd_media", {ca_peer, ca_number, ca_none}, do_fwd_media},
  {"msg", {ca_peer, ca_string_end, ca_none}, do_msg},
  {"rename_chat", {ca_peer, ca_string_end, ca_none}, do_rename_chat},
  {"load_photo", {ca_number, ca_none}, do_load_photo},
  {"view_photo", {ca_number, ca_none}, do_open_photo},
  {"load_video_thumb", {ca_number, ca_none}, do_load_video_thumb},
  {"view_video_thumb", {ca_number, ca_none}, do_open_video_thumb},
  {"load_video", {ca_number, ca_none}, do_load_video},
  {"view_video", {ca_number, ca_none}, do_open_video},
  {"load_audio", {ca_number, ca_none}, do_load_audio},
  {"view_audio", {ca_number, ca_none}, do_open_audio},
  {"load_document", {ca_number, ca_none}, do_load_document},
  {"view_document", {ca_number, ca_none}, do_open_document},
  {"load_document_thumb", {ca_number, ca_none}, do_load_document_thumb},
  {"view_document_thumb", {ca_number, ca_none}, do_open_document_thumb},
  {"add_contact", {ca_string, ca_string, ca_string, ca_none}, do_add_contact},
  {"del_contact", {ca_user, ca_none}, do_del_contact},
  {"rename_contact", {ca_user, ca_string, ca_string, ca_none}, do_rename_contact},
  {"show_license", {ca_none}, do_show_license},
  {"search", {ca_peer | ca_optional, ca_number | ca_optional, ca_number | ca_optional, ca_number | ca_optional, ca_string_end}, do_search},
  {"mark_read", {ca_peer, ca_none}, do_mark_read},
  {"visualize_key", {ca_secret_chat, ca_none}, do_visualize_key},
  {"create_secret_chat", {ca_user, ca_none}, do_create_secret_chat},
  {"chat_add_user", {ca_chat, ca_user, ca_number | ca_optional, ca_none}, do_chat_add_user},
  {"chat_del_user", {ca_chat, ca_user, ca_none}, do_chat_del_user},
  {"status_online", {ca_none}, do_status_online},
  {"status_offline", {ca_none}, do_status_offline},
  {"quit", {ca_none}, do_quit},
  {"safe_quit", {ca_none}, do_safe_quit},
  {"set", {ca_string, ca_number, ca_none}, do_set},
  {"chat_with_peer", {ca_peer, ca_none}, do_chat_with_peer},
  {"delete_msg", {ca_number, ca_none}, do_delete_msg},
  {"restore_msg", {ca_number, ca_none}, do_restore_msg},
  {"create_group_chat", {ca_string, ca_user, ca_period, ca_none}, do_create_group_chat},
  {"chat_set_photo", {ca_chat, ca_file_name_end, ca_none}, do_chat_set_photo},
  {"set_profile_photo", {ca_file_name_end, ca_none}, do_set_profile_photo},
  {"accept_secret_chat", {ca_secret_chat, ca_none}, do_accept_secret_chat},
  {"set_ttl", {ca_secret_chat, ca_number,  ca_none}, do_set_ttl},
  {"export_card", {ca_none}, do_export_card},
  {"import_card", {ca_string, ca_none}, do_import_card},
  {"send_contact", {ca_peer, ca_string, ca_string, ca_string}, do_send_contact},
  {0, {ca_none}, 0}
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
    if (op == ca_user || op == ca_chat || op == ca_secret_chat || op == ca_peer || op == ca_number) {
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
    *R = strdup (commands[index].name);
    assert (*R);
    return index;
  } else {
    *R = 0;
    return -1;
  }
}

char *command_generator (const char *text, int state) {  
  static int len, index;
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
    len = strlen (text);
    index = -1;
    
    mode = get_complete_mode ();
    command_pos = cur_token;
    command_len = cur_token_len;
  } else {
    if (index == -1) { return 0; }
  }
  
  if (mode == ca_none || mode == ca_string || mode == ca_string_end || mode == ca_number) { 
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
    index = tgl_complete_user_list (index, command_pos, command_len, &R);    
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_peer:
    index = tgl_complete_peer_list (index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_file_name:
  case ca_file_name_end:
    R = rl_filename_completion_function (command_pos, state);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_chat:
    index = tgl_complete_chat_list (index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_secret_chat:
    index = tgl_complete_encr_chat_list (index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_modifier:
    index = complete_string_list (modifiers, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_extf:
    index = tglf_extf_autocomplete (text, len, index, &R, rl_line_buffer, rl_point);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
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

void print_msg_list_gw (void *extra, int success, int num, struct tgl_message *ML[]) {
  if (!success) { return; }
  print_start ();
  int i;
  for (i = num - 1; i >= 0; i--) {
    print_message (ML[i]);
  }
  print_end ();
}

void print_msg_gw (void *extra, int success, struct tgl_message *M) {
  if (!success) { return; }
  print_start ();
  print_message (M);
  print_end ();
}

void print_user_list_gw (void *extra, int success, int num, struct tgl_user *UL[]) {
  if (!success) { return; }
  print_start ();
  int i;
  for (i = num - 1; i >= 0; i--) {
    print_user_name (UL[i]->id, (void *)UL[i]);
    printf ("\n");
  }
  print_end ();
}

void print_user_gw (void *extra, int success, struct tgl_user *U) {
  if (!success) { return; }
  print_start ();
  print_user_name (U->id, (void *)U);
  printf ("\n");
  print_end ();
}

void print_filename_gw (void *extra, int success, char *name) {
  if (!success) { return; }
  print_start ();
  printf ("Saved to %s\n", name);
  print_end ();
}

void open_filename_gw (void *extra, int success, char *name) {
  if (!success) { return; }
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

void print_chat_info_gw (void *extra, int success, struct tgl_chat *C) {
  if (!success) { 
    vlogprintf (E_NOTICE, "Failed to get chat info\n");
    return; 
  }
  print_start ();

  tgl_peer_t *U = (void *)C;
  push_color (COLOR_YELLOW);
  printf ("Chat ");
  print_chat_name (U->id, U);
  printf (" (id %d) members:\n", tgl_get_peer_id (U->id));
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
}

void print_user_info_gw (void *extra, int success, struct tgl_user *U) {
  if (!success) { return; }
  tgl_peer_t *C = (void *)U;
  print_start ();
  push_color (COLOR_YELLOW);
  printf ("User ");
  print_user_name (U->id, C);
  printf (" (#%d):\n", tgl_get_peer_id (U->id));
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

void print_secret_chat_gw (void *extra, int success, struct tgl_secret_chat *E) {
  if (!success) { return; }
  print_start ();
  push_color (COLOR_YELLOW);
  printf (" Encrypted chat ");
  print_encr_chat_name (E->id, (void *)E);
  printf (" is now in wait state\n");
  pop_color ();
  print_end ();
}

void print_dialog_list_gw (void *extra, int success, int size, tgl_peer_id_t peers[], int last_msg_id[], int unread_count[]) {
  if (!success) { return; }
  print_start ();
  push_color (COLOR_YELLOW);
  int i;
  for (i = size - 1; i >= 0; i--) {
    tgl_peer_t *UC;
    switch (tgl_get_peer_type (peers[i])) {
    case TGL_PEER_USER:
      UC = tgl_peer_get (peers[i]);
      printf ("User ");
      print_user_name (peers[i], UC);
      printf (": %d unread\n", unread_count[i]);
      break;
    case TGL_PEER_CHAT:
      UC = tgl_peer_get (peers[i]);
      printf ("Chat ");
      print_chat_name (peers[i], UC);
      printf (": %d unread\n", unread_count[i]);
      break;
    }
  }
  pop_color ();
  print_end ();
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
    tgl_do_get_history (chat_mode_id, limit, offline_mode, print_msg_list_gw, 0);
    return;
  }
  if (!strncmp (line, "/read", 5)) {
    tgl_do_mark_read (chat_mode_id, 0, 0);
    return;
  }
  if (strlen (line) > 0) {
    tgl_do_send_message (chat_mode_id, line, strlen (line), 0, 0);
  }
}

#define MAX_UNREAD_MESSAGE_COUNT 10000
struct tgl_message *unread_message_list[MAX_UNREAD_MESSAGE_COUNT];
int unread_message_count;
struct event *unread_message_event;

void print_read_list (int num, struct tgl_message *list[]) {
  int i;
  print_start ();
  for (i = 0; i < num; i++) if (list[i]) {
    tgl_peer_id_t to_id;
    if (tgl_get_peer_type (list[i]->to_id) == TGL_PEER_USER && tgl_get_peer_id (list[i]->to_id) == tgl_state.our_id) {
      to_id = list[i]->from_id;
    } else {
      to_id = list[i]->to_id;
    }
    int j;
    int c1 = 0;
    int c2 = 0;
    for (j = i; j < num; j++) if (list[j]) {
      tgl_peer_id_t end_id;
      if (tgl_get_peer_type (list[j]->to_id) == TGL_PEER_USER && tgl_get_peer_id (list[j]->to_id) == tgl_state.our_id) {
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
    push_color (COLOR_YELLOW);
    switch (tgl_get_peer_type (to_id)) {
    case TGL_PEER_USER:
      printf ("User ");
      print_user_name (to_id, tgl_peer_get (to_id));    
      break;
    case TGL_PEER_CHAT:
      printf ("Chat ");
      print_chat_name (to_id, tgl_peer_get (to_id));    
      break;
    case TGL_PEER_ENCR_CHAT:
      printf ("Secret chat ");
      print_encr_chat_name (to_id, tgl_peer_get (to_id));    
      break;
    default:
      assert (0);
    }
    printf (" marked read %d outbox and %d inbox messages\n", c1, c2);
    pop_color ();
  }
  print_end ();
}

void unread_message_alarm (evutil_socket_t fd, short what, void *arg) {
  print_read_list (unread_message_count, unread_message_list);
  unread_message_count = 0;
  event_free (unread_message_event);
  unread_message_event = 0;
}

void mark_read_upd (int num, struct tgl_message *list[]) {
  if (!binlog_read) { return; }
  if (log_level < 1) { return; }

  if (unread_message_count + num <= MAX_UNREAD_MESSAGE_COUNT) {
    memcpy (unread_message_list + unread_message_count, list, num * sizeof (void *));
    unread_message_count += num;

    if (!unread_message_event) {
      unread_message_event = evtimer_new (tgl_state.ev_base, unread_message_alarm, 0);
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
  /*
  tgl_peer_id_t to_id = list[0]->to_id;
  int ok = 1;
  int i;
  for (i = 1; i < num; i++) {
    if (tgl_cmp_peer_id (to_id, list[i]->to_id)) {
      ok = 0;
    }
  }
  print_start ();
  push_color (COLOR_YELLOW);
  if (!ok) {
    printf ("%d messages mark read\n", num);
  } else {
    printf ("%d messages mark read in ", num);
    switch (tgl_get_peer_type (to_id)) {
    case TGL_PEER_USER:
      printf (" user ");
      print_user_name (to_id, tgl_peer_get (to_id));    
      break;
    case TGL_PEER_CHAT:
      printf (" chat ");
      print_chat_name (to_id, tgl_peer_get (to_id));    
      break;
    case TGL_PEER_ENCR_CHAT:
      printf (" secret chat ");
      print_chat_name (to_id, tgl_peer_get (to_id));    
      break;
    }
    printf ("\n");
  }
  pop_color ();
  print_end ();*/
}

void type_notification_upd (struct tgl_user *U) {
  if (log_level < 2 || disable_output) { return; }
  print_start ();
  push_color (COLOR_YELLOW);
  printf ("User ");
  print_user_name (U->id, (void *)U);
  printf (" is typing\n");
  pop_color ();
  print_end ();
}

void type_in_chat_notification_upd (struct tgl_user *U, struct tgl_chat *C) {
  if (log_level < 2 || disable_output) { return; }
  print_start ();
  push_color (COLOR_YELLOW);
  printf ("User ");
  print_user_name (U->id, (void *)U);
  printf (" is typing in chat ");
  print_chat_name (C->id, (void *)C);
  printf ("\n");
  pop_color ();
  print_end ();
}


void print_message_gw (struct tgl_message *M) {
  #ifdef USE_LUA
    lua_new_msg (M);
  #endif
  if (disable_output) { return; }
  if (!binlog_read) { return; }
  print_start ();
  print_message (M);
  print_end ();
}

void our_id_gw (int id) {
  #ifdef USE_LUA
    lua_our_id (id);
  #endif
}

void print_peer_updates (int flags) {
  if (flags & TGL_UPDATE_PHONE) {
    printf (" phone");
  }
  if (flags & TGL_UPDATE_CONTACT) {
    printf (" contact");
  }
  if (flags & TGL_UPDATE_PHOTO) {
    printf (" photo");
  }
  if (flags & TGL_UPDATE_BLOCKED) {
    printf (" blocked");
  }
  if (flags & TGL_UPDATE_REAL_NAME) {
    printf (" name");
  }
  if (flags & TGL_UPDATE_NAME) {
    printf (" contact_name");
  }
  if (flags & TGL_UPDATE_REQUESTED) {
    printf (" status");
  }
  if (flags & TGL_UPDATE_WORKING) {
    printf (" status");
  }
  if (flags & TGL_UPDATE_FLAGS) {
    printf (" flags");
  }
  if (flags & TGL_UPDATE_TITLE) {
    printf (" title");
  }
  if (flags & TGL_UPDATE_ADMIN) {
    printf (" admin");
  }
  if (flags & TGL_UPDATE_MEMBERS) {
    printf (" members");
  }
  if (flags & TGL_UPDATE_ACCESS_HASH) {
    printf (" access_hash");
  }
}

void user_update_gw (struct tgl_user *U, unsigned flags) {
  #ifdef USE_LUA
    lua_user_update (U, flags);
  #endif
  
  if (disable_output) { return; }
  if (!binlog_read) { return; }

  if (!(flags & TGL_UPDATE_CREATED)) {
    print_start ();
    push_color (COLOR_YELLOW);
    printf ("User ");
    print_user_name (U->id, (void *)U);
    if (!(flags & TGL_UPDATE_DELETED)) {
      printf (" updated");
      print_peer_updates (flags);
    } else {
      printf (" deleted");
    }
    printf ("\n");
    pop_color ();
    print_end ();
  }
}

void chat_update_gw (struct tgl_chat *U, unsigned flags) {
  #ifdef USE_LUA
    lua_chat_update (U, flags);
  #endif
  
  if (disable_output) { return; }
  if (!binlog_read) { return; }

  if (!(flags & TGL_UPDATE_CREATED)) {
    print_start ();
    push_color (COLOR_YELLOW);
    printf ("Chat ");
    print_chat_name (U->id, (void *)U);
    if (!(flags & TGL_UPDATE_DELETED)) {
      printf (" updated");
      print_peer_updates (flags);
    } else {
      printf (" deleted");
    }
    printf ("\n");
    pop_color ();
    print_end ();
  }
}

void secret_chat_update_gw (struct tgl_secret_chat *U, unsigned flags) {
  #ifdef USE_LUA
    lua_secret_chat_update (U, flags);
  #endif
  
  if (disable_output) { return; }
  if (!binlog_read) { return; }

  if ((flags & TGL_UPDATE_WORKING) || (flags & TGL_UPDATE_DELETED)) {
    write_secret_chat_file ();
  }
  if ((flags & TGL_UPDATE_REQUESTED) && !disable_auto_accept)  {
    tgl_do_accept_encr_chat_request (U, 0, 0);
  }

  if (!(flags & TGL_UPDATE_CREATED)) {
    print_start ();
    push_color (COLOR_YELLOW);
    printf ("Secret chat ");
    print_encr_chat_name (U->id, (void *)U);
    if (!(flags & TGL_UPDATE_DELETED)) {
      printf (" updated");
      print_peer_updates (flags);
    } else {
      printf (" deleted");
    }
    printf ("\n");
    pop_color ();
    print_end ();
  }
}

void print_card_gw (void *extra, int success, int size, int *card) {
  assert (success);
  print_start ();
  printf ("Card: ");
  int i;
  for (i = 0; i < size; i++) {
    printf ("%08x%c", card[i], i == size - 1 ? '\n' : ':');
  }
  print_end ();
}

void callback_extf (void *extra, int success, char *buf) {
  print_start ();
  printf ("%s\n", buf);
  print_end ();
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


void interpreter (char *line UU) {
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
    in_readline = 0;
    return; 
  }
  if (line && *line) {
    add_history (line);
  }
  
  if (*line == '(') { 
    tgl_do_send_extf (line, strlen (line), callback_extf, 0);
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
  void (*fun)(int, struct arg[]) = command->fun;
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
        fun (args_num, args);
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
        fun (args_num, args);
        break;
      }
    }

    char *save = line_ptr;
    next_token ();

    if (period && cur_token_end_str) {
      fun (args_num, args);
      break;
    }

    if (op == ca_user || op == ca_chat || op == ca_secret_chat || op == ca_peer || op == ca_number) {
      if (cur_token_quoted) {
        if (opt) {
          if (op != ca_number) {
            args[args_num ++].P = 0;
          } else {
            args[args_num ++].num = NOT_FOUND;
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
            if (op != ca_number) {
              args[args_num ++].P = 0;
            } else {
              args[args_num ++].num = NOT_FOUND;
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
        default:
          assert (0);
        }

        if (opt && !ok) {
          if (op != ca_number) {
            args[args_num ++].P = 0;
          } else {
            args[args_num ++].num = NOT_FOUND;
          }
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

int readline_active;
void rprintf (const char *format, ...) {
  print_start ();
  va_list ap;
  va_start (ap, format);
  vfprintf (stdout, format, ap);
  va_end (ap);
  print_end();
}

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
  print_start ();
  int *ptr = in_ptr;
  while (ptr < in_end) { printf (" %08x", *(ptr ++)); }
  printf ("\n");
  print_end (); 
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

void print_media (struct tgl_message_media *M) {
  assert (M);
  switch (M->type) {
    case tgl_message_media_none:
      return;
    case tgl_message_media_photo:
      if (M->photo.caption && strlen (M->photo.caption)) {
        printf ("[photo %s]", M->photo.caption);
      } else {
        printf ("[photo]");
      }
      return;
    case tgl_message_media_video:
      if (M->video.mime_type) {
        printf ("[video: type %s]", M->video.mime_type);
      } else {
        printf ("[video]");
      }
      return;
    case tgl_message_media_audio:
      if (M->audio.mime_type) {
        printf ("[audio: type %s]", M->audio.mime_type);
      } else {
        printf ("[audio]");
      }
      return;
    case tgl_message_media_document:
      if (M->document.mime_type && M->document.caption) {
        printf ("[document %s: type %s]", M->document.caption, M->document.mime_type);
      } else {
        printf ("[document]");
      }
      return;
    case tgl_message_media_photo_encr:
      printf ("[photo]");
      return;
    case tgl_message_media_video_encr:
      if (M->encr_video.mime_type) {
        printf ("[video: type %s]", M->encr_video.mime_type);
      } else {
        printf ("[video]");
      }
      return;
    case tgl_message_media_audio_encr:
      if (M->encr_audio.mime_type) {
        printf ("[audio: type %s]", M->encr_audio.mime_type);
      } else {
        printf ("[audio]");
      }
      return;
    case tgl_message_media_document_encr:
      if (M->encr_document.mime_type && M->encr_document.file_name) {
        printf ("[document %s: type %s]", M->encr_document.file_name, M->encr_document.mime_type);
      } else {
        printf ("[document]");
      }
      return;
    case tgl_message_media_geo:
      printf ("[geo] https://maps.google.com/?q=%.6lf,%.6lf", M->geo.latitude, M->geo.longitude);
      return;
    case tgl_message_media_contact:
      printf ("[contact] ");
      push_color (COLOR_RED);
      printf ("%s %s ", M->first_name, M->last_name);
      pop_color ();
      printf ("%s", M->phone);
      return;
    case tgl_message_media_unsupported:
      printf ("[unsupported]");
      return;
    default:
      printf ("x = %d\n", M->type);
      assert (0);
  }
}

int unknown_user_list_pos;
int unknown_user_list[1000];

void print_user_name (tgl_peer_id_t id, tgl_peer_t *U) {
  assert (tgl_get_peer_type (id) == TGL_PEER_USER);
  push_color (COLOR_RED);
  if (!U) {
    printf ("user#%d", tgl_get_peer_id (id));
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
      push_color (COLOR_REDB);
    }
    if ((U->flags & FLAG_DELETED)) {
      printf ("deleted user#%d", tgl_get_peer_id (id));
    } else if (!(U->flags & FLAG_CREATED)) {
      printf ("empty user#%d", tgl_get_peer_id (id));
    } else if (!U->user.first_name || !strlen (U->user.first_name)) {
      printf ("%s", U->user.last_name);
    } else if (!U->user.last_name || !strlen (U->user.last_name)) {
      printf ("%s", U->user.first_name);
    } else {
      printf ("%s %s", U->user.first_name, U->user.last_name); 
    }
    if (U->flags & (FLAG_USER_SELF | FLAG_USER_CONTACT)) {
      pop_color ();
    }
  }
  pop_color ();
}

void print_chat_name (tgl_peer_id_t id, tgl_peer_t *C) {
  assert (tgl_get_peer_type (id) == TGL_PEER_CHAT);
  push_color (COLOR_MAGENTA);
  if (!C) {
    printf ("chat#%d", tgl_get_peer_id (id));
  } else {
    printf ("%s", C->chat.title);
  }
  pop_color ();
}

void print_encr_chat_name (tgl_peer_id_t id, tgl_peer_t *C) {
  assert (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT);
  push_color (COLOR_MAGENTA);
  if (!C) {
    printf ("encr_chat#%d", tgl_get_peer_id (id));
  } else {
    printf ("%s", C->print_name);
  }
  pop_color ();
}

void print_encr_chat_name_full (tgl_peer_id_t id, tgl_peer_t *C) {
  assert (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT);
  push_color (COLOR_MAGENTA);
  if (!C) {
    printf ("encr_chat#%d", tgl_get_peer_id (id));
  } else {
    printf ("%s", C->print_name);
  }
  pop_color ();
}

static char *monthes[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
void print_date (long t) {
  struct tm *tm = localtime ((void *)&t);
  if (time (0) - t < 12 * 60 * 60) {
    printf ("[%02d:%02d] ", tm->tm_hour, tm->tm_min);
  } else {
    printf ("[%02d %s]", tm->tm_mday, monthes[tm->tm_mon]);
  }
}

void print_date_full (long t) {
  struct tm *tm = localtime ((void *)&t);
  printf ("[%04d/%02d/%02d %02d:%02d:%02d]", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

void print_service_message (struct tgl_message *M) {
  assert (M);
  //print_start ();
  push_color (COLOR_GREY);
  
  push_color (COLOR_MAGENTA);
  if (msg_num_mode) {
    printf ("%lld ", M->id);
  }
  print_date (M->date);
  pop_color ();
  printf (" ");
  if (tgl_get_peer_type (M->to_id) == TGL_PEER_CHAT) {
    print_chat_name (M->to_id, tgl_peer_get (M->to_id));
  } else {
    assert (tgl_get_peer_type (M->to_id) == TGL_PEER_ENCR_CHAT);
    print_encr_chat_name (M->to_id, tgl_peer_get (M->to_id));
  }
  printf (" ");
  print_user_name (M->from_id, tgl_peer_get (M->from_id));
 
  switch (M->action.type) {
  case tgl_message_action_none:
    printf ("\n");
    break;
  case tgl_message_action_geo_chat_create:
    printf ("Created geo chat\n");
    break;
  case tgl_message_action_geo_chat_checkin:
    printf ("Checkin in geochat\n");
    break;
  case tgl_message_action_chat_create:
    printf (" created chat %s. %d users\n", M->action.title, M->action.user_num);
    break;
  case tgl_message_action_chat_edit_title:
    printf (" changed title to %s\n", 
      M->action.new_title);
    break;
  case tgl_message_action_chat_edit_photo:
    printf (" changed photo\n");
    break;
  case tgl_message_action_chat_delete_photo:
    printf (" deleted photo\n");
    break;
  case tgl_message_action_chat_add_user:
    printf (" added user ");
    print_user_name (tgl_set_peer_id (TGL_PEER_USER, M->action.user), tgl_peer_get (tgl_set_peer_id (TGL_PEER_USER, M->action.user)));
    printf ("\n");
    break;
  case tgl_message_action_chat_delete_user:
    printf (" deleted user ");
    print_user_name (tgl_set_peer_id (TGL_PEER_USER, M->action.user), tgl_peer_get (tgl_set_peer_id (TGL_PEER_USER, M->action.user)));
    printf ("\n");
    break;
  case tgl_message_action_set_message_ttl:
    printf (" set ttl to %d seconds. Unsupported yet\n", M->action.ttl);
    break;
  case tgl_message_action_read_messages:
    printf (" %d messages marked read\n", M->action.read_cnt);
    break;
  case tgl_message_action_delete_messages:
    printf (" %d messages deleted\n", M->action.delete_cnt);
    break;
  case tgl_message_action_screenshot_messages:
    printf (" %d messages screenshoted\n", M->action.screenshot_cnt);
    break;
  case tgl_message_action_flush_history:
    printf (" cleared history\n");
    break;
  case tgl_message_action_notify_layer:
    printf (" updated layer to %d\n", M->action.layer);
    break;
  default:
    assert (0);
  }
  pop_color ();
  //print_end ();
}

tgl_peer_id_t last_from_id;
tgl_peer_id_t last_to_id;

void print_message (struct tgl_message *M) {
  assert (M);
  if (M->flags & (FLAG_MESSAGE_EMPTY | FLAG_DELETED)) {
    return;
  }
  if (!(M->flags & FLAG_CREATED)) { return; }
  if (M->service) {
    print_service_message (M);
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
      push_color (COLOR_GREEN);
      if (msg_num_mode) {
        printf ("%lld ", M->id);
      }
      print_date (M->date);
      pop_color ();
      printf (" ");
      print_user_name (M->to_id, tgl_peer_get (M->to_id));
      push_color (COLOR_GREEN);
      if (M->unread) {
        printf (" <<< ");
      } else {
        printf (" ««« ");
      }
    } else {
      push_color (COLOR_BLUE);
      if (msg_num_mode) {
        printf ("%lld ", M->id);
      }
      print_date (M->date);
      pop_color ();
      printf (" ");
      print_user_name (M->from_id, tgl_peer_get (M->from_id));
      push_color (COLOR_BLUE);
      if (M->unread) {
        printf (" >>> ");
      } else {
        printf (" »»» ");
      }
      if (alert_sound) {
        play_sound();
      }
    }
  } else if (tgl_get_peer_type (M->to_id) == TGL_PEER_ENCR_CHAT) {
    tgl_peer_t *P = tgl_peer_get (M->to_id);
    assert (P);
    if (M->out) {
      push_color (COLOR_GREEN);
      if (msg_num_mode) {
        printf ("%lld ", M->id);
      }
      print_date (M->date);
      printf (" ");
      push_color (COLOR_CYAN);
      printf (" %s", P->print_name);
      pop_color ();
      if (M->unread) {
        printf (" <<< ");
      } else {
        printf (" ««« ");
      }
    } else {
      push_color (COLOR_BLUE);
      if (msg_num_mode) {
        printf ("%lld ", M->id);
      }
      print_date (M->date);
      push_color (COLOR_CYAN);
      printf (" %s", P->print_name);
      pop_color ();
      if (M->unread) {
        printf (" >>> ");
      } else {
        printf (" »»» ");
      }
      if (alert_sound) {
        play_sound();
      }
    }
  } else {
    assert (tgl_get_peer_type (M->to_id) == TGL_PEER_CHAT);
    push_color (COLOR_MAGENTA);
    if (msg_num_mode) {
      printf ("%lld ", M->id);
    }
    print_date (M->date);
    pop_color ();
    printf (" ");
    print_chat_name (M->to_id, tgl_peer_get (M->to_id));
    printf (" ");
    print_user_name (M->from_id, tgl_peer_get (M->from_id));
    if ((tgl_get_peer_type (M->from_id) == TGL_PEER_USER) && (tgl_get_peer_id (M->from_id) == tgl_state.our_id)) {
      push_color (COLOR_GREEN);
    } else {
      push_color (COLOR_BLUE);
    }
    if (M->unread) {
      printf (" >>> ");
    } else {
      printf (" »»» ");
    }
  }
  if (tgl_get_peer_type (M->fwd_from_id) == TGL_PEER_USER) {
    printf ("[fwd from ");
    print_user_name (M->fwd_from_id, tgl_peer_get (M->fwd_from_id));
    printf ("] ");
  }
  if (M->message && strlen (M->message)) {
    printf ("%s", M->message);
  }
  if (M->media.type != tgl_message_media_none) {
    print_media (&M->media);
  }
  pop_color ();
  assert (!color_stack_pos);
  printf ("\n");
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
