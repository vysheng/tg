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

char *end_string_token (int *l) {
  while (*line_ptr == ' ') { line_ptr ++; }
  if (!*line_ptr) { 
    *l = 0;
    return 0;
  }
  char *s = line_ptr;
  while (*line_ptr) { line_ptr ++; }
  while (*line_ptr == ' ' || !*line_ptr) { line_ptr --; }
  line_ptr ++;
  
  *l = line_ptr - s;
  return s;
}

char *next_token (int *l) {
  while (*line_ptr == ' ') { line_ptr ++; }
  if (!*line_ptr) { 
    *l = 0;
    return 0;
  }
  int neg = 0;
  char *s = line_ptr;
  int in_str = 0;
  while (*line_ptr && (*line_ptr != ' ' || neg || in_str)) {
    line_ptr++;
  }
  *l = line_ptr - s;
  return s;
}

#define NOT_FOUND (int)0x80000000
tgl_peer_id_t TGL_PEER_NOT_FOUND = {.id = NOT_FOUND};

long long next_token_int (void) {
  int l;
  char *s = next_token (&l);
  if (!s) { return NOT_FOUND; }
  char *r;
  long long x = strtoll (s, &r, 10);
  if (r == s + l) { 
    return x;
  } else {
    return NOT_FOUND;
  }
}

tgl_peer_id_t next_token_user (void) {
  int l;
  char *s = next_token (&l);
  if (!s) { return TGL_PEER_NOT_FOUND; }

  if (l >= 6 && !memcmp (s, "user#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_USER, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }
  if (l >= 8 && !memcmp (s, "user#id", 7)) {
    s += 7;    
    l -= 7;
    int r = atoi (s);
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_USER, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }

  char c = s[l];
  s[l] = 0;
  tgl_peer_t *P = tgl_peer_get_by_name (s); 
  s[l] = c;
  
  if (P && tgl_get_peer_type (P->id) == TGL_PEER_USER) {
    return P->id;
  } else {
    return TGL_PEER_NOT_FOUND;
  }
}

tgl_peer_id_t next_token_chat (void) {
  int l;
  char *s = next_token (&l);
  if (!s) { return TGL_PEER_NOT_FOUND; }
  
  if (l >= 6 && !memcmp (s, "chat#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_CHAT, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }
  
  if (l >= 8 && !memcmp (s, "chat#id", 7)) {
    s += 7;    
    l -= 7;
    int r = atoi (s);
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_CHAT, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }

  char c = s[l];
  s[l] = 0;
  tgl_peer_t *P = tgl_peer_get_by_name (s); 
  s[l] = c;
  
  if (P && tgl_get_peer_type (P->id) == TGL_PEER_CHAT) {
    return P->id;
  } else {
    return TGL_PEER_NOT_FOUND;
  }
}

tgl_peer_id_t next_token_encr_chat (void) {
  int l;
  char *s = next_token (&l);
  if (!s) { return TGL_PEER_NOT_FOUND; }

  char c = s[l];
  s[l] = 0;
  tgl_peer_t *P = tgl_peer_get_by_name (s); 
  s[l] = c;
  
  if (P && tgl_get_peer_type (P->id) == TGL_PEER_ENCR_CHAT) {
    return P->id;
  } else {
    return TGL_PEER_NOT_FOUND;
  }
}

tgl_peer_id_t next_token_peer (void) {
  int l;
  char *s = next_token (&l);
  if (!s) { return TGL_PEER_NOT_FOUND; }
  
  if (l >= 6 && !memcmp (s, "user#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_USER, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }
  if (l >= 6 && !memcmp (s, "chat#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    if (r >= 0) { return tgl_set_peer_id (TGL_PEER_CHAT, r); }
    else { return TGL_PEER_NOT_FOUND; }
  }
  
  char c = s[l];
  s[l] = 0;
  tgl_peer_t *P = tgl_peer_get_by_name (s); 
  s[l] = c;
  
  if (P) {
    return P->id;
  } else {
    return TGL_PEER_NOT_FOUND;
  }
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
  ca_extf
};

struct command {
  char *name;
  enum command_argument args[10];
};

struct command commands[] = {
  {"help", {ca_none}},
  {"contact_list", {ca_none}},
  {"stats", {ca_none}},
  {"history", {ca_peer, ca_none, ca_number, ca_number}},
  {"dialog_list", {ca_none}},
  {"send_photo", {ca_peer, ca_file_name_end}},
  {"send_video", {ca_peer, ca_file_name_end}},
  {"send_audio", {ca_peer, ca_file_name_end}},
  {"send_document", {ca_peer, ca_file_name_end}},
  {"send_text", {ca_peer, ca_file_name_end}},
  {"chat_info", {ca_chat, ca_none}},
  {"user_info", {ca_user, ca_none}},
  {"fwd", {ca_peer, ca_number, ca_none}},
  {"fwd_media", {ca_peer, ca_number, ca_none}},
  {"msg", {ca_peer, ca_string_end}},
  {"rename_chat", {ca_peer, ca_string_end}},
  {"load_photo", {ca_number, ca_none}},
  {"view_photo", {ca_number, ca_none}},
  {"load_video_thumb", {ca_number, ca_none}},
  {"view_video_thumb", {ca_number, ca_none}},
  {"load_video", {ca_number, ca_none}},
  {"view_video", {ca_number, ca_none}},
  {"load_audio", {ca_number, ca_none}},
  {"view_audio", {ca_number, ca_none}},
  {"load_document", {ca_number, ca_none}},
  {"view_document", {ca_number, ca_none}},
  {"load_document_thumb", {ca_number, ca_none}},
  {"view_document_thumb", {ca_number, ca_none}},
  {"add_contact", {ca_string, ca_string, ca_string, ca_none}},
  {"del_contact", {ca_user, ca_none}},
  {"rename_contact", {ca_user, ca_string, ca_string, ca_none}},
  {"show_license", {ca_none}},
  {"search", {ca_peer, ca_string_end}},
  {"mark_read", {ca_peer, ca_none}},
  {"visualize_key", {ca_secret_chat, ca_none}},
  {"create_secret_chat", {ca_user, ca_none}},
  {"global_search", {ca_string_end}},
  {"chat_add_user", {ca_chat, ca_user, ca_none}},
  {"chat_del_user", {ca_chat, ca_user, ca_none}},
  {"status_online", {ca_none}},
  {"status_offline", {ca_none}},
  {"quit", {ca_none}},
  {"safe_quit", {ca_none}},
  {"set", {ca_string, ca_string, ca_none}},
  {"chat_with_peer", {ca_peer, ca_none}},
  {"delete_msg", {ca_number, ca_none}},
  {"restore_msg", {ca_number, ca_none}},
  {"create_group_chat", {ca_user, ca_string_end}},
  {"chat_set_photo", {ca_chat, ca_file_name_end}},
  {"set_profile_photo", {ca_file_name_end}},
  {"accept_secret_chat", {ca_secret_chat, ca_none}},
  {"export_card", {ca_none}},
  {"import_card", {ca_string, ca_none}},
  {"send_contact", {ca_peer, ca_string, ca_string, ca_string}},
  {0, {ca_none}}
};


enum command_argument get_complete_mode (void) {
  line_ptr = rl_line_buffer;
  int l = 0;
  char *r = next_token (&l);
  if (!r) { return ca_command; }
  if (*r == '(') { return ca_extf; }
  while (r && r[0] == '[' && r[l - 1] == ']') {
    r = next_token (&l);
    if (!r) { return ca_command; }
  }
  if (*r == '[' && !r[l]) {
    return ca_modifier;
  }
 
  if (!*line_ptr) { return ca_command; }
  struct command *command = commands;
  int n = 0;
  struct tgl_command;
  while (command->name) {
    if (is_same_word (r, l, command->name)) {
      break;
    }
    n ++;
    command ++;
  }
  enum command_argument *flags = command->args;
  while (1) {
    if (!next_token (&l) || !*line_ptr) {
      return *flags;
    }
    if (*flags == ca_none) {
      return ca_none;
    }
    if (*flags == ca_string_end) {
      return ca_string_end;
    }
    if (*flags == ca_file_name_end) {
      return ca_file_name_end;
    }
    flags ++;
    if (*flags == ca_period) {
      flags --;
    }
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

  if (in_chat_mode) {
    char *R = 0;
    index = complete_string_list (in_chat_commands, index, text, rl_point, &R);
    return R;
  }
 
  char c = 0;
  if (!state) {
    len = strlen (text);
    index = -1;
    
    c = rl_line_buffer[rl_point];
    rl_line_buffer[rl_point] = 0;
    mode = get_complete_mode ();
  } else {
    if (index == -1) { return 0; }
  }
  
  if (mode == ca_none || mode == ca_string || mode == ca_string_end || mode == ca_number) { 
    if (c) { rl_line_buffer[rl_point] = c; }
    return 0; 
  }

  char *R = 0;
  switch (mode) {
  case ca_command:
    index = complete_command_list (index, text, len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_user:
    index = tgl_complete_user_list (index, text, len, &R);    
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_peer:
    index = tgl_complete_peer_list (index, text, len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_file_name:
  case ca_file_name_end:
    R = rl_filename_completion_function (text, state);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_chat:
    index = tgl_complete_chat_list (index, text, len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_secret_chat:
    index = tgl_complete_encr_chat_list (index, text, len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_modifier:
    index = complete_string_list (modifiers, index, text, len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_extf:
    index = tglf_extf_autocomplete (text, len, index, &R, rl_line_buffer, rl_point);
    return R;
  default:
    if (c) { rl_line_buffer[rl_point] = c; }
    return 0;
  }
}

char **complete_text (char *text, int start UU, int end UU) {
  return (char **) rl_completion_matches (text, command_generator);
}

int offline_mode;
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

  int l;
  char *command;
  while (1) {
    command = next_token (&l);
    if (!command) { in_readline = 0; return; }
    if (*command == '[' && command[l - 1] == ']') {
      work_modifier (command, l);
    } else {
      break;
    }
  }

  int _;
  char *save = line_ptr;
  int ll = l;
  char *cs = command;
  for (_ = 0; _ < count; _ ++) {
    line_ptr = save;
    l = ll;
    command = cs;
#define IS_WORD(s) is_same_word (command, l, (s))
#define RET in_readline = 0; return; 

  tgl_peer_id_t id;
#define GET_PEER \
  id = next_token_peer (); \
  if (!tgl_cmp_peer_id (id, TGL_PEER_NOT_FOUND)) { \
    printf ("Bad user/chat id\n"); \
    RET; \
  } 
#define GET_PEER_USER \
  id = next_token_user (); \
  if (!tgl_cmp_peer_id (id, TGL_PEER_NOT_FOUND)) { \
    printf ("Bad user id\n"); \
    RET; \
  } 
#define GET_PEER_CHAT \
  id = next_token_chat (); \
  if (!tgl_cmp_peer_id (id, TGL_PEER_NOT_FOUND)) { \
    printf ("Bad chat id\n"); \
    RET; \
  } 
#define GET_PEER_ENCR_CHAT \
  id = next_token_encr_chat (); \
  if (!tgl_cmp_peer_id (id, TGL_PEER_NOT_FOUND)) { \
    printf ("Bad encr_chat id\n"); \
    RET; \
  } 

  if (command && *command == '(') {
    tgl_do_send_extf (line, strlen (line), callback_extf, 0);
  } else if (IS_WORD ("contact_list")) {
    tgl_do_update_contact_list (print_user_list_gw, 0);
  } else if (IS_WORD ("dialog_list")) {
    tgl_do_get_dialog_list (print_dialog_list_gw, 0);
  } else if (IS_WORD ("stats")) {
    static char stat_buf[1 << 15];
    tgl_print_stat (stat_buf, (1 << 15) - 1);
    printf ("%s\n", stat_buf);
    fflush (stdout);
  } else if (IS_WORD ("msg")) {
    GET_PEER;
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty message\n");
      RET;
    }
    tgl_do_send_message (id, s, strlen (s), 0, 0);
  } else if (IS_WORD ("rename_chat")) {
    GET_PEER_CHAT;
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty new name\n");
      RET;
    }
    tgl_do_rename_chat (id, s, 0, 0);
  } else if (IS_WORD ("send_photo")) {
    GET_PEER;
    int t;
    char *s = end_string_token (&t);
    if (!s) {
      printf ("Empty file name\n");
      RET;
    }
    char *d = strndup (s, t);
    assert (d);
    tgl_do_send_photo (tgl_message_media_photo, id, d, 0, 0);
  } else if (IS_WORD ("chat_set_photo")) {
    GET_PEER_CHAT;
    int t;
    char *s = end_string_token (&t);
    if (!s) {
      printf ("Empty file name\n");
      RET;
    }
    char *d = strndup (s, t);
    assert (d);
    tgl_do_set_chat_photo (id, d, 0, 0);
  } else if (IS_WORD ("set_profile_photo")) {
    int t;
    char *s = end_string_token (&t);
    if (!s) {
      printf ("Empty file name\n");
      RET;
    }
    char *d = strndup (s, t);
    assert (d);
    tgl_do_set_profile_photo (d, 0, 0);
  } else if (IS_WORD("send_video")) {
    GET_PEER;
    int t;
    char *s = end_string_token (&t);
    if (!s) {
      printf ("Empty file name\n");
      RET;
    }
    char *d = strndup (s, t);
    assert (d);
    tgl_do_send_photo (tgl_message_media_video, id, d, 0, 0);
  } else if (IS_WORD ("send_text")) {
    GET_PEER;
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty file name\n");
      RET;
    }
    char *d = strndup (s, t);
    assert (d);
    tgl_do_send_text (id, d, 0, 0);
  } else if (IS_WORD ("fwd")) {
    GET_PEER;
    int num = next_token_int ();
    if (num == NOT_FOUND || num <= 0) {
      printf ("Bad msg id\n");
      RET;
    }
    tgl_do_forward_message (id, num, 0, 0);
  } else if (IS_WORD ("fwd_media")) {
    GET_PEER;
    int num = next_token_int ();
    if (num == NOT_FOUND || num <= 0) {
      printf ("Bad msg id\n");
      RET;
    }
    tgl_do_forward_media (id, num, 0, 0);
  } else if (IS_WORD ("load_photo")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct tgl_message *M = tgl_message_get (num);
    if (M && !M->service && M->media.type == tgl_message_media_photo) {
      tgl_do_load_photo (&M->media.photo, print_filename_gw, 0);
    } else if (M && !M->service && M->media.type == tgl_message_media_photo_encr) {
      tgl_do_load_encr_video (&M->media.encr_video, print_filename_gw, 0); // this is not a bug. 
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("view_photo")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct tgl_message *M = tgl_message_get (num);
    if (M && !M->service && M->media.type == tgl_message_media_photo) {
      tgl_do_load_photo (&M->media.photo, open_filename_gw, 0);
    } else if (M && !M->service && M->media.type == tgl_message_media_photo_encr) {
      tgl_do_load_encr_video (&M->media.encr_video, open_filename_gw, 0); // this is not a bug. 
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("load_video_thumb")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct tgl_message *M = tgl_message_get (num);
    if (M && !M->service && M->media.type == tgl_message_media_video) {
      tgl_do_load_video_thumb (&M->media.video, print_filename_gw, 0);
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("view_video_thumb")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct tgl_message *M = tgl_message_get (num);
    if (M && !M->service && M->media.type == tgl_message_media_video) {
      tgl_do_load_video_thumb (&M->media.video, open_filename_gw, 0);
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("load_video")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct tgl_message *M = tgl_message_get (num);
    if (M && !M->service && M->media.type == tgl_message_media_video) {
      tgl_do_load_video (&M->media.video, print_filename_gw, 0);
    } else if (M && !M->service && M->media.type == tgl_message_media_video_encr) {
      tgl_do_load_encr_video (&M->media.encr_video, print_filename_gw, 0);
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("view_video")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct tgl_message *M = tgl_message_get (num);
    if (M && !M->service && M->media.type == tgl_message_media_video) {
      tgl_do_load_video (&M->media.video, open_filename_gw, 0);
    } else if (M && !M->service && M->media.type == tgl_message_media_video_encr) {
      tgl_do_load_encr_video (&M->media.encr_video, open_filename_gw, 0);
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("chat_info")) {
    GET_PEER_CHAT;
    tgl_do_get_chat_info (id, offline_mode, print_chat_info_gw, 0);
  } else if (IS_WORD ("user_info")) {
    GET_PEER_USER;
    tgl_do_get_user_info (id, offline_mode, print_user_info_gw, 0);
  } else if (IS_WORD ("history")) {
    GET_PEER;
    int limit = next_token_int ();
    tgl_do_get_history (id, limit > 0 ? limit : 40, offline_mode, print_msg_list_gw, 0);
  } else if (IS_WORD ("chat_add_user")) {
    GET_PEER_CHAT;    
    tgl_peer_id_t chat_id = id;
    GET_PEER_USER;
    tgl_do_add_user_to_chat (chat_id, id, 100, 0, 0);
  } else if (IS_WORD ("chat_del_user")) {
    GET_PEER_CHAT;    
    tgl_peer_id_t chat_id = id;
    GET_PEER_USER;
    tgl_do_del_user_from_chat (chat_id, id, 0, 0);
  } else if (IS_WORD ("add_contact")) {
    int phone_len, first_name_len, last_name_len;
    char *phone, *first_name, *last_name;
    phone = next_token (&phone_len);
    if (!phone) {
      printf ("No phone number found\n");
      RET;
    }
    first_name = next_token (&first_name_len);
    if (!first_name_len) {
      printf ("No first name found\n");
      RET;
    }
    last_name = next_token (&last_name_len);
    if (!last_name_len) {
      printf ("No last name found\n");
      RET;
    }
    tgl_do_add_contact (phone, phone_len, first_name, first_name_len, last_name, last_name_len, 0, print_user_list_gw, 0);
  } else if (IS_WORD ("del_contact")) {
    GET_PEER_USER;
    tgl_do_del_contact (id, 0, 0);
  } else if (IS_WORD ("send_contact")) {
    GET_PEER;
    int phone_len, first_name_len, last_name_len;
    char *phone, *first_name, *last_name;
    phone = next_token (&phone_len);
    if (!phone) {
      printf ("No phone number found\n");
      RET;
    }
    first_name = next_token (&first_name_len);
    if (!first_name_len) {
      printf ("No first name found\n");
      RET;
    }
    last_name = next_token (&last_name_len);
    if (!last_name_len) {
      printf ("No last name found\n");
      RET;
    }
    tgl_do_send_contact (id, phone, phone_len, first_name, first_name_len, last_name, last_name_len, print_msg_gw, 0);
  } else if (IS_WORD ("rename_contact")) {
    GET_PEER_USER;
    tgl_peer_t *U = tgl_peer_get (id);
    if (!U) {
      printf ("No such user\n");
      RET;
    }
    if (!U->user.phone || !strlen (U->user.phone)) {
      printf ("User has no phone. Can not rename\n");
      RET;
    }
    int phone_len, first_name_len, last_name_len;
    char *phone, *first_name, *last_name;
    phone_len = strlen (U->user.phone);
    phone = U->user.phone;
    first_name = next_token (&first_name_len);
    if (!first_name_len) {
      printf ("No first name found\n");
      RET;
    }
    last_name = next_token (&last_name_len);
    if (!last_name_len) {
      printf ("No last name found\n");
      RET;
    }
    tgl_do_add_contact (phone, phone_len, first_name, first_name_len, last_name, last_name_len, 1, print_user_list_gw, 0);
  } else if (IS_WORD ("help")) {
    //print_start ();
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
  } else if (IS_WORD ("show_license")) {
    char *b = 
#include "LICENSE.h"
    ;
    printf ("%s", b);
  } else if (IS_WORD ("search")) {
    GET_PEER;
    int from = 0;
    int to = 0;
    int limit = 40;
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty message\n");
      RET;
    }
    tgl_do_msg_search (id, from, to, limit, s, print_msg_list_gw, 0);
  } else if (IS_WORD ("global_search")) {
    int from = 0;
    int to = 0;
    int limit = 40;
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty message\n");
      RET;
    }
    tgl_do_msg_search (TGL_PEER_NOT_FOUND, from, to, limit, s, print_msg_list_gw, 0);
  } else if (IS_WORD ("mark_read")) {
    GET_PEER;
    tgl_do_mark_read (id, 0, 0);
  } else if (IS_WORD ("visualize_key")) {
    static char *colors[4] = {COLOR_GREY, COLOR_CYAN, COLOR_BLUE, COLOR_GREEN};
    GET_PEER_ENCR_CHAT;
    static unsigned char buf[16];
    memset (buf, 0, sizeof (buf));
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
  } else if (IS_WORD ("create_secret_chat")) {
    GET_PEER;    
    tgl_do_create_secret_chat (id, print_secret_chat_gw, 0);
  } else if (IS_WORD ("create_group_chat")) {
    GET_PEER_USER;
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty chat topic\n");
      RET;
    }    
    tgl_do_create_group_chat (id, s, 0, 0);  
  //} else if (IS_WORD ("suggested_contacts")) {
  //  tgl_do_get_suggested ();
  } else if (IS_WORD ("status_online")) {
    tgl_do_update_status (1, 0, 0);
  } else if (IS_WORD ("status_offline")) {
    tgl_do_update_status (0, 0, 0);
  } else if (IS_WORD ("contacts_search")) {
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty search query\n");
      RET;
    }
    tgl_do_contacts_search (100, s, print_user_list_gw, 0);
  } else if (IS_WORD("send_audio")) {
    GET_PEER;
    int t;
    char *s = end_string_token (&t);
    if (!s) {
      printf ("Empty file name\n");
      RET;
    }
    char *d = strndup (s, t);
    assert (d);
    tgl_do_send_photo (tgl_message_media_audio, id, d, 0, 0);
  } else if (IS_WORD("send_document")) {
    GET_PEER;
    int t;
    char *s = end_string_token (&t);
    if (!s) {
      printf ("Empty file name\n");
      RET;
    }
    char *d = strndup (s, t);
    assert (d);
    tgl_do_send_photo (tgl_message_media_document, id, d, 0, 0);
  } else if (IS_WORD ("load_audio")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct tgl_message *M = tgl_message_get (num);
    if (M && !M->service && M->media.type == tgl_message_media_audio) {
      tgl_do_load_audio (&M->media.audio, print_filename_gw, 0);
    } else if (M && !M->service && M->media.type == tgl_message_media_audio_encr) {
      tgl_do_load_encr_video (&M->media.encr_video, print_filename_gw, 0);
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("view_audio")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct tgl_message *M = tgl_message_get (num);
    if (M && !M->service && M->media.type == tgl_message_media_audio) {
      tgl_do_load_audio (&M->media.audio, open_filename_gw, 0);
    } else if (M && !M->service && M->media.type == tgl_message_media_audio_encr) {
      tgl_do_load_encr_video (&M->media.encr_video, open_filename_gw, 0);
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("load_document_thumb")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct tgl_message *M = tgl_message_get (num);
    if (M && !M->service && M->media.type == (int)tgl_message_media_document) {
      tgl_do_load_document_thumb (&M->media.document, print_filename_gw, 0);
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("view_document_thumb")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct tgl_message *M = tgl_message_get (num);
    if (M && !M->service && M->media.type == (int)tgl_message_media_document) {
      tgl_do_load_document_thumb (&M->media.document, open_filename_gw, 0);
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("load_document")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct tgl_message *M = tgl_message_get (num);
    if (M && !M->service && M->media.type == tgl_message_media_document) {
      tgl_do_load_document (&M->media.document, print_filename_gw, 0);
    } else if (M && !M->service && M->media.type == tgl_message_media_document_encr) {
      tgl_do_load_encr_video (&M->media.encr_video, print_filename_gw, 0);
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("view_document")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct tgl_message *M = tgl_message_get (num);
    if (M && !M->service && M->media.type == tgl_message_media_document) {
      tgl_do_load_document (&M->media.document, open_filename_gw, 0);
    } else if (M && !M->service && M->media.type == tgl_message_media_document_encr) {
      tgl_do_load_encr_video (&M->media.encr_video, open_filename_gw, 0);
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("set")) {
    command = next_token (&l);
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    if (IS_WORD ("debug_verbosity")) {
      tgl_set_verbosity (num); 
    } else if (IS_WORD ("log_level")) {
      log_level = num;
    } else if (IS_WORD ("msg_num")) {
      msg_num_mode = num;
    } else if (IS_WORD ("alert")) {
      alert_sound = num;
    }
  } else if (IS_WORD ("chat_with_peer")) {
    GET_PEER;
    in_chat_mode = 1;
    chat_mode_id = id;
  } else if (IS_WORD ("delete_msg")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    tgl_do_delete_msg (num, 0, 0);
  } else if (IS_WORD ("restore_msg")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    tgl_do_restore_msg (num, 0, 0);
  } else if (IS_WORD ("delete_restore_msg")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    tgl_do_delete_msg (num, 0, 0);
    tgl_do_restore_msg (num, 0, 0);
  } else if (IS_WORD ("export_card")) {
    tgl_do_export_card (print_card_gw, 0);
  } else if (IS_WORD ("import_card")) {
    int l;
    char *s = next_token (&l);
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
  } else if (IS_WORD ("quit")) {
    exit (0);
  } else if (IS_WORD ("accept_secret_chat")) {
    GET_PEER_ENCR_CHAT;
    tgl_peer_t *E = tgl_peer_get (id);
    assert (E);
    tgl_do_accept_encr_chat_request (&E->encr_chat, 0, 0);
  } else if (IS_WORD ("safe_quit")) {
    safe_quit = 1;
  }
  }
#undef IS_WORD
#undef RET
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
      if (M->photo.caption && strlen (M->photo.caption)) {
        printf ("[photo %s]", M->photo.caption);
      } else {
        printf ("[photo]");
      }
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
