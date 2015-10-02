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
#ifndef __INTERFACE_H__
#define __INTERFACE_H__
#include <tgl/tgl-structures.h>
#include <tgl/tgl-layout.h>

#define COLOR_RED "\033[0;31m"
#define COLOR_REDB "\033[1;31m"
#define COLOR_NORMAL "\033[0m"
#define COLOR_GREEN "\033[32;1m"
#define COLOR_GREY "\033[37;1m"
#define COLOR_YELLOW "\033[33;1m"
#define COLOR_BLUE "\033[34;1m"
#define COLOR_MAGENTA "\033[35;1m"
#define COLOR_CYAN "\033[36;1m"
#define COLOR_LCYAN "\033[0;36m"

#define COLOR_INVERSE "\033[7m"

char *get_default_prompt (void);
char *complete_none (const char *text, int state);
char **complete_text (char *text, int start, int end);
void interpreter (char *line);
void interpreter_ex (char *line, void *ex);

void rprintf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));
void logprintf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));

#define vlogprintf(v,...) \
  do { \
    if (TLS->verbosity >= (v)) {\
      logprintf (__VA_ARGS__);\
    }\
  } while (0);\


//void hexdump (int *in_ptr, int *in_end);

struct bufferevent;
struct in_ev {
  struct bufferevent *bev;
  char in_buf[4096];
  int in_buf_pos;
  int refcnt;
  int error;
  int fd;
};


struct tgl_message;
struct in_ev;
void print_message (struct in_ev *ev, struct tgl_message *M);
void print_chat_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C);
void print_channel_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C);
void print_user_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *U);
void print_encr_chat_name_full (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C);
void print_encr_chat_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C);
//void print_media (struct tgl_message_media *M);
//
void pop_color (void);
void push_color (const char *color);
void print_start (void);
void print_end (void);
void print_date_full (struct in_ev *ev, long t);
void print_date (struct in_ev *ev, long t);

void play_sound (void);
void update_prompt (void);
void set_interface_callbacks (void);

char *print_permanent_msg_id (tgl_message_id_t id);
char *print_permanent_peer_id (tgl_peer_id_t id);
tgl_peer_id_t parse_input_peer_id (const char *s, int l, int mask);
tgl_message_id_t parse_input_msg_id (const char *s, int l);
#endif
