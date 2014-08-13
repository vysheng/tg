#ifndef __TGL_H__
#define __TGL_H__

#include <tgl-layout.h>
#include <string.h>

#define TGL_MAX_DC_NUM 100

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

  struct dc *DC_list[TGL_MAX_DC_NUM];
  struct dc *DC_working;
  int dc_working_num;

  long long cur_uploading_bytes;
  long long cur_uploaded_bytes;
  long long cur_downloading_bytes;
  long long cur_downloaded_bytes;
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

static inline void tgl_set_test_mode (void) {
  tgl_state.test_mode ++;
}

void tgl_do_send_code (const char *user);
void tgl_do_phone_call (const char *user);
int tgl_do_send_code_result (const char *code);
void tgl_do_update_contact_list (void);
void tgl_do_send_message (tgl_peer_id_t id, const char *msg, int len);
void tgl_do_send_text (tgl_peer_id_t id, char *file);
void tgl_do_get_history (tgl_peer_id_t id, int limit);
void tgl_do_get_dialog_list (void);
void tgl_do_get_dialog_list_ex (void);
void tgl_do_send_photo (int type, tgl_peer_id_t to_id, char *file_name);
void tgl_do_get_chat_info (tgl_peer_id_t id);
void tgl_do_get_user_list_info_silent (int num, int *list);
void tgl_do_get_user_info (tgl_peer_id_t id);
void tgl_do_forward_message (tgl_peer_id_t id, int n);
void tgl_do_rename_chat (tgl_peer_id_t id, char *name);
void tgl_do_load_encr_video (struct tgl_encr_video *V, int next);
void tgl_do_create_encr_chat_request (int user_id);
void tgl_do_create_secret_chat (tgl_peer_id_t id);
void tgl_do_create_group_chat (tgl_peer_id_t id, char *chat_topic);
void tgl_do_get_suggested (void);

void tgl_do_load_photo (struct tgl_photo *photo, int next);
void tgl_do_load_video_thumb (struct tgl_video *video, int next);
void tgl_do_load_audio (struct tgl_video *V, int next);
void tgl_do_load_video (struct tgl_video *V, int next);
void tgl_do_load_document (struct tgl_document *V, int next);
void tgl_do_load_document_thumb (struct tgl_document *video, int next);
void tgl_do_help_get_config (void);
int tgl_do_auth_check_phone (const char *user);
int tgl_do_get_nearest_dc (void);
int tgl_do_send_code_result_auth (const char *code, const char *first_name, const char *last_name);
void tgl_do_import_auth (int num);
void tgl_do_export_auth (int num);
void tgl_do_add_contact (const char *phone, int phone_len, const char *first_name, int first_name_len, const char *last_name, int last_name_len, int force);
void tgl_do_msg_search (tgl_peer_id_t id, int from, int to, int limit, const char *s);
void tgl_do_accept_encr_chat_request (struct tgl_secret_chat *E);
void tgl_do_get_difference (void);
void tgl_do_mark_read (tgl_peer_id_t id);
void tgl_do_visualize_key (tgl_peer_id_t id);
void tgl_do_create_keys_end (struct tgl_secret_chat *U);
void tgl_do_add_user_to_chat (tgl_peer_id_t chat_id, tgl_peer_id_t id, int limit);
void tgl_do_del_user_from_chat (tgl_peer_id_t chat_id, tgl_peer_id_t id);
void tgl_do_update_status (int online);
void tgl_do_contacts_search (int limit, const char *s);
void tgl_do_send_msg (struct tgl_message *M);
void tgl_do_delete_msg (long long id);
void tgl_do_restore_msg (long long id);
void tgl_do_send_encr_chat_layer (struct tgl_secret_chat *E);
#endif
