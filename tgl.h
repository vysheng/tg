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

    Copyright Vitaly Valtman 2014
*/
#ifndef __TGL_H__
#define __TGL_H__

#include <tgl-layout.h>
#include <string.h>

#define TGL_MAX_DC_NUM 100
#define TG_SERVER "149.154.167.50"
#define TG_SERVER_TEST "149.154.167.40"
#define TG_SERVER_DC 2
#define TG_SERVER_TEST_DC 2

// JUST RANDOM STRING
#define TGL_BUILD "2012"
#define TGL_VERSION "1.0.3"

#define TGL_ENCRYPTED_LAYER 16

struct connection;
struct mtproto_methods;
struct tgl_session;
struct tgl_dc;
struct bingnum_ctx;

#define TGL_UPDATE_CREATED 1
#define TGL_UPDATE_DELETED 2
#define TGL_UPDATE_PHONE 4
#define TGL_UPDATE_CONTACT 8
#define TGL_UPDATE_PHOTO 16
#define TGL_UPDATE_BLOCKED 32
#define TGL_UPDATE_REAL_NAME 64
#define TGL_UPDATE_NAME 128
#define TGL_UPDATE_REQUESTED 256
#define TGL_UPDATE_WORKING 512
#define TGL_UPDATE_FLAGS 1024
#define TGL_UPDATE_TITLE 2048
#define TGL_UPDATE_ADMIN 4096
#define TGL_UPDATE_MEMBERS 8192
#define TGL_UPDATE_ACCESS_HASH 16384

struct tgl_allocator {
  void *(*alloc)(size_t size);
  void *(*realloc)(void *ptr, size_t old_size, size_t size);
  void (*free)(void *ptr, int size);
  void (*check)(void);
  void (*exists)(void *ptr, int size);
};
extern struct tgl_allocator tgl_allocator_release;
extern struct tgl_allocator tgl_allocator_debug;

struct tgl_update_callback {
  void (*new_msg)(struct tgl_message *M);
  void (*marked_read)(int num, struct tgl_message *list[]);
  void (*logprintf)(const char *format, ...)  __attribute__ ((format (printf, 1, 2)));
  void (*type_notification)(struct tgl_user *U);
  void (*type_in_chat_notification)(struct tgl_user *U, struct tgl_chat *C);
  void (*type_in_secret_chat_notification)(struct tgl_secret_chat *E);
  void (*status_notification)(struct tgl_user *U);
  void (*user_registered)(struct tgl_user *U);
  void (*user_activated)(struct tgl_user *U);
  void (*new_authorization)(const char *device, const char *location);
  //void (*secret_chat_created)(struct tgl_secret_chat *E);
  //void (*secret_chat_request)(struct tgl_secret_chat *E);
  //void (*secret_chat_established)(struct tgl_secret_chat *E);
  //void (*secret_chat_deleted)(struct tgl_secret_chat *E);
  //void (*secret_chat_accepted)(struct tgl_secret_chat *E);
  //void (*new_user)(struct tgl_user *U);
  //void (*delete_user)(struct tgl_user *U);
  //void (*update_user_info)(struct tgl_user *U);
  //void (*secret_chat_delete)(struct tgl_secret_chat *U);
  //void (*secret_chat_requested)(struct tgl_secret_chat *U);
  //void (*secret_chat_accepted)(struct tgl_secret_chat *U);
  //void (*secret_chat_created)(struct tgl_secret_chat *U);
  void (*chat_update)(struct tgl_chat *C, unsigned flags);
  void (*user_update)(struct tgl_user *C, unsigned flags);
  void (*secret_chat_update)(struct tgl_secret_chat *C, unsigned flags);
  void (*msg_receive)(struct tgl_message *M);
  void (*our_id)(int id);
  char *(*create_print_name) (tgl_peer_id_t id, const char *a1, const char *a2, const char *a3, const char *a4);
};

struct tgl_net_methods {
  int (*write_out) (struct connection *c, const void *data, int len);
  int (*read_in) (struct connection *c, void *data, int len);
  int (*read_in_lookup) (struct connection *c, void *data, int len);
  void (*flush_out) (struct connection *c);
  void (*incr_out_packet_num) (struct connection *c);
  struct tgl_dc *(*get_dc) (struct connection *c);
  struct tgl_session *(*get_session) (struct connection *c);

  struct connection *(*create_connection) (const char *host, int port, struct tgl_session *session, struct tgl_dc *dc, struct mtproto_methods *methods);
};


#define E_ERROR 0
#define E_WARNING 1
#define E_NOTICE 2
#define E_DEBUG 6

#define TGL_LOCK_DIFF 1

#define TGL_MAX_RSA_KEYS_NUM 10
// Do not modify this structure, unless you know what you do
struct tgl_state {
  int our_id; // ID of logged in user
  int encr_root;
  unsigned char *encr_prime;
  int encr_param_version;
  int pts;
  int qts;
  int date;
  int seq;
  int binlog_enabled;
  int test_mode; 
  int verbosity;
  int unread_messages;
  int active_queries;
  int max_msg_id;

  long long locks; 
  struct tgl_dc *DC_list[TGL_MAX_DC_NUM];
  struct tgl_dc *DC_working;
  int max_dc_num;
  int dc_working_num;
  int enable_pfs;
  int temp_key_expire_time;

  long long cur_uploading_bytes;
  long long cur_uploaded_bytes;
  long long cur_downloading_bytes;
  long long cur_downloaded_bytes;

  char *binlog_name;
  char *auth_file;
  char *downloads_directory;

  struct tgl_update_callback callback;
  struct tgl_net_methods *net_methods;
  struct event_base *ev_base;

  char *rsa_key_list[TGL_MAX_RSA_KEYS_NUM];
  int rsa_key_num;
  struct bignum_ctx *BN_ctx;

  struct tgl_allocator allocator;
};
extern struct tgl_state tgl_state;

void tgl_reopen_binlog_for_writing (void);
void tgl_replay_log (void);

int tgl_print_stat (char *s, int len);
tgl_peer_t *tgl_peer_get (tgl_peer_id_t id);
tgl_peer_t *tgl_peer_get_by_name (const char *s);

struct tgl_message *tgl_message_get (long long id);
void tgl_peer_iterator_ex (void (*it)(tgl_peer_t *P, void *extra), void *extra);

int tgl_complete_user_list (int index, const char *text, int len, char **R);
int tgl_complete_chat_list (int index, const char *text, int len, char **R);
int tgl_complete_encr_chat_list (int index, const char *text, int len, char **R);
int tgl_complete_peer_list (int index, const char *text, int len, char **R);

#define TGL_PEER_USER 1
#define TGL_PEER_CHAT 2
#define TGL_PEER_GEO_CHAT 3
#define TGL_PEER_ENCR_CHAT 4
#define TGL_PEER_UNKNOWN 0

#define TGL_MK_USER(id) tgl_set_peer_id (TGL_PEER_USER,id)
#define TGL_MK_CHAT(id) tgl_set_peer_id (TGL_PEER_CHAT,id)
#define TGL_MK_GEO_CHAT(id) tgl_set_peer_id (TGL_PEER_GEO_CHAT,id)
#define TGL_MK_ENCR_CHAT(id) tgl_set_peer_id (TGL_PEER_ENCR_CHAT,id)

void tgl_set_binlog_mode (int mode);
void tgl_set_binlog_path (const char *path);
void tgl_set_auth_file_path (const char *path);
void tgl_set_download_directory (const char *path);
void tgl_set_callback (struct tgl_update_callback *cb);
void tgl_set_rsa_key (const char *key);


static inline int tgl_get_peer_type (tgl_peer_id_t id) {
  return id.type;
}

static inline int tgl_get_peer_id (tgl_peer_id_t id) {
  return id.id;
}

static inline tgl_peer_id_t tgl_set_peer_id (int type, int id) {
  tgl_peer_id_t ID;
  ID.id = id;
  ID.type = type;
  return ID;
}

static inline int tgl_cmp_peer_id (tgl_peer_id_t a, tgl_peer_id_t b) {
  return memcmp (&a, &b, sizeof (a));
}

static inline void tgl_incr_verbosity (void) {
  tgl_state.verbosity ++;
}

static inline void tgl_set_verbosity (int val) {
  tgl_state.verbosity = val;
}

static inline void tgl_enable_pfs (void) {
  tgl_state.enable_pfs = 1;
}

static inline void tgl_set_test_mode (void) {
  tgl_state.test_mode ++;
}

struct pollfd;
int tgl_connections_make_poll_array (struct pollfd *fds, int max);
void tgl_connections_poll_result (struct pollfd *fds, int max);

void tgl_do_help_get_config (void (*callback)(void *callback_extra, int success), void *callback_extra);
void tgl_do_send_code (const char *user, void (*callback)(void *callback_extra, int success, int registered, const char *hash), void *callback_extra);
void tgl_do_phone_call (const char *user, const char *hash, void (*callback)(void *callback_extra, int success), void *callback_extra);
int tgl_do_send_code_result (const char *user, const char *hash, const char *code, void (*callback)(void *callback_extra, int success, struct tgl_user *Self), void *callback_extra) ;
int tgl_do_send_code_result_auth (const char *user, const char *hash, const char *code, const char *first_name, const char *last_name, void (*callback)(void *callback_extra, int success, struct tgl_user *Self), void *callback_extra);
void tgl_do_update_contact_list (void (*callback) (void *callback_extra, int success, int size, struct tgl_user *contacts[]), void *callback_extra);
void tgl_do_send_message (tgl_peer_id_t id, const char *msg, int len, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_send_msg (struct tgl_message *M, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_send_text (tgl_peer_id_t id, char *file, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_mark_read (tgl_peer_id_t id, void (*callback)(void *callback_extra, int success), void *callback_extra);
void tgl_do_get_history (tgl_peer_id_t id, int limit, int offline_mode, void (*callback)(void *callback_extra, int success, int size, struct tgl_message *list[]), void *callback_extra);
void tgl_do_get_dialog_list (void (*callback)(void *callback_extra, int success, int size, tgl_peer_id_t peers[], int last_msg_id[], int unread_count[]), void *callback_extra);
void tgl_do_send_photo (enum tgl_message_media_type type, tgl_peer_id_t to_id, char *file_name, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_set_chat_photo (tgl_peer_id_t chat_id, char *file_name, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_set_profile_photo (char *file_name, void (*callback)(void *callback_extra, int success), void *callback_extra);
void tgl_do_forward_message (tgl_peer_id_t id, int n, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_rename_chat (tgl_peer_id_t id, char *name, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_get_chat_info (tgl_peer_id_t id, int offline_mode, void (*callback)(void *callback_extra, int success, struct tgl_chat *C), void *callback_extra);
void tgl_do_get_user_info (tgl_peer_id_t id, int offline_mode, void (*callback)(void *callback_extra, int success, struct tgl_user *U), void *callback_extra);
void tgl_do_load_photo (struct tgl_photo *photo, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_load_video_thumb (struct tgl_video *video, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_load_audio (struct tgl_audio *V, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_load_video (struct tgl_video *V, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_load_document (struct tgl_document *V, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_load_document_thumb (struct tgl_document *video, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_load_encr_video (struct tgl_encr_video *V, void (*callback)(void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_export_auth (int num, void (*callback) (void *callback_extra, int success), void *callback_extra);
void tgl_do_add_contact (const char *phone, int phone_len, const char *first_name, int first_name_len, const char *last_name, int last_name_len, int force, void (*callback)(void *callback_extra, int success, int size, struct tgl_user *users[]), void *callback_extra);
void tgl_do_msg_search (tgl_peer_id_t id, int from, int to, int limit, const char *s, void (*callback)(void *callback_extra, int success, int size, struct tgl_message *list[]), void *callback_extra);
void tgl_do_contacts_search (int limit, const char *s, void (*callback) (void *callback_extra, int success, int size, struct tgl_user *users[]), void *callback_extra);
void tgl_do_create_encr_chat_request (int user_id, void (*callback)(void *callback_extra, int success, struct tgl_secret_chat *E), void *callback_extra);
void tgl_do_create_secret_chat (tgl_peer_id_t id, void (*callback)(void *callback_extra, int success, struct tgl_secret_chat *E), void *callback_extra);
void tgl_do_accept_encr_chat_request (struct tgl_secret_chat *E, void (*callback)(void *callback_extra, int success, struct tgl_secret_chat *E), void *callback_extra);
void tgl_do_get_difference (int sync_from_start, void (*callback)(void *callback_extra, int success), void *callback_extra);
void tgl_do_add_user_to_chat (tgl_peer_id_t chat_id, tgl_peer_id_t id, int limit, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_del_user_from_chat (tgl_peer_id_t chat_id, tgl_peer_id_t id, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_create_group_chat (tgl_peer_id_t id, char *chat_topic, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_delete_msg (long long id, void (*callback)(void *callback_extra, int success), void *callback_extra);
void tgl_do_restore_msg (long long id, void (*callback)(void *callback_extra, int success), void *callback_extra);
void tgl_do_update_status (int online, void (*callback)(void *callback_extra, int success), void *callback_extra);
void tgl_do_help_get_config_dc (struct tgl_dc *D, void (*callback)(void *, int), void *callback_extra);
void tgl_do_export_card (void (*callback)(void *callback_extra, int success, int size, int *card), void *callback_extra);
void tgl_do_import_card (int size, int *card, void (*callback)(void *callback_extra, int success, struct tgl_user *U), void *callback_extra);
void tgl_do_send_contact (tgl_peer_id_t id, const char *phone, int phone_len, const char *first_name, int first_name_len, const char *last_name, int last_name_len, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_forward_media (tgl_peer_id_t id, int n, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_del_contact (tgl_peer_id_t id, void (*callback)(void *callback_extra, int success), void *callback_extra);
void tgl_do_set_encr_chat_ttl (struct tgl_secret_chat *E, int ttl, void (*callback)(void *callback_extra, int success, struct tgl_message *M), void *callback_extra);


void tgl_do_visualize_key (tgl_peer_id_t id, unsigned char buf[16]);

void tgl_do_send_ping (struct connection *c);

int tgl_authorized_dc (struct tgl_dc *DC);
int tgl_signed_dc (struct tgl_dc *DC);

//void tgl_do_get_suggested (void);

void tgl_do_create_keys_end (struct tgl_secret_chat *U);
void tgl_do_send_encr_chat_layer (struct tgl_secret_chat *E);

struct mtproto_methods {
  int (*ready) (struct connection *c);
  int (*close) (struct connection *c);
  int (*execute) (struct connection *c, int op, int len);
};

void tgl_init (void);
void tgl_dc_authorize (struct tgl_dc *DC);

void tgl_dc_iterator (void (*iterator)(struct tgl_dc *DC));
void tgl_dc_iterator_ex (void (*iterator)(struct tgl_dc *DC, void *extra), void *extra);

double tglt_get_double_time (void);

void tgl_insert_empty_user (int id);
void tgl_insert_empty_chat (int id);

int tglf_extf_autocomplete (const char *text, int text_len, int index, char **R, char *data, int data_len);
struct paramed_type *tglf_extf_store (const char *data, int data_len);
void tgl_do_send_extf (char *data, int data_len, void (*callback)(void *callback_extra, int success, char *data), void *callback_extra);
char *tglf_extf_fetch (struct paramed_type *T);

#endif
