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
#include "net.h"
#ifndef __QUERIES_H__
#define __QUERIES_H__

#define QUERY_ACK_RECEIVED 1

struct query;
struct query_methods {
  int (*on_answer)(struct query *q);
  int (*on_error)(struct query *q, int error_code, int len, char *error);
  int (*on_timeout)(struct query *q);
};

struct event_timer {
  double timeout;
  int (*alarm)(void *self);
  void *self;
};

struct query {
  long long msg_id;
  int data_len;
  int flags;
  int seq_no;
  void *data;
  struct query_methods *methods;
  struct event_timer ev;
  struct dc *DC;
  struct session *session;
  void *extra;
};


struct query *send_query (struct dc *DC, int len, void *data, struct query_methods *methods, void *extra);
void query_ack (long long id);
void query_error (long long id);
void query_result (long long id);
void query_restart (long long id);

void insert_event_timer (struct event_timer *ev);
void remove_event_timer (struct event_timer *ev);
double next_timer_in (void);
void work_timers (void);

extern struct query_methods help_get_config_methods;

void do_send_code (const char *user);
int do_send_code_result (const char *code);
double get_double_time (void);

void do_update_contact_list (void);
union user_chat;
void do_send_message (union user_chat *U, const char *msg, int len);
void do_send_text (union user_chat *U, char *file);
void do_get_history (union user_chat *U, int limit);
void do_get_dialog_list (void);
void do_send_photo (int type, int to_id, char *file_name);
void do_get_chat_info (union user_chat *chat);
void do_get_user_list_info_silent (int num, int *list);
void do_get_user_info (union user_chat *user);
void do_forward_message (union user_chat *U, int n);

#endif
