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
#include <stdlib.h>

#define TGL_MAX_DC_NUM 100
#define TG_SERVER_1 "173.240.5.1"
#define TG_SERVER_2 "149.154.167.51"
#define TG_SERVER_3 "174.140.142.6"
#define TG_SERVER_4 "149.154.167.91"
#define TG_SERVER_5 "149.154.171.5"
#define TG_SERVER_DEFAULT 4

#define TG_SERVER_TEST_1 "173.240.5.253"
#define TG_SERVER_TEST_2 "149.154.167.40"
#define TG_SERVER_TEST_3 "174.140.142.5"
#define TG_SERVER_TEST_DEFAULT 2

// JUST RANDOM STRING
#define TGL_BUILD "2590"
#define TGL_VERSION "1.1.0"

#define TGL_ENCRYPTED_LAYER 17

struct connection;
struct mtproto_methods;
struct tgl_session;
struct tgl_dc;

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
#define TGL_UPDATE_USERNAME (1 << 15)

struct tgl_allocator {
  void *(*alloc)(size_t size);
  void *(*realloc)(void *ptr, size_t old_size, size_t size);
  void (*free)(void *ptr, int size);
  void (*check)(void);
  void (*exists)(void *ptr, int size);
};
extern struct tgl_allocator tgl_allocator_release;
extern struct tgl_allocator tgl_allocator_debug;
struct tgl_state;

struct tgl_update_callback {
  void (*new_msg)(struct tgl_state *TLS, struct tgl_message *M);
  void (*marked_read)(struct tgl_state *TLS, int num, struct tgl_message *list[]);
  void (*logprintf)(const char *format, ...)  __attribute__ ((format (printf, 1, 2)));
  void (*type_notification)(struct tgl_state *TLS, struct tgl_user *U, enum tgl_typing_status status);
  void (*type_in_chat_notification)(struct tgl_state *TLS, struct tgl_user *U, struct tgl_chat *C, enum tgl_typing_status status);
  void (*type_in_secret_chat_notification)(struct tgl_state *TLS, struct tgl_secret_chat *E);
  void (*status_notification)(struct tgl_state *TLS, struct tgl_user *U);
  void (*user_registered)(struct tgl_state *TLS, struct tgl_user *U);
  void (*user_activated)(struct tgl_state *TLS, struct tgl_user *U);
  void (*new_authorization)(struct tgl_state *TLS, const char *device, const char *location);
  void (*chat_update)(struct tgl_state *TLS, struct tgl_chat *C, unsigned flags);
  void (*user_update)(struct tgl_state *TLS, struct tgl_user *C, unsigned flags);
  void (*secret_chat_update)(struct tgl_state *TLS, struct tgl_secret_chat *C, unsigned flags);
  void (*msg_receive)(struct tgl_state *TLS, struct tgl_message *M);
  void (*our_id)(struct tgl_state *TLS, int id);
  void (*notification)(struct tgl_state *TLS, char *type, char *message);
  void (*user_status_update)(struct tgl_state *TLS, struct tgl_user *U);
  char *(*create_print_name) (struct tgl_state *TLS, tgl_peer_id_t id, const char *a1, const char *a2, const char *a3, const char *a4);
};

struct tgl_net_methods {
  int (*write_out) (struct connection *c, const void *data, int len);
  int (*read_in) (struct connection *c, void *data, int len);
  int (*read_in_lookup) (struct connection *c, void *data, int len);
  void (*flush_out) (struct connection *c);
  void (*incr_out_packet_num) (struct connection *c);
  void (*free) (struct connection *c);
  struct tgl_dc *(*get_dc) (struct connection *c);
  struct tgl_session *(*get_session) (struct connection *c);

  struct connection *(*create_connection) (struct tgl_state *TLS, const char *host, int port, struct tgl_session *session, struct tgl_dc *dc, struct mtproto_methods *methods);
};

struct mtproto_methods {
  int (*ready) (struct tgl_state *TLS, struct connection *c);
  int (*close) (struct tgl_state *TLS, struct connection *c);
  int (*execute) (struct tgl_state *TLS, struct connection *c, int op, int len);
};

struct tgl_timer;

struct tgl_timer_methods {
  struct tgl_timer *(*alloc) (struct tgl_state *TLS, void (*cb)(struct tgl_state *TLS, void *arg), void *arg);
  void (*insert) (struct tgl_timer *t, double timeout);
  void (*delete) (struct tgl_timer *t);
  void (*free) (struct tgl_timer *t);
};

#define E_ERROR 0
#define E_WARNING 1
#define E_NOTICE 2
#define E_DEBUG 6

#define TGL_LOCK_DIFF 1

#define TGL_MAX_RSA_KEYS_NUM 10
// Do not modify this structure, unless you know what you do

#pragma pack(push,4)
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
  int started;

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
  void *ev_base;

  char *rsa_key_list[TGL_MAX_RSA_KEYS_NUM];
  int rsa_key_num;
  struct bignum_ctx *BN_ctx;

  struct tgl_allocator allocator;

  struct tree_peer *peer_tree;
  struct tree_peer_by_name *peer_by_name_tree;
  struct tree_message *message_tree;
  struct tree_message *message_unsent_tree;

  int users_allocated;
  int chats_allocated;
  int messages_allocated;
  int peer_num;
  int peer_size;
  int encr_chats_allocated;
  int geo_chats_allocated;

  tgl_peer_t **Peers;
  struct tgl_message message_list;

  int binlog_fd;

  struct tgl_timer_methods *timer_methods;

  void *pubKey;

  struct tree_query *queries_tree;

  char *base_path; 
  
  struct tree_user *online_updates;

  struct tgl_timer *online_updates_timer;
};
#pragma pack(pop)
//extern struct tgl_state tgl_state;

void tgl_reopen_binlog_for_writing (struct tgl_state *TLS);
void tgl_replay_log (struct tgl_state *TLS);

int tgl_print_stat (struct tgl_state *TLS, char *s, int len);
tgl_peer_t *tgl_peer_get (struct tgl_state *TLS, tgl_peer_id_t id);
tgl_peer_t *tgl_peer_get_by_name (struct tgl_state *TLS, const char *s);

struct tgl_message *tgl_message_get (struct tgl_state *TLS, long long id);
void tgl_peer_iterator_ex (struct tgl_state *TLS, void (*it)(tgl_peer_t *P, void *extra), void *extra);

int tgl_complete_user_list (struct tgl_state *TLS, int index, const char *text, int len, char **R);
int tgl_complete_chat_list (struct tgl_state *TLS, int index, const char *text, int len, char **R);
int tgl_complete_encr_chat_list (struct tgl_state *TLS, int index, const char *text, int len, char **R);
int tgl_complete_peer_list (struct tgl_state *TLS, int index, const char *text, int len, char **R);

#define TGL_PEER_USER 1
#define TGL_PEER_CHAT 2
#define TGL_PEER_GEO_CHAT 3
#define TGL_PEER_ENCR_CHAT 4
#define TGL_PEER_UNKNOWN 0

#define TGL_MK_USER(id) tgl_set_peer_id (TGL_PEER_USER,id)
#define TGL_MK_CHAT(id) tgl_set_peer_id (TGL_PEER_CHAT,id)
#define TGL_MK_GEO_CHAT(id) tgl_set_peer_id (TGL_PEER_GEO_CHAT,id)
#define TGL_MK_ENCR_CHAT(id) tgl_set_peer_id (TGL_PEER_ENCR_CHAT,id)

void tgl_set_binlog_mode (struct tgl_state *TLS, int mode);
void tgl_set_binlog_path (struct tgl_state *TLS, const char *path);
void tgl_set_auth_file_path (struct tgl_state *TLS, const char *path);
void tgl_set_download_directory (struct tgl_state *TLS, const char *path);
void tgl_set_callback (struct tgl_state *TLS, struct tgl_update_callback *cb);
void tgl_set_rsa_key (struct tgl_state *TLS, const char *key);


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

static inline void tgl_incr_verbosity (struct tgl_state *TLS) {
  TLS->verbosity ++;
}

static inline void tgl_set_verbosity (struct tgl_state *TLS, int val) {
  TLS->verbosity = val;
}

static inline void tgl_enable_pfs (struct tgl_state *TLS) {
  TLS->enable_pfs = 1;
}

static inline void tgl_set_test_mode (struct tgl_state *TLS) {
  TLS->test_mode ++;
}

static inline void tgl_set_net_methods (struct tgl_state *TLS, struct tgl_net_methods *methods) {
  TLS->net_methods = methods;
}

static inline void tgl_set_timer_methods (struct tgl_state *TLS, struct tgl_timer_methods *methods) {
  TLS->timer_methods = methods;
}

static inline void tgl_set_ev_base (struct tgl_state *TLS, void *ev_base) {
  TLS->ev_base = ev_base;
}

//struct pollfd;
//int tgl_connections_make_poll_array (struct tgl_state *TLS, struct pollfd *fds, int max);
//void tgl_connections_poll_result (struct tgl_state *TLS, struct pollfd *fds, int max);

void tgl_do_help_get_config (struct tgl_state *TLS, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success), void *callback_extra);
void tgl_do_send_code (struct tgl_state *TLS, const char *user, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, int registered, const char *hash), void *callback_extra);
void tgl_do_phone_call (struct tgl_state *TLS, const char *user, const char *hash, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success), void *callback_extra);
int tgl_do_send_code_result (struct tgl_state *TLS, const char *user, const char *hash, const char *code, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_user *Self), void *callback_extra) ;
int tgl_do_send_code_result_auth (struct tgl_state *TLS, const char *user, const char *hash, const char *code, const char *first_name, const char *last_name, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_user *Self), void *callback_extra);
void tgl_do_update_contact_list (struct tgl_state *TLS, void (*callback) (struct tgl_state *TLS, void *callback_extra, int success, int size, struct tgl_user *contacts[]), void *callback_extra);
void tgl_do_send_message (struct tgl_state *TLS, tgl_peer_id_t id, const char *msg, int len, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_send_msg (struct tgl_state *TLS, struct tgl_message *M, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_send_text (struct tgl_state *TLS, tgl_peer_id_t id, char *file, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_mark_read (struct tgl_state *TLS, tgl_peer_id_t id, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success), void *callback_extra);
void tgl_do_get_history (struct tgl_state *TLS, tgl_peer_id_t id, int limit, int offline_mode, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, int size, struct tgl_message *list[]), void *callback_extra);
void tgl_do_get_history_ext (struct tgl_state *TLS, tgl_peer_id_t id, int offset, int limit, int offline_mode, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, int size, struct tgl_message *list[]), void *callback_extra);
void tgl_do_get_dialog_list (struct tgl_state *TLS, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, int size, tgl_peer_id_t peers[], int last_msg_id[], int unread_count[]), void *callback_extra);
void tgl_do_send_photo (struct tgl_state *TLS, enum tgl_message_media_type type, tgl_peer_id_t to_id, char *file_name, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_set_chat_photo (struct tgl_state *TLS, tgl_peer_id_t chat_id, char *file_name, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_set_profile_photo (struct tgl_state *TLS, char *file_name, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success), void *callback_extra);
void tgl_do_set_profile_name (struct tgl_state *TLS, char *first_name, char *last_name, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_user *U), void *callback_extra);
void tgl_do_set_username (struct tgl_state *TLS, char *name, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_user *U), void *callback_extra);
void tgl_do_forward_message (struct tgl_state *TLS, tgl_peer_id_t id, int n, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_rename_chat (struct tgl_state *TLS, tgl_peer_id_t id, char *name, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_get_chat_info (struct tgl_state *TLS, tgl_peer_id_t id, int offline_mode, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_chat *C), void *callback_extra);
void tgl_do_get_user_info (struct tgl_state *TLS, tgl_peer_id_t id, int offline_mode, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_user *U), void *callback_extra);
void tgl_do_load_photo (struct tgl_state *TLS, struct tgl_photo *photo, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_load_video_thumb (struct tgl_state *TLS, struct tgl_video *video, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_load_audio (struct tgl_state *TLS, struct tgl_audio *V, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_load_video (struct tgl_state *TLS, struct tgl_video *V, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_load_document (struct tgl_state *TLS, struct tgl_document *V, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_load_document_thumb (struct tgl_state *TLS, struct tgl_document *video, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_load_encr_video (struct tgl_state *TLS, struct tgl_encr_video *V, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, char *filename), void *callback_extra);
void tgl_do_export_auth (struct tgl_state *TLS, int num, void (*callback) (struct tgl_state *TLS, void *callback_extra, int success), void *callback_extra);
void tgl_do_add_contact (struct tgl_state *TLS, const char *phone, int phone_len, const char *first_name, int first_name_len, const char *last_name, int last_name_len, int force, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, int size, struct tgl_user *users[]), void *callback_extra);
void tgl_do_msg_search (struct tgl_state *TLS, tgl_peer_id_t id, int from, int to, int limit, int offset, const char *s, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, int size, struct tgl_message *list[]), void *callback_extra);
//void tgl_do_contacts_search (int limit, const char *s, void (*callback) (struct tgl_state *TLS, void *callback_extra, int success, int size, struct tgl_user *users[]), void *callback_extra);
void tgl_do_create_encr_chat_request (struct tgl_state *TLS, int user_id, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_secret_chat *E), void *callback_extra);
void tgl_do_create_secret_chat (struct tgl_state *TLS, tgl_peer_id_t id, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_secret_chat *E), void *callback_extra);
void tgl_do_accept_encr_chat_request (struct tgl_state *TLS, struct tgl_secret_chat *E, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_secret_chat *E), void *callback_extra);
void tgl_do_get_difference (struct tgl_state *TLS, int sync_from_start, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success), void *callback_extra);
void tgl_do_lookup_state (struct tgl_state *TLS);
void tgl_do_add_user_to_chat (struct tgl_state *TLS, tgl_peer_id_t chat_id, tgl_peer_id_t id, int limit, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_del_user_from_chat (struct tgl_state *TLS, tgl_peer_id_t chat_id, tgl_peer_id_t id, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_create_group_chat (struct tgl_state *TLS, tgl_peer_id_t id, char *chat_topic, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_create_group_chat_ex (struct tgl_state *TLS, int users_num, tgl_peer_id_t ids[], char *chat_topic, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_delete_msg (struct tgl_state *TLS, long long id, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success), void *callback_extra);
void tgl_do_restore_msg (struct tgl_state *TLS, long long id, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success), void *callback_extra);
void tgl_do_update_status (struct tgl_state *TLS, int online, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success), void *callback_extra);
void tgl_do_help_get_config_dc (struct tgl_state *TLS, struct tgl_dc *D, void (*callback)(struct tgl_state *TLS, void *, int), void *callback_extra);
void tgl_do_export_card (struct tgl_state *TLS, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, int size, int *card), void *callback_extra);
void tgl_do_import_card (struct tgl_state *TLS, int size, int *card, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_user *U), void *callback_extra);
void tgl_do_send_contact (struct tgl_state *TLS, tgl_peer_id_t id, const char *phone, int phone_len, const char *first_name, int first_name_len, const char *last_name, int last_name_len, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_forward_media (struct tgl_state *TLS, tgl_peer_id_t id, int n, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_del_contact (struct tgl_state *TLS, tgl_peer_id_t id, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success), void *callback_extra);
void tgl_do_set_encr_chat_ttl (struct tgl_state *TLS, struct tgl_secret_chat *E, int ttl, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_send_location (struct tgl_state *TLS, tgl_peer_id_t id, double latitude, double longitude, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M), void *callback_extra);
void tgl_do_contact_search (struct tgl_state *TLS, char *name, int limit, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, int cnt, struct tgl_user *U[]), void *callback_extra);


void tgl_do_visualize_key (struct tgl_state *TLS, tgl_peer_id_t id, unsigned char buf[16]);

void tgl_do_send_ping (struct tgl_state *TLS, struct connection *c);

void tgl_do_send_extf (struct tgl_state *TLS, char *data, int data_len, void (*callback)(struct tgl_state *TLS, void *callback_extra, int success, char *data), void *callback_extra);

int tgl_authorized_dc (struct tgl_state *TLS, struct tgl_dc *DC);
int tgl_signed_dc (struct tgl_state *TLS, struct tgl_dc *DC);

//void tgl_do_get_suggested (void);

void tgl_do_create_keys_end (struct tgl_state *TLS, struct tgl_secret_chat *U);
void tgl_do_send_encr_chat_layer (struct tgl_state *TLS, struct tgl_secret_chat *E);

void tgl_init (struct tgl_state *TLS);
void tgl_dc_authorize (struct tgl_state *TLS, struct tgl_dc *DC);

void tgl_dc_iterator (struct tgl_state *TLS, void (*iterator)(struct tgl_dc *DC));
void tgl_dc_iterator_ex (struct tgl_state *TLS, void (*iterator)(struct tgl_dc *DC, void *extra), void *extra);

double tglt_get_double_time (void);

void tgl_insert_empty_user (struct tgl_state *TLS, int id);
void tgl_insert_empty_chat (struct tgl_state *TLS, int id);

int tglf_extf_autocomplete (struct tgl_state *TLS, const char *text, int text_len, int index, char **R, char *data, int data_len);
struct paramed_type *tglf_extf_store (struct tgl_state *TLS, const char *data, int data_len);
char *tglf_extf_fetch (struct tgl_state *TLS, struct paramed_type *T);

void tgl_free_all (struct tgl_state *TLS);

static inline struct tgl_state *tgl_state_alloc (void) {
  struct tgl_state *TLS = malloc (sizeof (*TLS));
  if (!TLS) { return NULL; }
  memset (TLS, 0, sizeof (*TLS));
  return TLS;
}

#endif
