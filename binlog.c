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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef USE_LUA
#  include "lua-tg.h"
#endif
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <openssl/bn.h>

#include "binlog.h"
#include "mtproto-common.h"
#include "net.h"
#include "include.h"
#include "mtproto-client.h"
#include "loop.h"

#include "tgl.h"

#include <openssl/sha.h>

#define BINLOG_BUFFER_SIZE (1 << 20)
static int binlog_buffer[BINLOG_BUFFER_SIZE];
static int *rptr;
static int *wptr;

extern int test_dc;

extern int pts;
extern int qts;
extern int last_date;
extern int seq;

#define MAX_LOG_EVENT_SIZE (1 << 17)

char *get_binlog_file_name (void);
extern struct dc *DC_list[];
extern struct dc *DC_working;
extern int dc_working_num;
extern int binlog_enabled;
extern int encr_root;
extern unsigned char *encr_prime;
extern int encr_param_version;
extern int messages_allocated;

int in_replay_log;

void *alloc_log_event (int l UU) {
  return binlog_buffer;
}

static long long binlog_pos;

int fetch_comb_binlog_start (void *extra) {
  return 0;
}

int fetch_comb_binlog_dc_option (void *extra) {
  int id = fetch_int ();
  int l1 = prefetch_strlen ();
  assert (l1 >= 0);
  char *name = fetch_str (l1);
  int l2 = prefetch_strlen ();
  assert (l2 >= 0);
  char *ip = fetch_str (l2);
  int port = fetch_int ();

  vlogprintf (E_NOTICE, "DC%d '%s' update: %s:%d\n", id, name, ip, port);

  alloc_dc (id, tstrndup (ip, l2), port);
  return 0;
}

int fetch_comb_binlog_auth_key (void *extra) {
  int num = fetch_int ();
  assert (num >= 0 && num <= MAX_DC_ID);
  assert (DC_list[num]);
  DC_list[num]->auth_key_id = fetch_long ();
  fetch_ints (DC_list[num]->auth_key, 64);
  DC_list[num]->flags |= 1;
  return 0;
}

int fetch_comb_binlog_default_dc (void *extra) {
  int num = fetch_int ();
  assert (num >= 0 && num <= MAX_DC_ID);
  DC_working = DC_list[num];
  dc_working_num = num;
  return 0;
}

int fetch_comb_binlog_our_id (void *extra) {
  tgl_state.our_id = fetch_int ();
  #ifdef USE_LUA
    lua_our_id (tgl_state.our_id);
  #endif
  return 0;
}

int fetch_comb_binlog_dc_signed (void *extra) {
  int num = fetch_int ();
  assert (num >= 0 && num <= MAX_DC_ID);
  assert (DC_list[num]);
  DC_list[num]->has_auth = 1;
  return 0;
}

int fetch_comb_binlog_dc_salt (void *extra) {
  int num = fetch_int ();
  assert (num >= 0 && num <= MAX_DC_ID);
  assert (DC_list[num]);
  DC_list[num]->server_salt = fetch_long ();
  return 0;
}
      
int fetch_comb_binlog_set_dh_params (void *extra) {
  if (encr_prime) { tfree (encr_prime, 256); }
  encr_root = fetch_int ();
  encr_prime = talloc (256);
  fetch_ints (encr_prime, 64);
  encr_param_version = fetch_int ();

  return 0;
}

int fetch_comb_binlog_set_pts (void *extra) {
  int new_pts = fetch_int ();
  assert (new_pts >= pts);
  pts = new_pts;
  return 0;
}

int fetch_comb_binlog_set_qts (void *extra) {
  int new_qts = fetch_int ();
  //assert (new_qts >= qts);
  qts = new_qts;
  return 0;
}

int fetch_comb_binlog_set_date (void *extra) {
  int new_date = fetch_int ();
  assert (new_date >= last_date);
  last_date = new_date;
  return 0;
}

int fetch_comb_binlog_set_seq (void *extra) {
  int new_seq = fetch_int ();
  assert (new_seq >= seq);
  seq = new_seq;
  return 0;
}

int fetch_comb_binlog_user_add (void *extra) {
  peer_id_t id = MK_USER (fetch_int ());
  peer_t *_U = peer_get (id);
  if (!_U) {
    _U = talloc0 (sizeof (*_U));
    _U->id = id;
    insert_user (_U);
  } else {
    assert (!(_U->flags & FLAG_CREATED));
  }
  struct user *U = (void *)_U;
  U->flags |= FLAG_CREATED;
  if (get_peer_id (id) == tgl_state.our_id) {
    U->flags |= FLAG_USER_SELF;
  }
  U->first_name = fetch_str_dup ();
  U->last_name = fetch_str_dup ();
  assert (!U->print_name);
  U->print_name = create_print_name (U->id, U->first_name, U->last_name, 0, 0);

  peer_insert_name ((void *)U);
  U->access_hash = fetch_long ();
  U->phone = fetch_str_dup ();
  if (fetch_int ()) {
    U->flags |= FLAG_USER_CONTACT;
  }
      
  #ifdef USE_LUA
    lua_user_update (U);
  #endif
  return 0;
}

int fetch_comb_binlog_user_delete (void *extra) {
  peer_id_t id = MK_USER (fetch_int ());
  peer_t *U = peer_get (id);
  assert (U);
  U->flags |= FLAG_DELETED;
  
  #ifdef USE_LUA
    lua_user_update (&U->user);
  #endif
  return 0;
}

int fetch_comb_binlog_user_set_access_hash (void *extra) {
  peer_id_t id = MK_USER (fetch_int ());
  peer_t *U = peer_get (id);
  assert (U);
  U->user.access_hash = fetch_long ();
  
  #ifdef USE_LUA
    lua_user_update (&U->user);
  #endif
  return 0;
}

int fetch_comb_binlog_user_set_phone (void *extra) {
  peer_id_t id = MK_USER (fetch_int ());
  peer_t *U = peer_get (id);
  assert (U);
  if (U->user.phone) {
    tfree_str (U->user.phone);
  }
  U->user.phone = fetch_str_dup ();
  
  #ifdef USE_LUA
    lua_user_update (&U->user);
  #endif
  return 0;
}

int fetch_comb_binlog_user_set_friend (void *extra) {
  peer_id_t id = MK_USER (fetch_int ());
  peer_t *U = peer_get (id);
  assert (U);
  if (U->user.phone) {
    tfree_str (U->user.phone);
  }
  int friend = fetch_int ();
  if (friend) { U->flags |= FLAG_USER_CONTACT; }
  else { U->flags &= ~FLAG_USER_CONTACT; }
  
  #ifdef USE_LUA
    lua_user_update (&U->user);
  #endif
  return 0;
}

int fetch_comb_binlog_user_set_full_photo (void *extra) {
  peer_id_t id = MK_USER (fetch_int ());
  peer_t *U = peer_get (id);
  assert (U);
  if (U->flags & FLAG_HAS_PHOTO) {
    free_photo (&U->user.photo);
  }
  tglf_fetch_photo (&U->user.photo);
  
  #ifdef USE_LUA
    lua_user_update (&U->user);
  #endif
  return 0;
}

int fetch_comb_binlog_user_set_blocked (void *extra) {
  peer_id_t id = MK_USER (fetch_int ());
  peer_t *U = peer_get (id);
  assert (U);

  U->user.blocked = fetch_int ();
  
  #ifdef USE_LUA
    lua_user_update (&U->user);
  #endif
  return 0;
}

int fetch_comb_binlog_user_set_real_name (void *extra) {
  peer_id_t id = MK_USER (fetch_int ());
  peer_t *U = peer_get (id);
  assert (U);

  if (U->user.real_first_name) { tfree_str (U->user.real_first_name); }
  if (U->user.real_last_name) { tfree_str (U->user.real_last_name); }
  U->user.real_first_name = fetch_str_dup ();
  U->user.real_last_name = fetch_str_dup ();
  
  #ifdef USE_LUA
    lua_user_update (&U->user);
  #endif
  return 0;
}

int fetch_comb_binlog_user_set_name (void *extra) {
  peer_id_t id = MK_USER (fetch_int ());
  peer_t *U = peer_get (id);
  assert (U);

  if (U->user.first_name) { tfree_str (U->user.first_name); }
  if (U->user.last_name) { tfree_str (U->user.last_name); }
  U->user.first_name = fetch_str_dup ();
  U->user.last_name = fetch_str_dup ();
  if (U->print_name) { 
    peer_delete_name (U);
    tfree_str (U->print_name); 
  }
  U->print_name = create_print_name (U->id, U->user.first_name, U->user.last_name, 0, 0);
  peer_insert_name ((void *)U);
  
  #ifdef USE_LUA
    lua_user_update (&U->user);
  #endif
  return 0;
}

int fetch_comb_binlog_user_set_photo (void *extra) {
  peer_id_t id = MK_USER (fetch_int ());
  peer_t *U = peer_get (id);
  assert (U);
        
        
  unsigned y = fetch_int ();
  if (y == CODE_user_profile_photo_empty) {
    U->user.photo_id = 0;
    U->user.photo_big.dc = -2;
    U->user.photo_small.dc = -2;
  } else {
    assert (y == CODE_user_profile_photo);
    U->user.photo_id = fetch_long ();
    tglf_fetch_file_location (&U->user.photo_small);
    tglf_fetch_file_location (&U->user.photo_big);
  }
  
  #ifdef USE_LUA
    lua_user_update (&U->user);
  #endif
  return 0;
}

int fetch_comb_binlog_encr_chat_delete (void *extra) {
  peer_id_t id = MK_ENCR_CHAT (fetch_int ());
  peer_t *_U = peer_get (id);
  assert (_U);
  struct secret_chat *U = &_U->encr_chat;
  memset (U->key, 0, sizeof (U->key));
  U->flags |= FLAG_DELETED;
  U->state = sc_deleted;
  if (U->nonce) {
    tfree_secure (U->nonce, 256);
    U->nonce = 0;
  }
  if (U->g_key) {
    tfree_secure (U->g_key, 256);
    U->g_key = 0;
  }
  
  #ifdef USE_LUA
    lua_secret_chat_update (U);
  #endif
  return 0;
}

int fetch_comb_binlog_encr_chat_requested (void *extra) {
  peer_id_t id = MK_ENCR_CHAT (fetch_int ());
  peer_t *_U = peer_get (id);
  if (!_U) {
    _U = talloc0 (sizeof (*_U));
    _U->id = id;
    insert_encrypted_chat (_U);
  } else {
    assert (!(_U->flags & FLAG_CREATED));
  }
  struct secret_chat *U = (void *)_U;
  U->access_hash = fetch_long ();
  U->date = fetch_int ();
  U->admin_id = fetch_int ();
  U->user_id = fetch_int ();

  peer_t *Us = peer_get (MK_USER (U->user_id));
  assert (!U->print_name);
  if (Us) {
    U->print_name = create_print_name (id, "!", Us->user.first_name, Us->user.last_name, 0);
  } else {
    static char buf[100];
    tsnprintf (buf, 99, "user#%d", U->user_id);
    U->print_name = create_print_name (id, "!", buf, 0, 0);
  }
  peer_insert_name ((void *)U);
  U->g_key = talloc (256);
  U->nonce = talloc (256);
  fetch_ints (U->g_key, 64);
  fetch_ints (U->nonce, 64);

  U->flags |= FLAG_CREATED;
  U->state = sc_request;
  
  #ifdef USE_LUA
    lua_secret_chat_update (U);
  #endif
  return 0;
}

int fetch_comb_binlog_encr_chat_set_access_hash (void *extra) {
  peer_id_t id = MK_ENCR_CHAT (fetch_int ());
  peer_t *U = peer_get (id);
  assert (U);
  U->encr_chat.access_hash = fetch_long ();
  
  #ifdef USE_LUA
    lua_secret_chat_update (&U->encr_chat);
  #endif
  return 0;
}

int fetch_comb_binlog_encr_chat_set_date (void *extra) {
  peer_id_t id = MK_ENCR_CHAT (fetch_int ());
  peer_t *U = peer_get (id);
  assert (U);
  U->encr_chat.date = fetch_int ();
  
  #ifdef USE_LUA
    lua_secret_chat_update (&U->encr_chat);
  #endif
  return 0;
}

int fetch_comb_binlog_encr_chat_set_state (void *extra) {
  peer_id_t id = MK_ENCR_CHAT (fetch_int ());
  peer_t *U = peer_get (id);
  assert (U);
  U->encr_chat.state = fetch_int ();
  
  #ifdef USE_LUA
    lua_secret_chat_update (&U->encr_chat);
  #endif
  return 0;
}

int fetch_comb_binlog_encr_chat_accepted (void *extra) {
  peer_id_t id = MK_ENCR_CHAT (fetch_int ());
  peer_t *_U = peer_get (id);
  assert (_U);
  struct secret_chat *U = &_U->encr_chat;
  if (!U->g_key) {
    U->g_key = talloc (256);
  }
  if (!U->nonce) {
    U->nonce = talloc (256);
  }

  fetch_ints (U->g_key, 64);
  fetch_ints (U->nonce, 64);
  U->key_fingerprint = fetch_long ();
  
  if (U->state == sc_waiting) {
    do_create_keys_end (U);
  }
  U->state = sc_ok;
  
  #ifdef USE_LUA
    lua_secret_chat_update (U);
  #endif
  return 0;
}

int fetch_comb_binlog_encr_chat_set_key (void *extra) {
  peer_id_t id = MK_ENCR_CHAT (fetch_int ());
  peer_t *_U = peer_get (id);
  assert (_U);
  struct secret_chat *U = &_U->encr_chat;
  fetch_ints (U->key, 64);
  U->key_fingerprint = fetch_long ();
  
  #ifdef USE_LUA
    lua_secret_chat_update (U);
  #endif
  return 0;
}

int fetch_comb_binlog_encr_chat_init (void *extra) {
  peer_t *P = talloc0 (sizeof (*P));
  P->id = MK_ENCR_CHAT (fetch_int ());
  assert (!peer_get (P->id));
  P->encr_chat.user_id = fetch_int ();
  P->encr_chat.admin_id = tgl_state.our_id;
  insert_encrypted_chat (P);
  peer_t *Us = peer_get (MK_USER (P->encr_chat.user_id));
  assert (Us);
  P->print_name = create_print_name (P->id, "!", Us->user.first_name, Us->user.last_name, 0);
  peer_insert_name (P);

  P->encr_chat.g_key = talloc (256);
  fetch_ints (P->encr_chat.key, 64);
  fetch_ints (P->encr_chat.g_key, 64);
  P->flags |= FLAG_CREATED;
  
  #ifdef USE_LUA
    lua_secret_chat_update (U);
  #endif
  return 0;
}

int fetch_comb_binlog_chat_create (void *extra) {
  peer_id_t id = MK_CHAT (fetch_int ());
  peer_t *_C = peer_get (id);
  if (!_C) {
    _C = talloc0 (sizeof (*_C));
    _C->id = id;
    insert_chat (_C);
  } else {
    assert (!(_C->flags & FLAG_CREATED));
  }
  struct chat *C = &_C->chat;
  C->flags = FLAG_CREATED | fetch_int ();
  C->title = fetch_str_dup ();
  assert (!C->print_title);
  C->print_title = create_print_name (id, C->title, 0, 0, 0);
  peer_insert_name ((void *)C);
  C->users_num = fetch_int ();
  C->date = fetch_int ();
  C->version = fetch_int ();

  fetch_data (&C->photo_big, sizeof (struct file_location));
  fetch_data (&C->photo_small, sizeof (struct file_location));
      
  #ifdef USE_LUA
    lua_chat_update (C);
  #endif
  return 0;
}

int fetch_comb_binlog_chat_change_flags (void *extra) {
  peer_t *C = peer_get (MK_CHAT (fetch_int ()));
  assert (C && (C->flags & FLAG_CREATED));
  C->chat.flags |= fetch_int ();
  C->chat.flags &= ~fetch_int ();
  
  #ifdef USE_LUA
    lua_chat_update (&C->chat);
  #endif
  return 0;
}

int fetch_comb_binlog_chat_set_title (void *extra) {
  peer_t *C = peer_get (MK_CHAT (fetch_int ()));
  assert (C && (C->flags & FLAG_CREATED));
      
  if (C->chat.title) { tfree_str (C->chat.title); }
  C->chat.title = fetch_str_dup ();
  if (C->print_name) { 
    peer_delete_name ((void *)C);
    tfree_str (C->print_name); 
  }
  C->print_name = create_print_name (C->id, C->chat.title, 0, 0, 0);
  peer_insert_name ((void *)C);
  
  #ifdef USE_LUA
    lua_chat_update (&C->chat);
  #endif
  return 0;
}

int fetch_comb_binlog_chat_set_photo (void *extra) {
  peer_t *C = peer_get (MK_CHAT (fetch_int ()));
  assert (C && (C->flags & FLAG_CREATED));
  fetch_data (&C->photo_big, sizeof (struct file_location));
  fetch_data (&C->photo_small, sizeof (struct file_location));
  
  #ifdef USE_LUA
    lua_chat_update (&C->chat);
  #endif
  return 0;
}

int fetch_comb_binlog_chat_set_date (void *extra) {
  peer_t *C = peer_get (MK_CHAT (fetch_int ()));
  assert (C && (C->flags & FLAG_CREATED));
  C->chat.date = fetch_int ();
  
  #ifdef USE_LUA
    lua_chat_update (&C->chat);
  #endif
  return 0;
}

int fetch_comb_binlog_chat_set_version (void *extra) {
  peer_t *C = peer_get (MK_CHAT (fetch_int ()));
  assert (C && (C->flags & FLAG_CREATED));
  C->chat.version = fetch_int ();
  C->chat.users_num = fetch_int ();
  
  #ifdef USE_LUA
    lua_chat_update (&C->chat);
  #endif
  return 0;
}

int fetch_comb_binlog_chat_set_admin (void *extra) {
  peer_t *C = peer_get (MK_CHAT (fetch_int ()));
  assert (C && (C->flags & FLAG_CREATED));
  C->chat.admin_id = fetch_int ();
  
  #ifdef USE_LUA
    lua_chat_update (&C->chat);
  #endif
  return 0;
}

int fetch_comb_binlog_chat_set_participants (void *extra) {
  peer_t *C = peer_get (MK_CHAT (fetch_int ()));
  assert (C && (C->flags & FLAG_CREATED));
  C->chat.user_list_version = fetch_int ();
  if (C->chat.user_list) { tfree (C->chat.user_list, 12 * C->chat.user_list_size); }
  C->chat.user_list_size = fetch_int ();
  C->chat.user_list = talloc (12 * C->chat.user_list_size);
  fetch_ints (C->chat.user_list, 3 * C->chat.user_list_size);
  
  #ifdef USE_LUA
    lua_chat_update (&C->chat);
  #endif
  return 0;
}

int fetch_comb_binlog_chat_set_full_photo (void *extra) {
  peer_t *C = peer_get (MK_CHAT (fetch_int ()));
  assert (C && (C->flags & FLAG_CREATED));
      
  assert (C && (C->flags & FLAG_CREATED));
  if (C->flags & FLAG_HAS_PHOTO) {
    free_photo (&C->chat.photo);
  }
  tglf_fetch_photo (&C->chat.photo);
  
  #ifdef USE_LUA
    lua_chat_update (&C->chat);
  #endif
  return 0;
}

int fetch_comb_binlog_chat_add_participant (void *extra) {
  peer_id_t id = MK_CHAT (fetch_int ());
  peer_t *_C = peer_get (id);
  assert (_C && (_C->flags & FLAG_CREATED));
  struct chat *C = &_C->chat;

  int version = fetch_int ();
  int user = fetch_int ();
  int inviter = fetch_int ();
  int date = fetch_int ();
  assert (C->user_list_version < version);

  int i;
  for (i = 0; i < C->user_list_size; i++) {
    assert (C->user_list[i].user_id != user);
  }

  C->user_list_size ++;
  C->user_list = trealloc (C->user_list, 12 * C->user_list_size - 12, 12 * C->user_list_size);
  C->user_list[C->user_list_size - 1].user_id = user;
  C->user_list[C->user_list_size - 1].inviter_id = inviter;
  C->user_list[C->user_list_size - 1].date = date;
  C->user_list_version = version;
  
  #ifdef USE_LUA
    lua_chat_update (C);
  #endif
  return 0;
}

int fetch_comb_binlog_chat_del_participant (void *extra) {
  peer_id_t id = MK_CHAT (fetch_int ());
  peer_t *_C = peer_get (id);
  assert (_C && (_C->flags & FLAG_CREATED));
  struct chat *C = &_C->chat;
  
  int version = fetch_int ();
  int user = fetch_int ();
  assert (C->user_list_version < version);
      
  int i;
  for (i = 0; i < C->user_list_size; i++) {
    if (C->user_list[i].user_id == user) {
      struct chat_user t;
      t = C->user_list[i];
      C->user_list[i] = C->user_list[C->user_list_size - 1];
      C->user_list[C->user_list_size - 1] = t;
    }
  }
  assert (C->user_list[C->user_list_size - 1].user_id == user);
  C->user_list_size --;
  C->user_list = trealloc (C->user_list, 12 * C->user_list_size + 12, 12 * C->user_list_size);
  C->user_list_version = version;
  
  #ifdef USE_LUA
    lua_chat_update (C);
  #endif
  return 0;
}

int fetch_comb_binlog_create_message_text (void *extra) {
  long long id = fetch_int ();
  
  struct message *M = message_get (id);
  if (!M) {
    M = message_alloc (id);
  } else {
    assert (!(M->flags & FLAG_CREATED));
  }
  
  M->flags |= FLAG_CREATED;
  M->from_id = MK_USER (fetch_int ());
  int t = fetch_int ();
  if (t == PEER_ENCR_CHAT) {
    M->flags |= FLAG_ENCRYPTED;
  }

  M->to_id = set_peer_id (t, fetch_int ());
  M->date = fetch_int ();
      
  int l = prefetch_strlen ();
  M->message = talloc (l + 1);
  memcpy (M->message, fetch_str (l), l);
  M->message[l] = 0;
  M->message_len = l;

  if (t == PEER_ENCR_CHAT) {
    M->media.type = CODE_decrypted_message_media_empty;
  } else {
    M->media.type = CODE_message_media_empty;
  }
  
  M->unread = 1;
  M->out = get_peer_id (M->from_id) == tgl_state.our_id;

  message_insert (M);
      
  #ifdef USE_LUA
    lua_new_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_send_message_text (void *extra) {
  long long id = fetch_long ();
  
  struct message *M = message_get (id);
  if (!M) {
    M = message_alloc (id);
  } else {
    assert (!(M->flags & FLAG_CREATED));
  }
  
  M->flags |= FLAG_CREATED;
  M->from_id = MK_USER (fetch_int ());
  int t = fetch_int ();
  if (t == PEER_ENCR_CHAT) {
    M->flags |= FLAG_ENCRYPTED;
  }

  M->to_id = set_peer_id (t, fetch_int ());
  M->date = fetch_int ();
      
  int l = prefetch_strlen ();
  M->message = talloc (l + 1);
  memcpy (M->message, fetch_str (l), l);
  M->message[l] = 0;
  M->message_len = l;

  if (t == PEER_ENCR_CHAT) {
    M->media.type = CODE_decrypted_message_media_empty;
  } else {
    M->media.type = CODE_message_media_empty;
  }
  
  M->unread = 1;
  M->out = get_peer_id (M->from_id) == tgl_state.our_id;

  message_insert (M);
  message_insert_unsent (M);
  M->flags |= FLAG_PENDING;
      
  #ifdef USE_LUA
    lua_new_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_send_message_action_encr (void *extra) {
  long long id = fetch_long ();
  
  struct message *M = message_get (id);
  if (!M) {
    M = message_alloc (id);
  } else {
    assert (!(M->flags & FLAG_CREATED));
  }
  
  M->flags |= FLAG_CREATED | FLAG_ENCRYPTED;
  M->from_id = MK_USER (fetch_int ());
  
  int t = fetch_int ();
  M->to_id = set_peer_id (t, fetch_int ());
  M->date = fetch_int ();
      
  M->media.type = CODE_decrypted_message_media_empty;
  tglf_fetch_message_action_encrypted (&M->action);
  
  M->unread = 1;
  M->out = get_peer_id (M->from_id) == tgl_state.our_id;
  M->service = 1;

  message_insert (M);
  message_insert_unsent (M);
  M->flags |= FLAG_PENDING;
      
  #ifdef USE_LUA
    lua_new_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_create_message_text_fwd (void *extra) {
  long long id = fetch_int ();
  
  struct message *M = message_get (id);
  if (!M) {
    M = message_alloc (id);
  } else {
    assert (!(M->flags & FLAG_CREATED));
  }
  
  M->flags |= FLAG_CREATED;
  M->from_id = MK_USER (fetch_int ());
  int t = fetch_int ();
  if (t == PEER_ENCR_CHAT) {
    M->flags |= FLAG_ENCRYPTED;
  }

  M->to_id = set_peer_id (t, fetch_int ());
  M->date = fetch_int ();
  
  M->fwd_from_id = MK_USER (fetch_int ());
  M->fwd_date = fetch_int ();
      
  int l = prefetch_strlen ();
  M->message = talloc (l + 1);
  memcpy (M->message, fetch_str (l), l);
  M->message[l] = 0;
  M->message_len = l;

  if (t == PEER_ENCR_CHAT) {
    M->media.type = CODE_decrypted_message_media_empty;
  } else {
    M->media.type = CODE_message_media_empty;
  }
  
  M->unread = 1;
  M->out = get_peer_id (M->from_id) == tgl_state.our_id;

  message_insert (M);
      
  #ifdef USE_LUA
    lua_new_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_create_message_media (void *extra) {
  int id = fetch_int ();
  struct message *M = message_get (id);
  if (!M) {
    M = message_alloc (id);
  } else {
    assert (!(M->flags & FLAG_CREATED));
  }
  M->flags |= FLAG_CREATED;
  M->from_id = MK_USER (fetch_int ());
  int t = fetch_int ();
  M->to_id = set_peer_id (t, fetch_int ());
  M->date = fetch_int ();
      
  int l = prefetch_strlen ();
  M->message = talloc (l + 1);
  memcpy (M->message, fetch_str (l), l);
  M->message[l] = 0;
  M->message_len = l;

  tglf_fetch_message_media (&M->media);
  M->unread = 1;
  M->out = get_peer_id (M->from_id) == tgl_state.our_id;

  message_insert (M);
  #ifdef USE_LUA
    lua_new_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_create_message_media_encr (void *extra) {
  long long id = fetch_long ();
  struct message *M = message_get (id);
  if (!M) {
    M = message_alloc (id);
  } else {
    assert (!(M->flags & FLAG_CREATED));
  }
  M->flags |= FLAG_CREATED | FLAG_ENCRYPTED;
  M->from_id = MK_USER (fetch_int ());
  int t = fetch_int ();
  M->to_id = set_peer_id (t, fetch_int ());
  M->date = fetch_int ();
      
  int l = prefetch_strlen ();
  M->message = talloc (l + 1);
  memcpy (M->message, fetch_str (l), l);
  M->message[l] = 0;
  M->message_len = l;

  tglf_fetch_message_media_encrypted (&M->media);
  tglf_fetch_encrypted_message_file (&M->media);
  M->unread = 1;
  M->out = get_peer_id (M->from_id) == tgl_state.our_id;

  message_insert (M);
  #ifdef USE_LUA
    lua_new_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_create_message_media_fwd (void *extra) {
  int id = fetch_int ();
  struct message *M = message_get (id);
  if (!M) {
    M = message_alloc (id);
  } else {
    assert (!(M->flags & FLAG_CREATED));
  }
  M->flags |= FLAG_CREATED;
  M->from_id = MK_USER (fetch_int ());
  int t = fetch_int ();
  M->to_id = set_peer_id (t, fetch_int ());
  M->date = fetch_int ();
  
  M->fwd_from_id = MK_USER (fetch_int ());
  M->fwd_date = fetch_int ();
      
  int l = prefetch_strlen ();
  M->message = talloc (l + 1);
  memcpy (M->message, fetch_str (l), l);
  M->message[l] = 0;
  M->message_len = l;

  tglf_fetch_message_media (&M->media);
  M->unread = 1;
  M->out = get_peer_id (M->from_id) == tgl_state.our_id;

  message_insert (M);
  #ifdef USE_LUA
    lua_new_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_create_message_service (void *extra) {
  int id = fetch_int ();
  struct message *M = message_get (id);
  if (!M) {
    M = message_alloc (id);
  } else {
    assert (!(M->flags & FLAG_CREATED));
  }
  M->flags |= FLAG_CREATED;
  M->from_id = MK_USER (fetch_int ());
  int t = fetch_int ();
  M->to_id = set_peer_id (t, fetch_int ());
  M->date = fetch_int ();
      
  tglf_fetch_message_action (&M->action);
  M->unread = 1;
  M->out = get_peer_id (M->from_id) == tgl_state.our_id;
  M->service = 1;

  message_insert (M);
  #ifdef USE_LUA
    lua_new_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_create_message_service_encr (void *extra) {
  long long id = fetch_long ();
  struct message *M = message_get (id);
  if (!M) {
    M = message_alloc (id);
  } else {
    assert (!(M->flags & FLAG_CREATED));
  }
  M->flags |= FLAG_CREATED | FLAG_ENCRYPTED;
  M->from_id = MK_USER (fetch_int ());
  int t = fetch_int ();
  assert (t == PEER_ENCR_CHAT);
  M->to_id = set_peer_id (t, fetch_int ());
  M->date = fetch_int ();

  struct secret_chat *E = (void *)peer_get (M->to_id);
  assert (E);
  
  tglf_fetch_message_action_encrypted (&M->action);
  M->unread = 1;
  M->out = get_peer_id (M->from_id) == tgl_state.our_id;
  M->service = 1;

  if (!M->out && M->action.type == CODE_decrypted_message_action_notify_layer) {
    E->layer = M->action.layer;
  }

  message_insert (M);
  #ifdef USE_LUA
    lua_new_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_create_message_service_fwd (void *extra) {
  int id = fetch_int ();
  struct message *M = message_get (id);
  if (!M) {
    M = message_alloc (id);
  } else {
    assert (!(M->flags & FLAG_CREATED));
  }
  M->flags |= FLAG_CREATED;
  M->from_id = MK_USER (fetch_int ());
  int t = fetch_int ();
  M->to_id = set_peer_id (t, fetch_int ());
  M->date = fetch_int ();
  
  M->fwd_from_id = MK_USER (fetch_int ());
  M->fwd_date = fetch_int ();
      
  tglf_fetch_message_action (&M->action);
  M->unread = 1;
  M->out = get_peer_id (M->from_id) == tgl_state.our_id;
  M->service = 1;

  message_insert (M);
  #ifdef USE_LUA
    lua_new_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_message_set_unread (void *extra) {
  struct message *M = message_get (fetch_int ());
  assert (M);
  M->unread = 0;
  #ifdef USE_LUA
    lua_update_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_set_message_sent (void *extra) {
  struct message *M = message_get (fetch_long ());
  assert (M);
  message_remove_unsent (M);
  M->flags &= ~FLAG_PENDING;
  #ifdef USE_LUA
    lua_update_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_set_msg_id (void *extra) {
  struct message *M = message_get (fetch_long ());
  assert (M);
  if (M->flags & FLAG_PENDING) {
    message_remove_unsent (M);
    M->flags &= ~FLAG_PENDING;
  }
  message_remove_tree (M);
  message_del_peer (M);
  M->id = fetch_int ();
  if (message_get (M->id)) {
    free_message (M);
    tfree (M, sizeof (*M));
  } else {
    message_insert_tree (M);
    message_add_peer (M);
  }
  #ifdef USE_LUA
    lua_update_msg (M);
  #endif
  return 0;
}

int fetch_comb_binlog_delete_msg (void *extra) {
  struct message *M = message_get (fetch_long ());
  assert (M);
  if (M->flags & FLAG_PENDING) {
    message_remove_unsent (M);
    M->flags &= ~FLAG_PENDING;
  }
  message_remove_tree (M);
  message_del_peer (M);
  message_del_use (M);
  free_message (M);
  tfree (M, sizeof (*M));
  return 0;
}

#define FETCH_COMBINATOR_FUNCTION(NAME) \
  case CODE_ ## NAME:\
    ok = fetch_comb_ ## NAME (0); \
    break; \
    

void replay_log_event (void) {
  in_replay_log = 1;
  assert (rptr < wptr);
  int op = *rptr;

  if (verbosity >= 2) {
    logprintf ("log_pos %lld, op 0x%08x\n", binlog_pos, op);
  }

  in_ptr = rptr;
  in_end = wptr;
  assert (skip_type_any (TYPE_TO_PARAM(binlog_update)) >= 0);
  in_end = in_ptr;
  in_ptr = rptr;

  int ok = -1;
  in_ptr ++;

  switch (op) {
  FETCH_COMBINATOR_FUNCTION (binlog_start)
  FETCH_COMBINATOR_FUNCTION (binlog_dc_option)
  FETCH_COMBINATOR_FUNCTION (binlog_auth_key)
  FETCH_COMBINATOR_FUNCTION (binlog_default_dc)
  FETCH_COMBINATOR_FUNCTION (binlog_our_id)
  FETCH_COMBINATOR_FUNCTION (binlog_dc_signed)
  FETCH_COMBINATOR_FUNCTION (binlog_dc_salt)

  FETCH_COMBINATOR_FUNCTION (binlog_set_dh_params)
  FETCH_COMBINATOR_FUNCTION (binlog_set_pts)
  FETCH_COMBINATOR_FUNCTION (binlog_set_qts)
  FETCH_COMBINATOR_FUNCTION (binlog_set_date)
  FETCH_COMBINATOR_FUNCTION (binlog_set_seq)

  FETCH_COMBINATOR_FUNCTION (binlog_user_add)
  FETCH_COMBINATOR_FUNCTION (binlog_user_delete)
  FETCH_COMBINATOR_FUNCTION (binlog_user_set_access_hash)
  FETCH_COMBINATOR_FUNCTION (binlog_user_set_phone)
  FETCH_COMBINATOR_FUNCTION (binlog_user_set_friend)
  FETCH_COMBINATOR_FUNCTION (binlog_user_set_full_photo)
  FETCH_COMBINATOR_FUNCTION (binlog_user_set_blocked)
  FETCH_COMBINATOR_FUNCTION (binlog_user_set_name)
  FETCH_COMBINATOR_FUNCTION (binlog_user_set_photo)

  FETCH_COMBINATOR_FUNCTION (binlog_user_set_real_name)
  FETCH_COMBINATOR_FUNCTION (binlog_encr_chat_delete)
  FETCH_COMBINATOR_FUNCTION (binlog_encr_chat_requested)
  FETCH_COMBINATOR_FUNCTION (binlog_encr_chat_set_access_hash)
  FETCH_COMBINATOR_FUNCTION (binlog_encr_chat_set_date)
  FETCH_COMBINATOR_FUNCTION (binlog_encr_chat_set_state)
  FETCH_COMBINATOR_FUNCTION (binlog_encr_chat_accepted)
  FETCH_COMBINATOR_FUNCTION (binlog_encr_chat_set_key)
  FETCH_COMBINATOR_FUNCTION (binlog_encr_chat_init)

  FETCH_COMBINATOR_FUNCTION (binlog_chat_create)
  FETCH_COMBINATOR_FUNCTION (binlog_chat_change_flags)
  FETCH_COMBINATOR_FUNCTION (binlog_chat_set_title)
  FETCH_COMBINATOR_FUNCTION (binlog_chat_set_photo)
  FETCH_COMBINATOR_FUNCTION (binlog_chat_set_date)
  FETCH_COMBINATOR_FUNCTION (binlog_chat_set_version)
  FETCH_COMBINATOR_FUNCTION (binlog_chat_set_admin)
  FETCH_COMBINATOR_FUNCTION (binlog_chat_set_participants)
  FETCH_COMBINATOR_FUNCTION (binlog_chat_set_full_photo)
  FETCH_COMBINATOR_FUNCTION (binlog_chat_add_participant)
  FETCH_COMBINATOR_FUNCTION (binlog_chat_del_participant)

  FETCH_COMBINATOR_FUNCTION (binlog_create_message_text)
  FETCH_COMBINATOR_FUNCTION (binlog_send_message_text)
  FETCH_COMBINATOR_FUNCTION (binlog_send_message_action_encr)
  FETCH_COMBINATOR_FUNCTION (binlog_create_message_text_fwd)
  FETCH_COMBINATOR_FUNCTION (binlog_create_message_media)
  FETCH_COMBINATOR_FUNCTION (binlog_create_message_media_encr)
  FETCH_COMBINATOR_FUNCTION (binlog_create_message_media_fwd)
  FETCH_COMBINATOR_FUNCTION (binlog_create_message_service)
  FETCH_COMBINATOR_FUNCTION (binlog_create_message_service_encr)
  FETCH_COMBINATOR_FUNCTION (binlog_create_message_service_fwd)
  FETCH_COMBINATOR_FUNCTION (binlog_message_set_unread)
  FETCH_COMBINATOR_FUNCTION (binlog_set_message_sent)
  FETCH_COMBINATOR_FUNCTION (binlog_set_msg_id)
  FETCH_COMBINATOR_FUNCTION (binlog_delete_msg)
  default:
    logprintf ("Unknown op 0x%08x\n", op);
    assert (0);
  }
  assert (ok >= 0);

  assert (in_ptr == in_end);
  in_replay_log = 0;
  binlog_pos += (in_ptr - rptr) * 4;
  rptr = in_ptr;
}

void create_new_binlog (void) {
  static int s[1000];
  packet_ptr = s;
  out_int (CODE_binlog_start);
  out_int (CODE_binlog_dc_option);
  out_int (1);
  out_string ("");
  out_string (tgl_params.test_mode ? TG_SERVER_TEST : TG_SERVER);
  out_int (443);
  out_int (CODE_binlog_default_dc);
  out_int (1);
  
  int fd = open (get_binlog_file_name (), O_WRONLY | O_EXCL | O_CREAT, 0600);
  if (fd < 0) {
    perror ("Write new binlog");
    exit (2);
  }
  assert (write (fd, s, (packet_ptr - s) * 4) == (packet_ptr - s) * 4);
  close (fd);
}


void replay_log (void) {
  if (access (get_binlog_file_name (), F_OK) < 0) {
    printf ("No binlog found. Creating new one\n");
    create_new_binlog ();
  }
  int fd = open (get_binlog_file_name (), O_RDONLY);
  if (fd < 0) {
    perror ("binlog open");
    exit (2);
  }
  int end = 0;
  while (1) {
    if (!end && wptr - rptr < MAX_LOG_EVENT_SIZE / 4) {
      if (wptr == rptr) {
        wptr = rptr = binlog_buffer;
      } else {
        int x = wptr - rptr;
        memcpy (binlog_buffer, rptr, 4 * x);
        wptr -= (rptr - binlog_buffer);
        rptr = binlog_buffer;
      }
      int l = (binlog_buffer + BINLOG_BUFFER_SIZE - wptr) * 4;
      int k = read (fd, wptr, l);
      if (k < 0) {
        perror ("read binlog");
        exit (2);
      }
      assert (!(k & 3));
      if (k < l) { 
        end = 1;
      }
      wptr += (k / 4);
    }
    if (wptr == rptr) { break; }
    replay_log_event ();
  }
  close (fd);
}

int binlog_fd;
void write_binlog (void) {
  binlog_fd = open (get_binlog_file_name (), O_WRONLY);
  if (binlog_fd < 0) {
    perror ("binlog open");
    exit (2);
  }
  
  assert (lseek (binlog_fd, binlog_pos, SEEK_SET) == binlog_pos);
  if (flock (binlog_fd, LOCK_EX | LOCK_NB) < 0) {
    perror ("get lock");
    exit (2);
  } 
}

void add_log_event (const int *data, int len) {
  if (verbosity) {
    logprintf ("Add log event: magic = 0x%08x, len = %d\n", data[0], len);
  }
  assert (!(len & 3));
  if (in_replay_log) { return; }
  rptr = (void *)data;
  wptr = rptr + (len / 4);
  int *in = in_ptr;
  int *end = in_end;
  replay_log_event ();
  if (rptr != wptr) {
    logprintf ("Unread %lld ints. Len = %d\n", (long long)(wptr - rptr), len);
    assert (rptr == wptr);
  }
  if (binlog_enabled) {
    assert (binlog_fd > 0);
    assert (write (binlog_fd, data, len) == len);
  }
  in_ptr = in;
  in_end = end;
}

void bl_do_set_auth_key_id (int num, unsigned char *buf) {
  static unsigned char sha1_buffer[20];
  SHA1 (buf, 256, sha1_buffer);
  long long fingerprint = *(long long *)(sha1_buffer + 12);
  int *ev = alloc_log_event (8 + 8 + 256);
  ev[0] = CODE_binlog_auth_key;
  ev[1] = num;
  *(long long *)(ev + 2) = fingerprint;
  memcpy (ev + 4, buf, 256);
  add_log_event (ev, 8 + 8 + 256);
}

void bl_do_set_our_id (int id) {
  if (tgl_state.our_id) {
    assert (tgl_state.our_id == id);
    return;
  }
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_our_id;
  ev[1] = id;
  add_log_event (ev, 8);
  write_auth_file ();
}

void bl_do_user_add (int id, const char *f, int fl, const char *l, int ll, long long access_token, const char *p, int pl, int contact) {
  clear_packet ();
  out_int (CODE_binlog_user_add);
  out_int (id);
  out_cstring (f ? f : "", fl);
  out_cstring (l ? l : "", ll);
  out_long (access_token);
  out_cstring (p ? p : "", pl);
  out_int (contact);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_user_delete (struct user *U) {
  if (U->flags & FLAG_DELETED) { return; }
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_user_delete;
  ev[1] = get_peer_id (U->id);
  add_log_event (ev, 8);
}

void bl_do_set_user_profile_photo (struct user *U, long long photo_id, struct file_location *big, struct file_location *small) {
  if (photo_id == U->photo_id) { return; }
  if (!photo_id) {
    int *ev = alloc_log_event (12);
    ev[0] = CODE_binlog_user_set_photo;
    ev[1] = get_peer_id (U->id);
    ev[2] = CODE_user_profile_photo_empty;
    add_log_event (ev, 12);
  } else {
    clear_packet ();
    out_int (CODE_binlog_user_set_photo);
    out_int (get_peer_id (U->id));
    out_int (CODE_user_profile_photo);
    out_long (photo_id);
    if (small->dc >= 0) {
      out_int (CODE_file_location);
      out_int (small->dc);
      out_long (small->volume);
      out_int (small->local_id);
      out_long (small->secret);
    } else {
      out_int (CODE_file_location_unavailable);
      out_long (small->volume);
      out_int (small->local_id);
      out_long (small->secret);
    }
    if (big->dc >= 0) {
      out_int (CODE_file_location);
      out_int (big->dc);
      out_long (big->volume);
      out_int (big->local_id);
      out_long (big->secret);
    } else {
      out_int (CODE_file_location_unavailable);
      out_long (big->volume);
      out_int (big->local_id);
      out_long (big->secret);
    }
    add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
  }
}

void bl_do_user_set_name (struct user *U, const char *f, int fl, const char *l, int ll) {
  if ((U->first_name && (int)strlen (U->first_name) == fl && !strncmp (U->first_name, f, fl)) && 
      (U->last_name  && (int)strlen (U->last_name)  == ll && !strncmp (U->last_name,  l, ll))) {
    return;
  }
  clear_packet ();
  out_int (CODE_binlog_user_set_name);
  out_int (get_peer_id (U->id));
  out_cstring (f, fl);
  out_cstring (l, ll);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_user_set_access_hash (struct user *U, long long access_token) {
  if (U->access_hash == access_token) { return; }
  int *ev = alloc_log_event (16);
  ev[0] = CODE_binlog_user_set_access_hash;
  ev[1] = get_peer_id (U->id);
  *(long long *)(ev + 2) = access_token;
  add_log_event (ev, 16);
}

void bl_do_user_set_phone (struct user *U, const char *p, int pl) {
  if (U->phone && (int)strlen (U->phone) == pl && !strncmp (U->phone, p, pl)) {
    return;
  }
  clear_packet ();
  out_int (CODE_binlog_user_set_phone);
  out_int (get_peer_id (U->id));
  out_cstring (p, pl);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_user_set_friend (struct user *U, int friend) {
  if (friend == ((U->flags & FLAG_USER_CONTACT) != 0)) { return ; }
  int *ev = alloc_log_event (12);
  ev[0] = CODE_binlog_user_set_friend;
  ev[1] = get_peer_id (U->id);
  ev[2] = friend;
  add_log_event (ev, 12);
}

void bl_do_dc_option (int id, int l1, const char *name, int l2, const char *ip, int port) {
  struct dc *DC = DC_list[id];
  if (DC) { return; }
  
  clear_packet ();
  out_int (CODE_binlog_dc_option);
  out_int (id);
  out_cstring (name, l1);
  out_cstring (ip, l2);
  out_int (port);

  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_dc_signed (int id) {
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_dc_signed;
  ev[1] = id;
  add_log_event (ev, 8);
}

void bl_do_set_working_dc (int num) {
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_default_dc;
  ev[1] = num;
  add_log_event (ev, 8);
}

void bl_do_user_set_full_photo (struct user *U, const int *start, int len) {
  if (U->photo.id == *(long long *)(start + 1)) { return; }
  int *ev = alloc_log_event (len + 8);
  ev[0] = CODE_binlog_user_set_full_photo;
  ev[1] = get_peer_id (U->id);
  memcpy (ev + 2, start, len);
  add_log_event (ev, len + 8);
}

void bl_do_user_set_blocked (struct user *U, int blocked) {
  if (U->blocked == blocked) { return; }
  int *ev = alloc_log_event (12);
  ev[0] = CODE_binlog_user_set_blocked;
  ev[1] = get_peer_id (U->id);
  ev[2] = blocked;
  add_log_event (ev, 12);
}

void bl_do_user_set_real_name (struct user *U, const char *f, int fl, const char *l, int ll) {
  if ((U->real_first_name && (int)strlen (U->real_first_name) == fl && !strncmp (U->real_first_name, f, fl)) && 
      (U->real_last_name  && (int)strlen (U->real_last_name)  == ll && !strncmp (U->real_last_name,  l, ll))) {
    return;
  }
  clear_packet ();
  out_int (CODE_binlog_user_set_real_name);
  out_int (get_peer_id (U->id));
  out_cstring (f, fl);
  out_cstring (l, ll);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_encr_chat_delete (struct secret_chat *U) {
  if (!(U->flags & FLAG_CREATED) || U->state == sc_deleted || U->state == sc_none) { return; }
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_encr_chat_delete;
  ev[1] = get_peer_id (U->id);
  add_log_event (ev, 8);
}

void bl_do_encr_chat_requested (struct secret_chat *U, long long access_hash, int date, int admin_id, int user_id, unsigned char g_key[], unsigned char nonce[]) {
  if (U->state != sc_none) { return; }
  int *ev = alloc_log_event (540);
  ev[0] = CODE_binlog_encr_chat_requested;
  ev[1] = get_peer_id (U->id);
  *(long long *)(ev + 2) = access_hash;
  ev[4] = date;
  ev[5] = admin_id;
  ev[6] = user_id;
  memcpy (ev + 7, g_key, 256);
  memcpy (ev + 7 + 64, nonce, 256);
  add_log_event (ev, 540);
}

void bl_do_encr_chat_set_access_hash (struct secret_chat *U, long long access_hash) {
  if (U->access_hash == access_hash) { return; }
  int *ev = alloc_log_event (16);
  ev[0] = CODE_binlog_encr_chat_set_access_hash;
  ev[1] = get_peer_id (U->id);
  *(long long *)(ev + 2) = access_hash;
  add_log_event (ev, 16);
}

void bl_do_encr_chat_set_date (struct secret_chat *U, int date) {
  if (U->date == date) { return; }
  int *ev = alloc_log_event (12);
  ev[0] = CODE_binlog_encr_chat_set_date;
  ev[1] = get_peer_id (U->id);
  ev[2] = date;
  add_log_event (ev, 12);
}

void bl_do_encr_chat_set_state (struct secret_chat *U, enum secret_chat_state state) {
  if (U->state == state) { return; }
  int *ev = alloc_log_event (12);
  ev[0] = CODE_binlog_encr_chat_set_state;
  ev[1] = get_peer_id (U->id);
  ev[2] = state;
  add_log_event (ev, 12);
}

void bl_do_encr_chat_accepted (struct secret_chat *U, const unsigned char g_key[], const unsigned char nonce[], long long key_fingerprint) {
  if (U->state != sc_waiting && U->state != sc_request) { return; }
  int *ev = alloc_log_event (528);
  ev[0] = CODE_binlog_encr_chat_accepted;
  ev[1] = get_peer_id (U->id);
  memcpy (ev + 2, g_key, 256);
  memcpy (ev + 66, nonce, 256);
  *(long long *)(ev + 130) = key_fingerprint;
  add_log_event (ev, 528);
}

void bl_do_encr_chat_set_key (struct secret_chat *E, unsigned char key[], long long key_fingerprint) {
  int *ev = alloc_log_event (272);
  ev[0] = CODE_binlog_encr_chat_set_key;
  ev[1] = get_peer_id (E->id);
  memcpy (ev + 2, key, 256);
  *(long long *)(ev + 66) = key_fingerprint;
  add_log_event (ev, 272);
}

void bl_do_set_dh_params (int root, unsigned char prime[], int version) {
  int *ev = alloc_log_event (268);
  ev[0] = CODE_binlog_set_dh_params;
  ev[1] = root;
  memcpy (ev + 2, prime, 256);
  ev[66] = version;
  add_log_event (ev, 268);
}

void bl_do_encr_chat_init (int id, int user_id, unsigned char random[], unsigned char g_a[]) {
  int *ev = alloc_log_event (524);
  ev[0] = CODE_binlog_encr_chat_init;
  ev[1] = id;
  ev[2] = user_id;
  memcpy (ev + 3, random, 256);
  memcpy (ev + 67, g_a, 256);
  add_log_event (ev, 524);
}

void bl_do_set_pts (int pts) {
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_set_pts;
  ev[1] = pts;
  add_log_event (ev, 8);
}

void bl_do_set_qts (int qts) {
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_set_qts;
  ev[1] = qts;
  add_log_event (ev, 8);
}

void bl_do_set_date (int date) {
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_set_date;
  ev[1] = date;
  add_log_event (ev, 8);
}

void bl_do_set_seq (int seq) {
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_set_seq;
  ev[1] = seq;
  add_log_event (ev, 8);
}

void bl_do_create_chat (struct chat *C, int y, const char *s, int l, int users_num, int date, int version, struct file_location *big, struct file_location *small) {
  clear_packet ();
  out_int (CODE_binlog_chat_create);
  out_int (get_peer_id (C->id));
  out_int (y);
  out_cstring (s, l);
  out_int (users_num);
  out_int (date);
  out_int (version);
  out_data (big, sizeof (struct file_location));
  out_data (small, sizeof (struct file_location));
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_chat_forbid (struct chat *C, int on) {
  if (on) {
    if (C->flags & FLAG_FORBIDDEN) { return; }
    int *ev = alloc_log_event (16);
    ev[0] = CODE_binlog_chat_change_flags;
    ev[1] = get_peer_id (C->id);
    ev[2] = FLAG_FORBIDDEN;
    ev[3] = 0;
    add_log_event (ev, 16);
  } else {
    if (!(C->flags & FLAG_FORBIDDEN)) { return; }
    int *ev = alloc_log_event (16);
    ev[0] = CODE_binlog_chat_change_flags;
    ev[1] = get_peer_id (C->id);
    ev[2] = 0;
    ev[3] = FLAG_FORBIDDEN;
    add_log_event (ev, 16);
  }
}

void bl_do_chat_set_title (struct chat *C, const char *s, int l) {
  if (C->title && (int)strlen (C->title) == l && !strncmp (C->title, s, l)) { return; }
  clear_packet ();
  out_int (CODE_binlog_chat_set_title);
  out_int (get_peer_id (C->id));
  out_cstring (s, l);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_chat_set_photo (struct chat *C, struct file_location *big, struct file_location *small) {
  if (!memcmp (&C->photo_small, small, sizeof (struct file_location)) &&
      !memcmp (&C->photo_big, big, sizeof (struct file_location))) { return; }
  clear_packet ();
  out_int (CODE_binlog_chat_set_photo);
  out_int (get_peer_id (C->id));
  out_data (big, sizeof (struct file_location));
  out_data (small, sizeof (struct file_location));
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_chat_set_date (struct chat *C, int date) {
  if (C->date == date) { return; }
  int *ev = alloc_log_event (12);
  ev[0] = CODE_binlog_chat_set_date;
  ev[1] = get_peer_id (C->id);
  ev[2] = date;
  add_log_event (ev, 12);
}

void bl_do_chat_set_set_in_chat (struct chat *C, int on) {
  if (on) {
    if (C->flags & FLAG_CHAT_IN_CHAT) { return; }
    int *ev = alloc_log_event (16);
    ev[0] = CODE_binlog_chat_change_flags;
    ev[1] = get_peer_id (C->id);
    ev[2] = FLAG_CHAT_IN_CHAT;
    ev[3] = 0;
    add_log_event (ev, 16);
  } else {
    if (!(C->flags & FLAG_CHAT_IN_CHAT)) { return; }
    int *ev = alloc_log_event (16);
    ev[0] = CODE_binlog_chat_change_flags;
    ev[1] = get_peer_id (C->id);
    ev[2] = 0;
    ev[3] = FLAG_CHAT_IN_CHAT;
    add_log_event (ev, 16);
  }
}

void bl_do_chat_set_version (struct chat *C, int version, int user_num) {
  if (C->version >= version) { return; }
  int *ev = alloc_log_event (16);
  ev[0] = CODE_binlog_chat_set_version;
  ev[1] = get_peer_id (C->id);
  ev[2] = version;
  ev[3] = user_num;
  add_log_event (ev, 16);
}

void bl_do_chat_set_admin (struct chat *C, int admin) {
  if (C->admin_id == admin) { return; }
  int *ev = alloc_log_event (12);
  ev[0] = CODE_binlog_chat_set_admin;
  ev[1] = get_peer_id (C->id);
  ev[2] = admin;
  add_log_event (ev, 12);
}

void bl_do_chat_set_participants (struct chat *C, int version, int user_num, struct chat_user *users) {
  if (C->user_list_version >= version) { return; }
  int *ev = alloc_log_event (12 * user_num + 16);
  ev[0] = CODE_binlog_chat_set_participants;
  ev[1] = get_peer_id (C->id);
  ev[2] = version;
  ev[3] = user_num;
  memcpy (ev + 4, users, 12 * user_num);
  add_log_event (ev, 12 * user_num + 16);
}

void bl_do_chat_set_full_photo (struct chat *U, const int *start, int len) {
  if (U->photo.id == *(long long *)(start + 1)) { return; }
  int *ev = alloc_log_event (len + 8);
  ev[0] = CODE_binlog_chat_set_full_photo;
  ev[1] = get_peer_id (U->id);
  memcpy (ev + 2, start, len);
  add_log_event (ev, len + 8);
}

void bl_do_chat_add_user (struct chat *C, int version, int user, int inviter, int date) {
  if (C->user_list_version >= version || !C->user_list_version) { return; }
  int *ev = alloc_log_event (24);
  ev[0] = CODE_binlog_chat_add_participant;
  ev[1] = get_peer_id (C->id);
  ev[2] = version;
  ev[3] = user;
  ev[4] = inviter;
  ev[5] = date;
  add_log_event (ev, 24);
}

void bl_do_chat_del_user (struct chat *C, int version, int user) {
  if (C->user_list_version >= version || !C->user_list_version) { return; }
  int *ev = alloc_log_event (16);
  ev[0] = CODE_binlog_chat_del_participant;
  ev[1] = get_peer_id (C->id);
  ev[2] = version;
  ev[3] = user;
  add_log_event (ev, 16);
}

void bl_do_create_message_text (int msg_id, int from_id, int to_type, int to_id, int date, int l, const char *s) {
  clear_packet ();
  out_int (CODE_binlog_create_message_text);
  out_int (msg_id);
  out_int (from_id);
  out_int (to_type);
  out_int (to_id);
  out_int (date);
  out_cstring (s, l);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_send_message_text (long long msg_id, int from_id, int to_type, int to_id, int date, int l, const char *s) {
  clear_packet ();
  out_int (CODE_binlog_send_message_text);
  out_long (msg_id);
  out_int (from_id);
  out_int (to_type);
  out_int (to_id);
  out_int (date);
  out_cstring (s, l);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_send_message_action_encr (long long msg_id, int from_id, int to_type, int to_id, int date, int l, const int *action) {
  clear_packet ();
  out_int (CODE_binlog_send_message_action_encr);
  out_long (msg_id);
  out_int (from_id);
  out_int (to_type);
  out_int (to_id);
  out_int (date);
  out_ints (action, l);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_create_message_text_fwd (int msg_id, int from_id, int to_type, int to_id, int date, int fwd, int fwd_date, int l, const char *s) {
  clear_packet ();
  out_int (CODE_binlog_create_message_text_fwd);
  out_int (msg_id);
  out_int (from_id);
  out_int (to_type);
  out_int (to_id);
  out_int (date);
  out_int (fwd);
  out_int (fwd_date);
  out_cstring (s, l);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_create_message_media (int msg_id, int from_id, int to_type, int to_id, int date, int l, const char *s, const int *data, int len) {
  clear_packet ();
  out_int (CODE_binlog_create_message_media);
  out_int (msg_id);
  out_int (from_id);
  out_int (to_type);
  out_int (to_id);
  out_int (date);
  out_cstring (s, l);
  out_ints (data, len);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_create_message_media_encr (long long msg_id, int from_id, int to_type, int to_id, int date, int l, const char *s, const int *data, int len, const int *data2, int len2) {
  clear_packet ();
  out_int (CODE_binlog_create_message_media_encr);
  out_long (msg_id);
  out_int (from_id);
  out_int (to_type);
  out_int (to_id);
  out_int (date);
  out_cstring (s, l);
  out_ints (data, len);
  out_ints (data2, len2);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_create_message_media_fwd (int msg_id, int from_id, int to_type, int to_id, int date, int fwd, int fwd_date, int l, const char *s, const int *data, int len) {
  clear_packet ();
  out_int (CODE_binlog_create_message_media_fwd);
  out_int (msg_id);
  out_int (from_id);
  out_int (to_type);
  out_int (to_id);
  out_int (date);
  out_int (fwd);
  out_int (fwd_date);
  out_cstring (s, l);
  out_ints (data, len);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_create_message_service (int msg_id, int from_id, int to_type, int to_id, int date, const int *data, int len) {
  clear_packet ();
  out_int (CODE_binlog_create_message_service);
  out_int (msg_id);
  out_int (from_id);
  out_int (to_type);
  out_int (to_id);
  out_int (date);
  out_ints (data, len);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_create_message_service_encr (long long msg_id, int from_id, int to_type, int to_id, int date, const int *data, int len) {
  clear_packet ();
  out_int (CODE_binlog_create_message_service_encr);
  out_long (msg_id);
  out_int (from_id);
  out_int (to_type);
  out_int (to_id);
  out_int (date);
  out_ints (data, len);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_create_message_service_fwd (int msg_id, int from_id, int to_type, int to_id, int date, int fwd, int fwd_date, const int *data, int len) {
  clear_packet ();
  out_int (CODE_binlog_create_message_service_fwd);
  out_int (msg_id);
  out_int (from_id);
  out_int (to_type);
  out_int (to_id);
  out_int (date);
  out_int (fwd);
  out_int (fwd_date);
  out_ints (data, len);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_set_unread (struct message *M, int unread) {
  if (unread || !M->unread) { return; }
  clear_packet ();
  out_int (CODE_binlog_message_set_unread);
  out_int (M->id);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_set_message_sent (struct message *M) {
  if (!(M->flags & FLAG_PENDING)) { return; }
  clear_packet ();
  out_int (CODE_binlog_set_message_sent);
  out_long (M->id);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_set_msg_id (struct message *M, int id) {
  if (M->id == id) { return; }
  clear_packet ();
  out_int (CODE_binlog_set_msg_id);
  out_long (M->id);
  out_int (id);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_delete_msg (struct message *M) {
  clear_packet ();
  out_int (CODE_binlog_delete_msg);
  out_long (M->id);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}
