#ifndef __TGL_FETCH_H__
#define __TGL_FETCH_H__

int tglf_fetch_file_location (struct file_location *loc);
int tglf_fetch_user_status (struct user_status *S);
int tglf_fetch_user (struct user *U);
struct user *tglf_fetch_alloc_user (void);
struct user *tglf_fetch_alloc_user_full (void);
struct chat *tglf_fetch_alloc_chat (void);
struct chat *tglf_fetch_alloc_chat_full (void);
struct secret_chat *tglf_fetch_alloc_encrypted_chat (void);
struct message *tglf_fetch_alloc_message (void);
struct message *tglf_fetch_alloc_geo_message (void);
struct message *tglf_fetch_alloc_message_short (void);
struct message *tglf_fetch_alloc_message_short_chat (void);
struct message *tglf_fetch_alloc_encrypted_message (void);
void tglf_fetch_encrypted_message_file (struct message_media *M);
peer_id_t tglf_fetch_peer_id (void);

void tglf_fetch_message_media (struct message_media *M);
void tglf_fetch_message_media_encrypted (struct message_media *M);
void tglf_fetch_message_action (struct message_action *M);
void tglf_fetch_message_action_encrypted (struct message_action *M);
void tglf_fetch_photo (struct photo *P);
#endif
