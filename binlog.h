/* 
    This file is part of tgl-library

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

    Copyright Vitaly Valtman 2013-2014
*/
#ifndef __BINLOG_H__
#define __BINLOG_H__

//#include "structures.h"
#include "tgl.h"

void bl_do_set_auth_key_id (int num, unsigned char *buf);

void bl_do_dc_option (int id, int l1, const char *name, int l2, const char *ip, int port);

void bl_do_set_our_id (int id);
void bl_do_user_add (int id, const char *f, int fl, const char *l, int ll, long long access_token, const char *p, int pl, int contact);
void bl_do_user_delete (struct tgl_user *U);
void bl_do_set_user_profile_photo (struct tgl_user *U, long long photo_id, struct tgl_file_location *big, struct tgl_file_location *small);
void bl_do_user_set_name (struct tgl_user *U, const char *f, int fl, const char *l, int ll);
void bl_do_user_set_access_hash (struct tgl_user *U, long long access_token);
void bl_do_user_set_phone (struct tgl_user *U, const char *p, int pl);
void bl_do_user_set_friend (struct tgl_user *U, int friend);
void bl_do_user_set_full_photo (struct tgl_user *U, const int *start, int len);
void bl_do_user_set_blocked (struct tgl_user *U, int blocked);
void bl_do_user_set_real_name (struct tgl_user *U, const char *f, int fl, const char *l, int ll);

void bl_do_encr_chat_delete (struct tgl_secret_chat *U);
void bl_do_encr_chat_requested (struct tgl_secret_chat *U, long long access_hash, int date, int admin_id, int user_id, unsigned char g_key[], unsigned char nonce[]);
void bl_do_encr_chat_create (int id, int user_id, int admin_id, char *name, int name_len);
void bl_do_encr_chat_set_access_hash (struct tgl_secret_chat *U, long long access_hash);
void bl_do_encr_chat_set_date (struct tgl_secret_chat *U, int date);
void bl_do_encr_chat_set_state (struct tgl_secret_chat *U, enum tgl_secret_chat_state state);
void bl_do_encr_chat_set_ttl (struct tgl_secret_chat *U, int ttl);
void bl_do_encr_chat_set_layer (struct tgl_secret_chat *U, int layer);
void bl_do_encr_chat_accepted (struct tgl_secret_chat *U, const unsigned char g_key[], const unsigned char nonce[], long long key_fingerprint);
void bl_do_encr_chat_set_key (struct tgl_secret_chat *E, unsigned char key[], long long key_fingerprint);
void bl_do_encr_chat_init (int id, int user_id, unsigned char random[], unsigned char g_a[]);

void bl_do_dc_signed (int id);
void bl_do_set_working_dc (int num);
void bl_do_set_dh_params (int root, unsigned char prime[], int version);

void bl_do_set_pts (int pts);
void bl_do_set_qts (int qts);
void bl_do_set_seq (int seq);
void bl_do_set_date (int date);

void bl_do_create_chat (struct tgl_chat *C, int y, const char *s, int l, int users_num, int date, int version, struct tgl_file_location *big, struct tgl_file_location *small);
void bl_do_chat_forbid (struct tgl_chat *C, int on);
void bl_do_chat_set_title (struct tgl_chat *C, const char *s, int l);
void bl_do_chat_set_photo (struct tgl_chat *C, struct tgl_file_location *big, struct tgl_file_location *small);
void bl_do_chat_set_date (struct tgl_chat *C, int date);
void bl_do_chat_set_set_in_chat (struct tgl_chat *C, int on);
void bl_do_chat_set_version (struct tgl_chat *C, int version, int user_num);
void bl_do_chat_set_admin (struct tgl_chat *C, int admin);
void bl_do_chat_set_participants (struct tgl_chat *C, int version, int user_num, struct tgl_chat_user *users);
void bl_do_chat_set_full_photo (struct tgl_chat *U, const int *start, int len);
void bl_do_chat_add_user (struct tgl_chat *C, int version, int user, int inviter, int date);
void bl_do_chat_del_user (struct tgl_chat *C, int version, int user);

void bl_do_create_message_text (int msg_id, int from_id, int to_type, int to_id, int date, int unread, int l, const char *s);
void bl_do_create_message_text_fwd (int msg_id, int from_id, int to_type, int to_id, int date, int fwd, int fwd_date, int unread, int l, const char *s);
void bl_do_create_message_service (int msg_id, int from_id, int to_type, int to_id, int date, int unread, const int *data, int len);
void bl_do_create_message_service_fwd (int msg_id, int from_id, int to_type, int to_id, int date, int fwd, int fwd_date, int unread, const int *data, int len);
void bl_do_create_message_media (int msg_id, int from_id, int to_type, int to_id, int date, int unread, int l, const char *s, const int *data, int len);
void bl_do_create_message_media_encr_pending (long long msg_id, int from_id, int to_type, int to_id, int date, int l, const char *s, const int *data, int len);
void bl_do_create_message_media_encr_sent (long long msg_id, const int *data, int len);
void bl_do_create_message_media_fwd (int msg_id, int from_id, int to_type, int to_id, int date, int fwd, int fwd_date, int unread, int l, const char *s, const int *data, int len);
void bl_do_create_message_media_encr (long long msg_id, int from_id, int to_type, int to_id, int date, int l, const char *s, const int *data, int len, const int *data2, int len2);
void bl_do_create_message_service_encr (long long msg_id, int from_id, int to_type, int to_id, int date, const int *data, int len);
void bl_do_send_message_text (long long msg_id, int from_id, int to_type, int to_id, int date, int l, const char *s);
void bl_do_send_message_action_encr (long long msg_id, int from_id, int to_type, int to_id, int date, int l, const int *s);
void bl_do_set_unread (struct tgl_message *M, int unread);
void bl_do_set_message_sent (struct tgl_message *M);
void bl_do_set_msg_id (struct tgl_message *M, int id);
void bl_do_delete_msg (struct tgl_message *M);

void bl_do_msg_seq_update (long long id);
void bl_do_msg_update (long long id);

void bl_do_reset_authorization (void);

//void bl_do_add_dc (int id, const char *ip, int l, int port, long long auth_key_id, const char *auth_key);
#endif
