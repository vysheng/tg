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

#include "config.h"
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
#include "queries.h"

#include "interface.h"
#include "telegram.h"
#include "structures.h"

#include "mtproto-common.h"
char *default_prompt = "> ";

int unread_messages;
int msg_num_mode;

int in_readline;
int readline_active;

long long cur_uploading_bytes;
long long cur_uploaded_bytes;
long long cur_downloading_bytes;
long long cur_downloaded_bytes;

char *line_ptr;
extern peer_t *Peers[];
extern int peer_num;


int is_same_word (const char *s, size_t l, const char *word) {
  return s && word && strlen (word) == l && !memcmp (s, word, l);
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
    if (*line_ptr == '\\') {
      neg = 1 - neg;
    } else {
      if (*line_ptr == '"' && !neg) {
        in_str = !in_str;
      }
      neg = 0;
    }
    line_ptr++;
  }
  *l = line_ptr - s;
  return s;
}

#define NOT_FOUND (int)0x80000000
peer_id_t PEER_NOT_FOUND = {.id = NOT_FOUND};

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

peer_id_t next_token_user (void) {
  int l;
  char *s = next_token (&l);
  if (!s) { return PEER_NOT_FOUND; }

  if (l >= 6 && !memcmp (s, "user#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    if (r >= 0) { return set_peer_id (PEER_USER, r); }
    else { return PEER_NOT_FOUND; }
  }

  int index = 0;
  while (index < peer_num && (!is_same_word (s, l, Peers[index]->print_name) || get_peer_type (Peers[index]->id) != PEER_USER)) {
    index ++;
  }
  if (index < peer_num) {
    return Peers[index]->id;
  } else {
    return PEER_NOT_FOUND;
  }
}

peer_id_t next_token_chat (void) {
  int l;
  char *s = next_token (&l);
  if (!s) { return PEER_NOT_FOUND; }
  
  if (l >= 6 && !memcmp (s, "chat#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    if (r >= 0) { return set_peer_id (PEER_CHAT, r); }
    else { return PEER_NOT_FOUND; }
  }

  int index = 0;
  while (index < peer_num && (!is_same_word (s, l, Peers[index]->print_name) || get_peer_type (Peers[index]->id) != PEER_CHAT)) {
    index ++;
  }
  if (index < peer_num) {
    return Peers[index]->id;
  } else {
    return PEER_NOT_FOUND;
  }
}

peer_id_t next_token_encr_chat (void) {
  int l;
  char *s = next_token (&l);
  if (!s) { return PEER_NOT_FOUND; }

  int index = 0;
  while (index < peer_num && (!is_same_word (s, l, Peers[index]->print_name) || get_peer_type (Peers[index]->id) != PEER_ENCR_CHAT)) {
    index ++;
  }
  if (index < peer_num) {
    return Peers[index]->id;
  } else {
    return PEER_NOT_FOUND;
  }
}

peer_id_t next_token_peer (void) {
  int l;
  char *s = next_token (&l);
  if (!s) { return PEER_NOT_FOUND; }
  
  if (l >= 6 && !memcmp (s, "user#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    if (r >= 0) { return set_peer_id (PEER_USER, r); }
    else { return PEER_NOT_FOUND; }
  }
  if (l >= 6 && !memcmp (s, "chat#", 5)) {
    s += 5;    
    l -= 5;
    int r = atoi (s);
    if (r >= 0) { return set_peer_id (PEER_CHAT, r); }
    else { return PEER_NOT_FOUND; }
  }

  int index = 0;
  while (index < peer_num && (!is_same_word (s, l, Peers[index]->print_name))) {
    index ++;
  }
  if (index < peer_num) {
    return Peers[index]->id;
  } else {
    return PEER_NOT_FOUND;
  }
}

char *get_default_prompt (void) {
  static char buf[100];
  if (unread_messages || cur_uploading_bytes || cur_downloading_bytes) {
    int l = sprintf (buf, COLOR_RED "[");
    int ok = 0;
    if (unread_messages) {
      l += sprintf (buf + l, "%d unread", unread_messages);
      ok = 1;
    }
    if (cur_uploading_bytes) {
      if (ok) { *(buf + l) = ' '; l ++; }
      ok = 1;
      l += sprintf (buf + l, "%lld%%Up", 100 * cur_uploaded_bytes / cur_uploading_bytes);
    }
    if (cur_downloading_bytes) {
      if (ok) { *(buf + l) = ' '; l ++; }
      ok = 1;
      l += sprintf (buf + l, "%lld%%Down", 100 * cur_downloaded_bytes / cur_downloading_bytes);
    }
    sprintf (buf + l, "]" COLOR_NORMAL "%s", default_prompt);
    return buf;
  } else {
    return default_prompt;
  }
}

char *complete_none (const char *text UU, int state UU) {
  return 0;
}


void set_prompt (const char *s) {
  rl_set_prompt (s);
}

void update_prompt (void) {
  print_start ();
  set_prompt (get_default_prompt ());
  if (readline_active) {
    rl_redisplay ();
  }
  print_end ();
}

char *commands[] = {
  "help",
  "msg",
  "contact_list",
  "stats",
  "history",
  "dialog_list",
  "send_photo",
  "send_video",
  "send_text",
  "chat_info",
  "user_info",
  "fwd",
  "rename_chat",
  "load_photo",
  "view_photo",
  "load_video_thumb",
  "view_video_thumb",
  "load_video",
  "view_video",
  "add_contact",
  "rename_contact",
  "show_license",
  "search",
  "mark_read",
  "visualize_key",
  "create_secret_chat",
  "suggested_contacts",
  "global_search",
  "chat_add_user",
  "chat_del_user",
  "status_online",
  "status_offline",
  "contacts_search",
  "quit",
  0 };

int commands_flags[] = {
  070,
  072,
  07,
  07,
  072,
  07,
  0732,
  0732,
  0732,
  074,
  071,
  072,
  074,
  07,
  07,
  07,
  07,
  07,
  07,
  07,
  071,
  07,
  072,
  072,
  075,
  071,
  07,
  07,
  0724,
  0724,
  07,
  07,
  07,
  07,
};

int get_complete_mode (void) {
  line_ptr = rl_line_buffer;
  int l = 0;
  char *r = next_token (&l);
  if (!r) { return 0; }
  while (r && r[0] == '[' && r[l - 1] == ']') {
    r = next_token (&l);
    if (!r) { return 0; }
  }
 
  if (!*line_ptr) { return 0; }
  char **command = commands;
  int n = 0;
  int flags = -1;
  while (*command) {
    if (is_same_word (r, l, *command)) {
      flags = commands_flags[n];
      break;
    }
    n ++;
    command ++;
  }
  if (flags == -1) {
    return 7;
  }
  int s = 0;
  while (1) {
    if (!next_token (&l) || !*line_ptr) {
      return flags ? flags & 7 : 7;
    }
    s ++;
    if (s <= 4) { flags >>= 3; }
  }
}

int complete_user_list (int index, const char *text, int len, char **R) {
  index ++;
  while (index < peer_num && (!Peers[index]->print_name || strncmp (Peers[index]->print_name, text, len) || get_peer_type (Peers[index]->id) != PEER_USER)) {
    index ++;
  }
  if (index < peer_num) {
    *R = strdup (Peers[index]->print_name);
    return index;
  } else {
    return -1;
  }
}

int complete_chat_list (int index, const char *text, int len, char **R) {
  index ++;
  while (index < peer_num && (!Peers[index]->print_name || strncmp (Peers[index]->print_name, text, len) || get_peer_type (Peers[index]->id) != PEER_CHAT)) {
    index ++;
  }
  if (index < peer_num) {
    *R = strdup (Peers[index]->print_name);
    return index;
  } else {
    return -1;
  }
}

int complete_encr_chat_list (int index, const char *text, int len, char **R) {
  index ++;
  while (index < peer_num && (!Peers[index]->print_name || strncmp (Peers[index]->print_name, text, len) || get_peer_type (Peers[index]->id) != PEER_ENCR_CHAT)) {
    index ++;
  }
  if (index < peer_num) {
    *R = strdup (Peers[index]->print_name);
    return index;
  } else {
    return -1;
  }
}

int complete_user_chat_list (int index, const char *text, int len, char **R) {
  index ++;
  while (index < peer_num && (!Peers[index]->print_name || strncmp (Peers[index]->print_name, text, len))) {
    index ++;
  }
  if (index < peer_num) {
    *R = strdup (Peers[index]->print_name);
    return index;
  } else {
    return -1;
  }
}

int complete_string_list (char **list, int index, const char *text, int len, char **R) {
  index ++;
  while (list[index] && strncmp (list[index], text, len)) {
    index ++;
  }
  if (list[index]) {
    *R = strdup (list[index]);
    return index;
  } else {
    *R = 0;
    return -1;
  }
}
char *command_generator (const char *text, int state) {  
  static int len, index, mode;
 
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

  if (mode == -1) { 
    if (c) { rl_line_buffer[rl_point] = c; }
    return 0; 
  }

  char *R = 0;
  switch (mode & 7) {
  case 0:
    index = complete_string_list (commands, index, text, len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case 1:
    index = complete_user_list (index, text, len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case 2:
    index = complete_user_chat_list (index, text, len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case 3:
    R = rl_filename_completion_function(text,state);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case 4:
    index = complete_chat_list (index, text, len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case 5:
    index = complete_encr_chat_list (index, text, len, &R);
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

void work_modifier (const char *s UU) {
}

void interpreter (char *line UU) {
  line_ptr = line;
  assert (!in_readline);
  in_readline = 1;
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
      work_modifier (command);
    } else {
      break;
    }
  }

#define IS_WORD(s) is_same_word (command, l, (s))
#define RET in_readline = 0; return; 

  peer_id_t id;
#define GET_PEER \
  id = next_token_peer (); \
  if (!cmp_peer_id (id, PEER_NOT_FOUND)) { \
    printf ("Bad user/char id\n"); \
    RET; \
  } 
#define GET_PEER_USER \
  id = next_token_user (); \
  if (!cmp_peer_id (id, PEER_NOT_FOUND)) { \
    printf ("Bad user id\n"); \
    RET; \
  } 
#define GET_PEER_CHAT \
  id = next_token_chat (); \
  if (!cmp_peer_id (id, PEER_NOT_FOUND)) { \
    printf ("Bad chat id\n"); \
    RET; \
  } 
#define GET_PEER_ENCR_CHAT \
  id = next_token_encr_chat (); \
  if (!cmp_peer_id (id, PEER_NOT_FOUND)) { \
    printf ("Bad encr_chat id\n"); \
    RET; \
  } 

  if (IS_WORD ("contact_list")) {
    do_update_contact_list ();
  } else if (IS_WORD ("dialog_list")) {
    do_get_dialog_list ();
  } else if (IS_WORD ("stats")) {
    static char stat_buf[1 << 15];
    print_stat (stat_buf, (1 << 15) - 1);
    printf ("%s\n", stat_buf);
  } else if (IS_WORD ("msg")) {
    GET_PEER;
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty message\n");
      RET;
    }
    do_send_message (id, s, strlen (s));
  } else if (IS_WORD ("rename_chat")) {
    GET_PEER_CHAT;
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty new name\n");
      RET;
    }
    do_rename_chat (id, s);
  } else if (IS_WORD ("send_photo")) {
    GET_PEER;
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty file name\n");
      RET;
    }
    do_send_photo (CODE_input_media_uploaded_photo, id, strndup (s, t));
  } else if (IS_WORD("send_video")) {
    GET_PEER;
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty file name\n");
      RET;
    }
    do_send_photo (CODE_input_media_uploaded_video, id, strndup (s, t));
  } else if (IS_WORD ("send_text")) {
    GET_PEER;
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty file name\n");
      RET;
    }
    do_send_text (id, strndup (s, t));
  } else if (IS_WORD ("fwd")) {
    GET_PEER;
    int num = next_token_int ();
    if (num == NOT_FOUND || num <= 0) {
      printf ("Bad msg id\n");
      RET;
    }
    do_forward_message (id, num);
  } else if (IS_WORD ("load_photo")) {
    long long num = next_token_int ();
    if (num == NOT_FOUND) {
      printf ("Bad msg id\n");
      RET;
    }
    struct message *M = message_get (num);
    if (M && !M->service && M->media.type == (int)CODE_message_media_photo) {
      do_load_photo (&M->media.photo, 1);
    } else if (M && !M->service && M->media.type == (int)CODE_decrypted_message_media_photo) {
      do_load_encr_video (&M->media.encr_video, 1); // this is not a bug. 
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
    struct message *M = message_get (num);
    if (M && !M->service && M->media.type == (int)CODE_message_media_photo) {
      do_load_photo (&M->media.photo, 2);
    } else if (M && !M->service && M->media.type == (int)CODE_decrypted_message_media_photo) {
      do_load_encr_video (&M->media.encr_video, 2); // this is not a bug. 
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
    struct message *M = message_get (num);
    if (M && !M->service && M->media.type == (int)CODE_message_media_video) {
      do_load_video_thumb (&M->media.video, 1);
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
    struct message *M = message_get (num);
    if (M && !M->service && M->media.type == (int)CODE_message_media_video) {
      do_load_video_thumb (&M->media.video, 2);
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
    struct message *M = message_get (num);
    if (M && !M->service && M->media.type == (int)CODE_message_media_video) {
      do_load_video (&M->media.video, 1);
    } else if (M && !M->service && M->media.type == (int)CODE_decrypted_message_media_video) {
      do_load_encr_video (&M->media.encr_video, 1);
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
    struct message *M = message_get (num);
    if (M && !M->service && M->media.type == (int)CODE_message_media_video) {
      do_load_video (&M->media.video, 2);
    } else if (M && !M->service && M->media.type == (int)CODE_decrypted_message_media_video) {
      do_load_encr_video (&M->media.encr_video, 2);
    } else {
      printf ("Bad msg id\n");
      RET;
    }
  } else if (IS_WORD ("chat_info")) {
    GET_PEER_CHAT;
    do_get_chat_info (id);
  } else if (IS_WORD ("user_info")) {
    GET_PEER_USER;
    do_get_user_info (id);
  } else if (IS_WORD ("history")) {
    GET_PEER;
    int limit = next_token_int ();
    do_get_history (id, limit > 0 ? limit : 40);
  } else if (IS_WORD ("chat_add_user")) {
    GET_PEER_CHAT;    
    peer_id_t chat_id = id;
    GET_PEER_USER;
    do_add_user_to_chat (chat_id, id, 100);
  } else if (IS_WORD ("chat_del_user")) {
    GET_PEER_CHAT;    
    peer_id_t chat_id = id;
    GET_PEER_USER;
    do_del_user_from_chat (chat_id, id);
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
    do_add_contact (phone, phone_len, first_name, first_name_len, last_name, last_name_len, 0);
  } else if (IS_WORD ("rename_contact")) {
    GET_PEER_USER;
    peer_t *U = user_chat_get (id);
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
    do_add_contact (phone, phone_len, first_name, first_name_len, last_name, last_name_len, 1);
  } else if (IS_WORD ("help")) {
    //print_start ();
    push_color (COLOR_YELLOW);
    printf (
      "help - prints this help\n"
      "msg <peer> Text - sends message to this peer\n"
      "contact_list - prints info about users in your contact list\n"
      "stats - just for debugging \n"
      "history <peerd> [limit] - prints history (and marks it as read). Default limit = 40\n"
      "dialog_list - prints info about your dialogs\n"
      "send_photo <peer> <photo-file-name> - sends photo to peer\n"
      "send_video <peer> <video-file-name> - sends video to peer\n"
      "send_text <peer> <text-file-name> - sends text file as plain messages\n"
      "chat_info <chat> - prints info about chat\n"
      "user_info <user> - prints info about user\n"
      "fwd <user> <msg-seqno> - forward message to user. You can see message numbers starting client with -N\n"
      "rename_chat <char> <new-name>\n"
      "load_photo/load_video/load_video_thumb <msg-seqno> - loads photo/video to download dir\n"
      "view_photo/view_video/view_video_thumb <msg-seqno> - loads photo/video to download dir and starts system default viewer\n"
      "show_license - prints contents of GPLv2\n"
      "search <peer> pattern - searches pattern in messages with peer\n"
      "global_search pattern - searches pattern in all messages\n"
      "mark_read <peer> - mark read all received messages with peer\n"
      "add_contact <phone-number> <first-name> <last-name> - tries to add contact to contact-list by phone\n"
      "create_secret_chat <user> - creates secret chat with this user\n"
      "rename_contact <user> <first-name> <last-name> - tries to rename contact. If you have another device it will be a fight\n"
      "suggested_contacts - print info about contacts, you have max common friends\n"
      "visualize_key <secret_chat> - prints visualization of encryption key. You should compare it to your partner's one\n"
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
    do_msg_search (id, from, to, limit, s);
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
    do_msg_search (PEER_NOT_FOUND, from, to, limit, s);
  } else if (IS_WORD ("mark_read")) {
    GET_PEER;
    do_mark_read (id);
  } else if (IS_WORD ("visualize_key")) {
    GET_PEER_ENCR_CHAT;
    do_visualize_key (id);
  } else if (IS_WORD ("create_secret_chat")) {
    GET_PEER;    
    do_create_secret_chat (id);
  } else if (IS_WORD ("suggested_contacts")) {
    do_get_suggested ();
  } else if (IS_WORD ("status_online")) {
    do_update_status (1);
  } else if (IS_WORD ("status_offline")) {
    do_update_status (0);
  } else if (IS_WORD ("contacts_search")) {
    int t;
    char *s = next_token (&t);
    if (!s) {
      printf ("Empty search query\n");
      RET;
    }
    do_contacts_search (100, s);
  } else if (IS_WORD ("quit")) {
    exit (0);
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
  assert (!prompt_was);
  if (readline_active) {
    saved_point = rl_point;
#ifdef READLINE_GNU
    saved_line = rl_copy_text(0, rl_end);    
    rl_save_prompt();
    rl_replace_line("", 0);
#else
    assert (rl_end >= 0);
    saved_line = malloc (rl_end + 1);
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
    free(saved_line);
  }
  prompt_was = 0;
}

void hexdump (int *in_ptr, int *in_end) {
  print_start ();
  int *ptr = in_ptr;
  while (ptr < in_end) { printf (" %08x", *(ptr ++)); }
  printf ("\n");
  print_end (); 
}

void logprintf (const char *format, ...) {
  print_start ();
  printf (COLOR_GREY " *** ");
  va_list ap;
  va_start (ap, format);
  vfprintf (stdout, format, ap);
  va_end (ap);
  printf (COLOR_NORMAL);
  print_end ();
}

int color_stack_pos;
const char *color_stack[10];

void push_color (const char *color) {
  assert (color_stack_pos < 10);
  color_stack[color_stack_pos ++] = color;
  printf ("%s", color);
}

void pop_color (void) {
  assert (color_stack_pos > 0);
  color_stack_pos --;
  if (color_stack_pos >= 1) {
    printf ("%s", color_stack[color_stack_pos - 1]);
  } else {
    printf ("%s", COLOR_NORMAL);
  }
}

void print_media (struct message_media *M) {
  switch (M->type) {
    case CODE_message_media_empty:
      return;
    case CODE_message_media_photo:
      if (M->photo.caption && strlen (M->photo.caption)) {
        printf ("[photo %s]", M->photo.caption);
      } else {
        printf ("[photo]");
      }
      return;
    case CODE_message_media_video:
      printf ("[video]");
      return;
    case CODE_decrypted_message_media_photo:
       printf ("[photo]");
      return;
    case CODE_decrypted_message_media_video:
      printf ("[video]");
      return;
    case CODE_message_media_geo:
      printf ("[geo] https://maps.google.com/?q=%.6lf,%.6lf", M->geo.latitude, M->geo.longitude);
      return;
    case CODE_message_media_contact:
      printf ("[contact] ");
      push_color (COLOR_RED);
      printf ("%s %s ", M->first_name, M->last_name);
      pop_color ();
      printf ("%s", M->phone);
      return;
    case CODE_message_media_unsupported:
      printf ("[unsupported]");
      return;
    default:
      assert (0);
  }
}

int unknown_user_list_pos;
int unknown_user_list[1000];

void print_user_name (peer_id_t id, peer_t *U) {
  assert (get_peer_type (id) == PEER_USER);
  push_color (COLOR_RED);
  if (!U) {
    printf ("user#%d", get_peer_id (id));
    int i;
    int ok = 1;
    for (i = 0; i < unknown_user_list_pos; i++) {
      if (unknown_user_list[i] == get_peer_id (id)) {
        ok = 0;
        break;
      }
    }
    if (ok) {
      assert (unknown_user_list_pos < 1000);
      unknown_user_list[unknown_user_list_pos ++] = get_peer_id (id);
    }
  } else {
    if (U->flags & (FLAG_USER_SELF | FLAG_USER_CONTACT)) {
      push_color (COLOR_REDB);
    }
    if ((U->flags & FLAG_DELETED) || (U->flags & FLAG_EMPTY)) {
      printf ("deleted user#%d", get_peer_id (id));
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

void print_chat_name (peer_id_t id, peer_t *C) {
  push_color (COLOR_MAGENTA);
  if (!C) {
    printf ("chat#%d", get_peer_id (id));
  } else {
    printf ("%s", C->chat.title);
  }
  pop_color ();
}

void print_encr_chat_name (peer_id_t id, peer_t *C) {
  push_color (COLOR_MAGENTA);
  if (!C) {
    printf ("encr_chat#%d", get_peer_id (id));
  } else {
    printf ("%s", C->print_name);
  }
  pop_color ();
}

void print_encr_chat_name_full (peer_id_t id, peer_t *C) {
  push_color (COLOR_MAGENTA);
  if (!C) {
    printf ("encr_chat#%d", get_peer_id (id));
  } else {
    printf ("%s", C->print_name);
  }
  pop_color ();
}

static char *monthes[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
void print_date (long t) {
  struct tm *tm = localtime (&t);
  if (time (0) - t < 12 * 60 * 60) {
    printf ("[%02d:%02d] ", tm->tm_hour, tm->tm_min);
  } else {
    printf ("[%02d %s]", tm->tm_mday, monthes[tm->tm_mon]);
  }
}

void print_date_full (long t) {
  struct tm *tm = localtime (&t);
  printf ("[%04d/%02d/%02d %02d:%02d:%02d]", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

int our_id;

void print_service_message (struct message *M) {
  print_start ();
  push_color (COLOR_GREY);
  
  push_color (COLOR_MAGENTA);
  if (msg_num_mode) {
    printf ("%lld ", M->id);
  }
  print_date (M->date);
  pop_color ();
  printf (" ");
  print_chat_name (M->to_id, user_chat_get (M->to_id));
  printf (" ");
  print_user_name (M->from_id, user_chat_get (M->from_id));
 
  switch (M->action.type) {
  case CODE_message_action_empty:
    printf ("\n");
    break;
  case CODE_message_action_chat_create:
    printf (" created chat %s. %d users\n", M->action.title, M->action.user_num);
    break;
  case CODE_message_action_chat_edit_title:
    printf (" changed title to %s\n", 
      M->action.new_title);
    break;
  case CODE_message_action_chat_edit_photo:
    printf (" changed photo\n");
    break;
  case CODE_message_action_chat_delete_photo:
    printf (" deleted photo\n");
    break;
  case CODE_message_action_chat_add_user:
    printf (" added user ");
    print_user_name (set_peer_id (PEER_USER, M->action.user), user_chat_get (set_peer_id (PEER_USER, M->action.user)));
    printf ("\n");
    break;
  case CODE_message_action_chat_delete_user:
    printf (" deleted user ");
    print_user_name (set_peer_id (PEER_USER, M->action.user), user_chat_get (set_peer_id (PEER_USER, M->action.user)));
    printf ("\n");
    break;
  default:
    assert (0);
  }
  pop_color ();
  print_end ();
}

peer_id_t last_from_id;
peer_id_t last_to_id;

void print_message (struct message *M) {
  if (M->flags & (FLAG_EMPTY | FLAG_DELETED)) {
    return;
  }
  if (M->service) {
    print_service_message (M);
    return;
  }

  last_from_id = M->from_id;
  last_to_id = M->to_id;

  print_start ();
  if (get_peer_type (M->to_id) == PEER_USER) {
    if (M->out) {
      push_color (COLOR_GREEN);
      if (msg_num_mode) {
        printf ("%lld ", M->id);
      }
      print_date (M->date);
      pop_color ();
      printf (" ");
      print_user_name (M->to_id, user_chat_get (M->to_id));
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
      print_user_name (M->from_id, user_chat_get (M->from_id));
      push_color (COLOR_BLUE);
      if (M->unread) {
        printf (" >>> ");
      } else {
        printf (" »»» ");
      }
    }
  } else if (get_peer_type (M->to_id) == PEER_ENCR_CHAT) {
    peer_t *P = user_chat_get (M->to_id);
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
    }
    
  } else {
    assert (get_peer_type (M->to_id) == PEER_CHAT);
    push_color (COLOR_MAGENTA);
    if (msg_num_mode) {
      printf ("%lld ", M->id);
    }
    print_date (M->date);
    pop_color ();
    printf (" ");
    print_chat_name (M->to_id, user_chat_get (M->to_id));
    printf (" ");
    print_user_name (M->from_id, user_chat_get (M->from_id));
    if ((get_peer_type (M->from_id) == PEER_USER) && (get_peer_id (M->from_id) == our_id)) {
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
  if (get_peer_type (M->fwd_from_id) == PEER_USER) {
    printf ("[fwd from ");
    print_user_name (M->fwd_from_id, user_chat_get (M->fwd_from_id));
    printf ("] ");
  }
  if (M->message && strlen (M->message)) {
    printf ("%s", M->message);
  }
  if (M->media.type != CODE_message_media_empty) {
    print_media (&M->media);
  }
  pop_color ();
  assert (!color_stack_pos);
  printf ("\n");
  print_end();
}

void set_interface_callbacks (void) {
  readline_active = 1;
  rl_callback_handler_install (get_default_prompt (), interpreter);
  rl_attempted_completion_function = (CPPFunction *) complete_text;
  rl_completion_entry_function = (void *)complete_none;
}
