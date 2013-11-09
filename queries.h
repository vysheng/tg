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
#include "structures.h"

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
void do_send_message (peer_id_t id, const char *msg, int len);
void do_send_text (peer_id_t id, char *file);
void do_get_history (peer_id_t id, int limit);
void do_get_dialog_list (void);
void do_get_dialog_list_ex (void);
void do_send_photo (int type, peer_id_t to_id, char *file_name);
void do_get_chat_info (peer_id_t id);
void do_get_user_list_info_silent (int num, int *list);
void do_get_user_info (peer_id_t id);
void do_forward_message (peer_id_t id, int n);
void do_rename_chat (peer_id_t id, char *name);
void do_load_encr_video (struct encr_video *V, int next);
void do_create_encr_chat_request (struct secret_chat *E);
void do_create_secret_chat (peer_id_t id);
void do_get_suggested (void);

struct photo;
struct video;
void do_load_photo (struct photo *photo, int next);
void do_load_video_thumb (struct video *video, int next);
void do_load_video (struct video *V, int next);
void do_help_get_config (void);
int do_auth_check_phone (const char *user);
int do_get_nearest_dc (void);
int do_send_code_result_auth (const char *code, const char *first_name, const char *last_name);
void do_import_auth (int num);
void do_export_auth (int num);
void do_add_contact (const char *phone, int phone_len, const char *first_name, int first_name_len, const char *last_name, int last_name_len, int force);
void do_msg_search (peer_id_t id, int from, int to, int limit, const char *s);
void do_accept_encr_chat_request (struct secret_chat *E);
void do_get_difference (void);
void do_mark_read (peer_id_t id);
void do_visualize_key (peer_id_t id);
void do_create_keys_end (struct secret_chat *U);
void do_add_user_to_chat (peer_id_t chat_id, peer_id_t id, int limit);
void do_del_user_from_chat (peer_id_t chat_id, peer_id_t id);
void do_update_status (int online);
void do_contacts_search (int limit, const char *s);

#endif
