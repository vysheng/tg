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
#ifndef __STRUCTURES_H__
#define __STRUCTURES_H__

#include <assert.h>
#include "tgl-layout.h"
#include "tgl-fetch.h"

char *tgls_default_create_print_name (tgl_peer_id_t id, const char *a1, const char *a2, const char *a3, const char *a4);


void tgls_free_user (struct tgl_user *U);
void tgls_free_chat (struct tgl_chat *U);
void tgls_free_photo (struct tgl_photo *P);
void tgls_free_message (struct tgl_message *M);

struct tgl_message *tglm_message_alloc (long long id);
void tglm_message_insert_tree (struct tgl_message *M);
void tglm_update_message_id (struct tgl_message *M, long long id);
void tglm_message_insert (struct tgl_message *M);
void tglm_message_insert_unsent (struct tgl_message *M);
void tglm_message_remove_unsent (struct tgl_message *M);
void tglm_send_all_unsent (void);
void tglm_message_remove_tree (struct tgl_message *M);
void tglm_message_add_peer (struct tgl_message *M);
void tglm_message_del_peer (struct tgl_message *M);
void tglm_message_del_use (struct tgl_message *M);
void tglm_message_add_use (struct tgl_message *M);

void tglp_peer_insert_name (tgl_peer_t *P);
void tglp_peer_delete_name (tgl_peer_t *P);
void tglp_insert_encrypted_chat (tgl_peer_t *P);
void tglp_insert_user (tgl_peer_t *P);
void tglp_insert_chat (tgl_peer_t *P);

#endif
