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
#ifndef __BINLOG_H__
#define __BINLOG_H__

#include "structures.h"

#define LOG_START 0x8948329a
#define LOG_AUTH_KEY 0x984932aa
#define LOG_DEFAULT_DC 0x95382908
#define LOG_OUR_ID 0x8943211a
#define LOG_DC_SIGNED 0x234f9893
#define LOG_DC_SALT 0x92192ffa
#define LOG_DH_CONFIG 0x8983402b
#define LOG_ENCR_CHAT_KEY 0x894320aa
#define LOG_ENCR_CHAT_SEND_ACCEPT 0x12ab01c4
#define LOG_ENCR_CHAT_SEND_CREATE 0xab091e24
#define LOG_ENCR_CHAT_DELETED 0x99481230
#define LOG_ENCR_CHAT_WAITING 0x7102100a
#define LOG_ENCR_CHAT_REQUESTED 0x9011011a
#define LOG_ENCR_CHAT_OK 0x7612ce13

#define CODE_binlog_new_user 0xe04f30de
#define CODE_binlog_user_delete 0xf7a27c79
#define CODE_binlog_set_user_access_token 0x1349f615
#define CODE_binlog_set_user_phone 0x5d3afde2
#define CODE_binlog_set_user_friend 0x75a7ec5a
#define CODE_binlog_dc_option 0x08c0ef19
#define CODE_binlog_user_full_photo 0xfaa35824
#define CODE_binlog_user_blocked 0xb2dea7cd
#define CODE_binlog_set_user_full_name 0x4ceb4cf0
#define CODE_binlog_encr_chat_delete 0xb9d33f87
#define CODE_binlog_encr_chat_requested 0xf57d1ea2
#define CODE_binlog_set_encr_chat_access_hash 0xe5612bb3
#define CODE_binlog_set_encr_chat_date 0x54f16911
#define CODE_binlog_set_encr_chat_state 0x76a6e45b
#define CODE_binlog_encr_chat_accepted 0x4627e926
#define CODE_binlog_set_encr_chat_key 0x179df2d4
#define CODE_binlog_set_dh_params 0x20ba46bc
#define CODE_binlog_encr_chat_init 0x939cd1c7
#define CODE_binlog_set_pts 0x844e4c1c
#define CODE_binlog_set_qts 0x3cf22b79
#define CODE_binlog_set_date 0x33dfe392
#define CODE_binlog_set_seq 0xb9294837
#define CODE_binlog_chat_create 0xbaa75791
#define CODE_binlog_chat_change_flags 0x1e494031
#define CODE_binlog_set_chat_title 0x7dd9bea8
#define CODE_binlog_set_chat_photo 0xb4ea1fd2
#define CODE_binlog_set_chat_date 0x78d1114e
#define CODE_binlog_set_chat_version 0xa5d3504f
#define CODE_binlog_set_chat_admin 0x1e7cea04
#define CODE_binlog_set_chat_participants 0x3a29d335
#define CODE_binlog_chat_full_photo 0x6cca6629
#define CODE_binlog_add_chat_participant 0x63345108
#define CODE_binlog_del_chat_participant 0x82d1f0ee
#define CODE_binlog_create_message_text 0x269acd5b
#define CODE_binlog_create_message_text_fwd 0xa3d864cd
#define CODE_binlog_create_message_service 0xbbe5e94b
#define CODE_binlog_create_message_service_fwd 0xea9c57ae
#define CODE_binlog_create_message_media 0x62a92d19
#define CODE_binlog_create_message_media_fwd 0xbefdc462
#define CODE_binlog_send_message_text 0x31cfd652
#define CODE_binlog_set_unread 0x21d4c909
#define CODE_binlog_set_message_sent 0xc335282b
#define CODE_binlog_set_msg_id 0xf3285b6a
#define CODE_binlog_create_message_media_encr 0x19cd7c9d
#define CODE_binlog_create_message_service_encr 0x8b4b9395
#define CODE_binlog_delete_msg 0xa1d6ab6d

void *alloc_log_event (int l);
void replay_log (void);
void add_log_event (const int *data, int l);
void write_binlog (void);
void bl_do_set_auth_key_id (int num, unsigned char *buf);

void bl_do_dc_option (int id, int l1, const char *name, int l2, const char *ip, int port);

void bl_do_set_our_id (int id);
void bl_do_new_user (int id, const char *f, int fl, const char *l, int ll, long long access_token, const char *p, int pl, int contact);
void bl_do_user_delete (struct user *U);
void bl_do_set_user_profile_photo (struct user *U, long long photo_id, struct file_location *big, struct file_location *small);
void bl_do_set_user_name (struct user *U, const char *f, int fl, const char *l, int ll);
void bl_do_set_user_access_token (struct user *U, long long access_token);
void bl_do_set_user_phone (struct user *U, const char *p, int pl);
void bl_do_set_user_friend (struct user *U, int friend);
void bl_do_set_user_full_photo (struct user *U, const int *start, int len);
void bl_do_set_user_blocked (struct user *U, int blocked);
void bl_do_set_user_real_name (struct user *U, const char *f, int fl, const char *l, int ll);

void bl_do_encr_chat_delete (struct secret_chat *U);
void bl_do_encr_chat_requested (struct secret_chat *U, long long access_hash, int date, int admin_id, int user_id, unsigned char g_key[], unsigned char nonce[]);
void bl_do_set_encr_chat_access_hash (struct secret_chat *U, long long access_hash);
void bl_do_set_encr_chat_date (struct secret_chat *U, int date);
void bl_do_set_encr_chat_state (struct secret_chat *U, enum secret_chat_state state);
void bl_do_encr_chat_accepted (struct secret_chat *U, const unsigned char g_key[], const unsigned char nonce[], long long key_fingerprint);
void bl_do_set_encr_chat_key (struct secret_chat *E, unsigned char key[], long long key_fingerprint);
void bl_do_encr_chat_init (int id, int user_id, unsigned char random[], unsigned char g_a[]);

void bl_do_dc_signed (int id);
void bl_do_set_working_dc (int num);
void bl_do_set_dh_params (int root, unsigned char prime[], int version);

void bl_do_set_pts (int pts);
void bl_do_set_qts (int qts);
void bl_do_set_seq (int seq);
void bl_do_set_date (int date);

void bl_do_create_chat (struct chat *C, int y, const char *s, int l, int users_num, int date, int version, struct file_location *big, struct file_location *small);
void bl_do_chat_forbid (struct chat *C, int on);
void bl_do_set_chat_title (struct chat *C, const char *s, int l);
void bl_do_set_chat_photo (struct chat *C, struct file_location *big, struct file_location *small);
void bl_do_set_chat_date (struct chat *C, int date);
void bl_do_set_chat_set_in_chat (struct chat *C, int on);
void bl_do_set_chat_version (struct chat *C, int version, int user_num);
void bl_do_set_chat_admin (struct chat *C, int admin);
void bl_do_set_chat_participants (struct chat *C, int version, int user_num, struct chat_user *users);
void bl_do_set_chat_full_photo (struct chat *U, const int *start, int len);
void bl_do_chat_add_user (struct chat *C, int version, int user, int inviter, int date);
void bl_do_chat_del_user (struct chat *C, int version, int user);

void bl_do_create_message_text (int msg_id, int from_id, int to_type, int to_id, int date, int l, const char *s);
void bl_do_create_message_text_fwd (int msg_id, int from_id, int to_type, int to_id, int date, int fwd, int fwd_date, int l, const char *s);
void bl_do_create_message_service (int msg_id, int from_id, int to_type, int to_id, int date, const int *data, int len);
void bl_do_create_message_service_fwd (int msg_id, int from_id, int to_type, int to_id, int date, int fwd, int fwd_date, const int *data, int len);
void bl_do_create_message_media (int msg_id, int from_id, int to_type, int to_id, int date, int l, const char *s, const int *data, int len);
void bl_do_create_message_media_fwd (int msg_id, int from_id, int to_type, int to_id, int date, int fwd, int fwd_date, int l, const char *s, const int *data, int len);
void bl_do_create_message_media_encr (long long msg_id, int from_id, int to_type, int to_id, int date, int l, const char *s, const int *data, int len, const int *data2, int len2);
void bl_do_create_message_service_encr (long long msg_id, int from_id, int to_type, int to_id, int date, const int *data, int len);
void bl_do_send_message_text (long long msg_id, int from_id, int to_type, int to_id, int date, int l, const char *s);
void bl_do_set_unread (struct message *M, int unread);
void bl_do_set_message_sent (struct message *M);
void bl_do_set_msg_id (struct message *M, int id);
void bl_do_delete_msg (struct message *M);
#endif
