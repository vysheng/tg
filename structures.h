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
#ifndef __STRUCTURES_H__
#define __STRUCTURES_H__

#include <assert.h>
#include "tgl-layout.h"
#include "tgl-fetch.h"

char *create_print_name (peer_id_t id, const char *a1, const char *a2, const char *a3, const char *a4);

struct message *message_alloc (long long id);

void free_user (struct user *U);
void free_chat (struct chat *U);


int tgl_print_stat (char *s, int len);
peer_t *peer_get (peer_id_t id);
peer_t *peer_lookup_name (const char *s);
struct message *message_get (long long id);



void message_insert_tree (struct message *M);
void update_message_id (struct message *M, long long id);
void message_insert (struct message *M);
void free_photo (struct photo *P);
void insert_encrypted_chat (peer_t *P);
void insert_user (peer_t *P);
void insert_chat (peer_t *P);
void message_insert_unsent (struct message *M);
void message_remove_unsent (struct message *M);
void send_all_unsent (void);
void message_remove_tree (struct message *M);
void message_add_peer (struct message *M);
void message_del_peer (struct message *M);
void free_message (struct message *M);
void message_del_use (struct message *M);
void peer_insert_name (peer_t *P);
void peer_delete_name (peer_t *P);
void peer_iterator_ex (void (*it)(peer_t *P, void *extra), void *extra);

int complete_user_list (int index, const char *text, int len, char **R);
int complete_chat_list (int index, const char *text, int len, char **R);
int complete_encr_chat_list (int index, const char *text, int len, char **R);
int complete_peer_list (int index, const char *text, int len, char **R);
#define PEER_USER 1
#define PEER_CHAT 2
#define PEER_GEO_CHAT 3
#define PEER_ENCR_CHAT 4
#define PEER_UNKNOWN 0

#define MK_USER(id) set_peer_id (PEER_USER,id)
#define MK_CHAT(id) set_peer_id (PEER_CHAT,id)
#define MK_GEO_CHAT(id) set_peer_id (PEER_GEO_CHAT,id)
#define MK_ENCR_CHAT(id) set_peer_id (PEER_ENCR_CHAT,id)

static inline int get_peer_type (peer_id_t id) {
  return id.type;
}

static inline int get_peer_id (peer_id_t id) {
  return id.id;
}

static inline peer_id_t set_peer_id (int type, int id) {
  peer_id_t ID;
  ID.id = id;
  ID.type = type;
  return ID;
}

static inline int cmp_peer_id (peer_id_t a, peer_id_t b) {
  return memcmp (&a, &b, sizeof (a));
}

#endif
