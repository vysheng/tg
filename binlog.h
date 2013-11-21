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
#endif
