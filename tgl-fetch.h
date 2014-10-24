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
#ifndef __TGL_FETCH_H__
#define __TGL_FETCH_H__
#include "tgl.h"

int tglf_fetch_file_location (struct tgl_state *TLS, struct tgl_file_location *loc);
int tglf_fetch_user_status (struct tgl_state *TLS, struct tgl_user_status *S);
int tglf_fetch_user (struct tgl_state *TLS, struct tgl_user *U);
struct tgl_user *tglf_fetch_alloc_user (struct tgl_state *TLS);
struct tgl_user *tglf_fetch_alloc_user_full (struct tgl_state *TLS);
struct tgl_chat *tglf_fetch_alloc_chat (struct tgl_state *TLS);
struct tgl_chat *tglf_fetch_alloc_chat_full (struct tgl_state *TLS);
struct tgl_secret_chat *tglf_fetch_alloc_encrypted_chat (struct tgl_state *TLS);
struct tgl_message *tglf_fetch_alloc_message (struct tgl_state *TLS);
struct tgl_message *tglf_fetch_alloc_geo_message (struct tgl_state *TLS);
struct tgl_message *tglf_fetch_alloc_message_short (struct tgl_state *TLS);
struct tgl_message *tglf_fetch_alloc_message_short_chat (struct tgl_state *TLS);
struct tgl_message *tglf_fetch_alloc_encrypted_message (struct tgl_state *TLS);
void tglf_fetch_encrypted_message_file (struct tgl_state *TLS, struct tgl_message_media *M);
tgl_peer_id_t tglf_fetch_peer_id (struct tgl_state *TLS);

void tglf_fetch_message_media (struct tgl_state *TLS, struct tgl_message_media *M);
void tglf_fetch_message_media_encrypted (struct tgl_state *TLS, struct tgl_message_media *M);
void tglf_fetch_message_action (struct tgl_state *TLS, struct tgl_message_action *M);
void tglf_fetch_message_action_encrypted (struct tgl_state *TLS, struct tgl_message_action *M);
void tglf_fetch_photo (struct tgl_state *TLS, struct tgl_photo *P);

void tglf_fetch_chat (struct tgl_state *TLS, struct tgl_chat *C);
void tglf_fetch_chat_full (struct tgl_state *TLS, struct tgl_chat *C);

void tglf_fetch_audio (struct tgl_state *TLS, struct tgl_audio *V);
void tglf_fetch_video (struct tgl_state *TLS, struct tgl_video *V);
void tglf_fetch_document (struct tgl_state *TLS, struct tgl_document *V);
void tglf_fetch_message (struct tgl_state *TLS, struct tgl_message *M);
void tglf_fetch_geo_message (struct tgl_state *TLS, struct tgl_message *M);
#endif
