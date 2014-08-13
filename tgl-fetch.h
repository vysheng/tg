#ifndef __TGL_FETCH_H__
#define __TGL_FETCH_H__

int tglf_fetch_file_location (struct tgl_file_location *loc);
int tglf_fetch_user_status (struct tgl_user_status *S);
int tglf_fetch_user (struct tgl_user *U);
struct tgl_user *tglf_fetch_alloc_user (void);
struct tgl_user *tglf_fetch_alloc_user_full (void);
struct tgl_chat *tglf_fetch_alloc_chat (void);
struct tgl_chat *tglf_fetch_alloc_chat_full (void);
struct tgl_secret_chat *tglf_fetch_alloc_encrypted_chat (void);
struct tgl_message *tglf_fetch_alloc_message (void);
struct tgl_message *tglf_fetch_alloc_geo_message (void);
struct tgl_message *tglf_fetch_alloc_message_short (void);
struct tgl_message *tglf_fetch_alloc_message_short_chat (void);
struct tgl_message *tglf_fetch_alloc_encrypted_message (void);
void tglf_fetch_encrypted_message_file (struct tgl_message_media *M);
tgl_peer_id_t tglf_fetch_peer_id (void);

void tglf_fetch_message_media (struct tgl_message_media *M);
void tglf_fetch_message_media_encrypted (struct tgl_message_media *M);
void tglf_fetch_message_action (struct tgl_message_action *M);
void tglf_fetch_message_action_encrypted (struct tgl_message_action *M);
void tglf_fetch_photo (struct tgl_photo *P);

void tglf_fetch_chat (struct tgl_chat *C);
void tglf_fetch_chat_full (struct tgl_chat *C);

void tglf_fetch_audio (struct tgl_audio *V);
void tglf_fetch_video (struct tgl_video *V);
void tglf_fetch_document (struct tgl_document *V);
void tglf_fetch_message (struct tgl_message *M);
void tglf_fetch_geo_message (struct tgl_message *M);
#endif
