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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <string.h>
#include "structures.h"
#include "mtproto-common.h"
//#include "telegram.h"
#include "tree.h"
#include "loop.h"
#include <openssl/aes.h>
#include <openssl/sha.h>
#include "queries.h"
#include "binlog.h"
#include "updates.h"

#include "tgl.h"

#define sha1 SHA1

#ifndef TGL_MAX_PEER_NUM
#  define TGL_MAX_PEER_NUM 100000
#endif

static int id_cmp (struct tgl_message *M1, struct tgl_message *M2);
#define peer_cmp(a,b) (tgl_cmp_peer_id (a->id, b->id))
#define peer_cmp_name(a,b) (strcmp (a->print_name, b->print_name))
DEFINE_TREE(peer,tgl_peer_t *,peer_cmp,0)
DEFINE_TREE(peer_by_name,tgl_peer_t *,peer_cmp_name,0)
DEFINE_TREE(message,struct tgl_message *,id_cmp,0)


static struct tgl_message message_list = {
  .next_use = &message_list,
  .prev_use = &message_list
};

static struct tree_peer *tgl_peer_tree;
static struct tree_peer_by_name *peer_by_name_tree;
static struct tree_message *message_tree;
static struct tree_message *message_unsent_tree;

static int users_allocated;
static int chats_allocated;
static int messages_allocated;
static int peer_num;
static int encr_chats_allocated;
static int geo_chats_allocated;

static tgl_peer_t *Peers[TGL_MAX_PEER_NUM];



char *tgls_default_create_print_name (tgl_peer_id_t id, const char *a1, const char *a2, const char *a3, const char *a4) {
  const char *d[4];
  d[0] = a1; d[1] = a2; d[2] = a3; d[3] = a4;
  static char buf[10000];
  buf[0] = 0;
  int i;
  int p = 0;
  for (i = 0; i < 4; i++) {
    if (d[i] && strlen (d[i])) {
      p += tsnprintf (buf + p, 9999 - p, "%s%s", p ? "_" : "", d[i]);
      assert (p < 9990);
    }
  }
  char *s = buf;
  while (*s) {
    if (*s == ' ') { *s = '_'; }
    s++;
  }
  s = buf;
  int fl = strlen (s);
  int cc = 0;
  while (1) {
    tgl_peer_t *P = tgl_peer_get_by_name (s);
    if (!P || !tgl_cmp_peer_id (P->id, id)) {
      break;
    }
    cc ++;
    assert (cc <= 9999);
    tsnprintf (s + fl, 9999 - fl, "#%d", cc);
  }
  return tstrdup (s);
}

/* {{{ Fetch */

int tglf_fetch_file_location (struct tgl_file_location *loc) {
  int x = fetch_int ();
  assert (x == CODE_file_location_unavailable || x == CODE_file_location);

  if (x == CODE_file_location_unavailable) {
    loc->dc = -1;
    loc->volume = fetch_long ();
    loc->local_id = fetch_int ();
    loc->secret = fetch_long ();
  } else {
    loc->dc = fetch_int ();
    loc->volume = fetch_long ();
    loc->local_id = fetch_int ();
    loc->secret = fetch_long ();
  }
  return 0;
}

int tglf_fetch_user_status (struct tgl_user_status *S) {
  unsigned x = fetch_int ();
  assert (x == CODE_user_status_empty || x == CODE_user_status_online || x == CODE_user_status_offline);
  switch (x) {
  case CODE_user_status_empty:
    S->online = 0;
    S->when = 0;
    break;
  case CODE_user_status_online:
    S->online = 1;
    S->when = fetch_int ();
    break;
  case CODE_user_status_offline:
    S->online = -1;
    S->when = fetch_int ();
    break;
  default:
    assert (0);
  }
  return 0;
}

long long tglf_fetch_user_photo (struct tgl_user *U) {
  unsigned x = fetch_int ();
  assert (x == CODE_user_profile_photo || x == CODE_user_profile_photo_old || x == CODE_user_profile_photo_empty);
  if (x == CODE_user_profile_photo_empty) {
    bl_do_set_user_profile_photo (U, 0, 0, 0);
    return 0;
  }
  long long photo_id = 1;
  if (x == CODE_user_profile_photo) {
    photo_id = fetch_long ();
  }
  static struct tgl_file_location big;
  static struct tgl_file_location small;
  assert (tglf_fetch_file_location (&small) >= 0);
  assert (tglf_fetch_file_location (&big) >= 0);

  bl_do_set_user_profile_photo (U, photo_id, &big, &small);
  return 0;
}

int tglf_fetch_user (struct tgl_user *U) {
  unsigned x = fetch_int ();
  assert (x == CODE_user_empty || x == CODE_user_self || x == CODE_user_contact ||  x == CODE_user_request || x == CODE_user_foreign || x == CODE_user_deleted);
  U->id = TGL_MK_USER (fetch_int ());
  if (x == CODE_user_empty) {
    return 0;
  }
  
  if (x == CODE_user_self) {
    bl_do_set_our_id (tgl_get_peer_id (U->id));
  }
  
  int new = !(U->flags & FLAG_CREATED);
  if (new) {
    int l1 = prefetch_strlen ();
    assert (l1 >= 0);
    char *s1 = fetch_str (l1);
    int l2 = prefetch_strlen ();
    assert (l2 >= 0);
    char *s2 = fetch_str (l2);
    
    if (x == CODE_user_deleted && !(U->flags & FLAG_DELETED)) {
      bl_do_user_add (tgl_get_peer_id (U->id), s1, l1, s2, l2, 0, 0, 0, 0);
      bl_do_user_delete (U);
    }
    if (x != CODE_user_deleted) {
      long long access_hash = 0;
      if (x != CODE_user_self) {
        access_hash = fetch_long ();
      }
      int phone_len = 0;
      char *phone = 0;
      if (x != CODE_user_foreign) {
        phone_len = prefetch_strlen ();
        assert (phone_len >= 0);
        phone = fetch_str (phone_len);
      }
      bl_do_user_add (tgl_get_peer_id (U->id), s1, l1, s2, l2, access_hash, phone, phone_len, x == CODE_user_contact);
      assert (tglf_fetch_user_photo (U) >= 0);
      assert (tglf_fetch_user_status (&U->status) >= 0);

      if (x == CODE_user_self) {
        fetch_bool ();
      }
    }
  } else {
    int l1 = prefetch_strlen ();
    char *s1 = fetch_str (l1);
    int l2 = prefetch_strlen ();
    char *s2 = fetch_str (l2);
    
    bl_do_user_set_name (U, s1, l1, s2, l2);

    if (x == CODE_user_deleted && !(U->flags & FLAG_DELETED)) {
      bl_do_user_delete (U);
    }
    if (x != CODE_user_deleted) {
      if (x != CODE_user_self) {
        bl_do_user_set_access_hash (U, fetch_long ());
      }
      if (x != CODE_user_foreign) {
        int l = prefetch_strlen ();
        char *s = fetch_str (l);
        bl_do_user_set_phone (U, s, l);
      }
      assert (tglf_fetch_user_photo (U) >= 0);
    
      tglf_fetch_user_status (&U->status);
      if (x == CODE_user_self) {
        fetch_bool ();
      }

      if (x == CODE_user_contact) {
        bl_do_user_set_friend (U, 1);
      } else {
        bl_do_user_set_friend (U, 0);
      }
    }
  }
  return 0;
}

void tglf_fetch_user_full (struct tgl_user *U) {
  assert (fetch_int () == CODE_user_full);
  tglf_fetch_alloc_user ();
  assert (skip_type_any (TYPE_TO_PARAM (contacts_link)) >= 0);

  int *start = in_ptr;
  assert (skip_type_any (TYPE_TO_PARAM (photo)) >= 0);
  bl_do_user_set_full_photo (U, start, 4 * (in_ptr - start));

  assert (skip_type_any (TYPE_TO_PARAM (peer_notify_settings)) >= 0);
  
  bl_do_user_set_blocked (U, fetch_bool ());
  int l1 = prefetch_strlen ();
  char *s1 = fetch_str (l1);
  int l2 = prefetch_strlen ();
  char *s2 = fetch_str (l2);
  bl_do_user_set_real_name (U, s1, l1, s2, l2);
}

void tglf_fetch_encrypted_chat (struct tgl_secret_chat *U) {
  unsigned x = fetch_int ();
  assert (x == CODE_encrypted_chat_empty || x == CODE_encrypted_chat_waiting || x == CODE_encrypted_chat_requested ||  x == CODE_encrypted_chat || x == CODE_encrypted_chat_discarded);
  U->id = TGL_MK_ENCR_CHAT (fetch_int ());
  if (x == CODE_encrypted_chat_empty) {
    return;
  }
  int new = !(U->flags & FLAG_CREATED);
 
  if (x == CODE_encrypted_chat_discarded) {
    if (new) {
      vlogprintf (E_WARNING, "Unknown chat in deleted state. May be we forgot something...\n");
      return;
    }
    bl_do_encr_chat_delete (U);
    //write_secret_chat_file ();
    return;
  }

  static char g_key[256];
  static char nonce[256];
  if (new) {
    long long access_hash = fetch_long ();
    int date = fetch_int ();
    int admin_id = fetch_int ();
    int user_id = fetch_int () + admin_id - tgl_state.our_id;

    if (x == CODE_encrypted_chat_waiting) {
      vlogprintf (E_WARNING, "Unknown chat in waiting state. May be we forgot something...\n");
      return;
    }
    if (x == CODE_encrypted_chat_requested || x == CODE_encrypted_chat) {
      memset (g_key, 0, sizeof (g_key));
    }

    fetch256 (g_key);
    
    if (x == CODE_encrypted_chat) {
      fetch_long (); // fingerprint
    }

    if (x == CODE_encrypted_chat) {
      vlogprintf (E_WARNING, "Unknown chat in ok state. May be we forgot something...\n");
      return;
    }

    bl_do_encr_chat_requested (U, access_hash, date, admin_id, user_id, (void *)g_key, (void *)nonce);
    //write_secret_chat_file ();
  } else {
    bl_do_encr_chat_set_access_hash (U, fetch_long ());
    bl_do_encr_chat_set_date (U, fetch_int ());
    if (fetch_int () != U->admin_id) {
      vlogprintf (E_WARNING, "Changed admin in secret chat. WTF?\n");
      return;
    }
    if (U->user_id != U->admin_id + fetch_int () - tgl_state.our_id) {
      vlogprintf (E_WARNING, "Changed partner in secret chat. WTF?\n");
      return;
    }
    if (x == CODE_encrypted_chat_waiting) {
      bl_do_encr_chat_set_state (U, sc_waiting);
      //write_secret_chat_file ();
      return; // We needed only access hash from here
    }
    
    if (x == CODE_encrypted_chat_requested || x == CODE_encrypted_chat) {
      memset (g_key, 0, sizeof (g_key));
    }
   
    fetch256 (g_key);
    
    if (x == CODE_encrypted_chat_requested) {
      return; // Duplicate?
    }
    bl_do_encr_chat_accepted (U, (void *)g_key, (void *)nonce, fetch_long ());
    //write_secret_chat_file ();
  }
}

void tglf_fetch_chat (struct tgl_chat *C) {
  unsigned x = fetch_int ();
  assert (x == CODE_chat_empty || x == CODE_chat || x == CODE_chat_forbidden);
  C->id = TGL_MK_CHAT (fetch_int ());
  if (x == CODE_chat_empty) {
    return;
  }
  int new = !(C->flags & FLAG_CREATED);
  if (new) {
    int y = 0;
    if (x == CODE_chat_forbidden) {
      y |= FLAG_FORBIDDEN;
    }
    int l = prefetch_strlen ();
    char *s = fetch_str (l);

    struct tgl_file_location small;
    struct tgl_file_location big;
    memset (&small, 0, sizeof (small));
    memset (&big, 0, sizeof (big));
    int users_num = -1;
    int date = 0;
    int version = -1;

    if (x == CODE_chat) {
      unsigned z = fetch_int ();
      if (z == CODE_chat_photo_empty) {
        small.dc = -2;
        big.dc = -2;
      } else {
        assert (z == CODE_chat_photo);
        tglf_fetch_file_location (&small);
        tglf_fetch_file_location (&big);
      }
      users_num = fetch_int ();
      date = fetch_int ();
      if (fetch_bool ()) {
        y |= FLAG_CHAT_IN_CHAT;
      }
      version = fetch_int ();
    } else {
      small.dc = -2;
      big.dc = -2;
      users_num = -1;
      date = fetch_int ();
      version = -1;
    }

    bl_do_create_chat (C, y, s, l, users_num, date, version, &big, &small);
  } else {
    if (x == CODE_chat_forbidden) {
      bl_do_chat_forbid (C, 1);
    } else {
      bl_do_chat_forbid (C, 0);
    }
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    bl_do_chat_set_title (C, s, l);
    
    struct tgl_file_location small;
    struct tgl_file_location big;
    memset (&small, 0, sizeof (small));
    memset (&big, 0, sizeof (big));
    
    if (x == CODE_chat) {
      unsigned y = fetch_int ();
      if (y == CODE_chat_photo_empty) {
        small.dc = -2;
        big.dc = -2;
      } else {
        assert (y == CODE_chat_photo);
        tglf_fetch_file_location (&small);
        tglf_fetch_file_location (&big);
      }
      bl_do_chat_set_photo (C, &big, &small);
      int users_num = fetch_int ();
      bl_do_chat_set_date (C, fetch_int ());
      bl_do_chat_set_set_in_chat (C, fetch_bool ());
      bl_do_chat_set_version (C, users_num, fetch_int ());
    } else {
      bl_do_chat_set_date (C, fetch_int ());
    }
  }
}

void tglf_fetch_chat_full (struct tgl_chat *C) {
  unsigned x = fetch_int ();
  assert (x == CODE_messages_chat_full);
  assert (fetch_int () == CODE_chat_full); 
  C->id = TGL_MK_CHAT (fetch_int ());
  x = fetch_int ();
  int version = 0;
  struct tgl_chat_user *users = 0;
  int users_num = 0;
  int admin_id = 0;

  if (x == CODE_chat_participants) {
    assert (fetch_int () == tgl_get_peer_id (C->id));
    admin_id =  fetch_int ();
    assert (fetch_int () == CODE_vector);
    users_num = fetch_int ();
    users = talloc (sizeof (struct tgl_chat_user) * users_num);
    int i;
    for (i = 0; i < users_num; i++) {
      assert (fetch_int () == (int)CODE_chat_participant);
      users[i].user_id = fetch_int ();
      users[i].inviter_id = fetch_int ();
      users[i].date = fetch_int ();
    }
    version = fetch_int ();
  }
  int *start = in_ptr;
  assert (skip_type_any (TYPE_TO_PARAM (photo)) >= 0);
  int *end = in_ptr;
  assert (skip_type_any (TYPE_TO_PARAM (peer_notify_settings)) >= 0);

  int n, i;
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_chat ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_user ();
  }
  if (admin_id) {
    bl_do_chat_set_admin (C, admin_id);
  }
  if (version > 0) {
    bl_do_chat_set_participants (C, version, users_num, users);
    tfree (users, sizeof (struct tgl_chat_user) * users_num);
  }
  bl_do_chat_set_full_photo (C, start, 4 * (end - start));
}

void tglf_fetch_photo_size (struct tgl_photo_size *S) {
  memset (S, 0, sizeof (*S));
  unsigned x = fetch_int ();
  assert (x == CODE_photo_size || x == CODE_photo_cached_size || x == CODE_photo_size_empty);
  S->type = fetch_str_dup ();
  if (x != CODE_photo_size_empty) {
    tglf_fetch_file_location (&S->loc);
    S->w = fetch_int ();
    S->h = fetch_int ();
    if (x == CODE_photo_size) {
      S->size = fetch_int ();
    } else {
      S->size = prefetch_strlen ();
      fetch_str (S->size);
    }
  }
}

void tglf_fetch_geo (struct tgl_geo *G) {
  unsigned x = fetch_int ();
  if (x == CODE_geo_point) {
    G->longitude = fetch_double ();
    G->latitude = fetch_double ();
  } else {
    assert (x == CODE_geo_point_empty);
    G->longitude = 0;
    G->latitude = 0;
  }
}

void tglf_fetch_photo (struct tgl_photo *P) {
  memset (P, 0, sizeof (*P));
  unsigned x = fetch_int ();
  assert (x == CODE_photo_empty || x == CODE_photo);
  P->id = fetch_long ();
  if (x == CODE_photo_empty) { return; }
  P->access_hash = fetch_long ();
  P->user_id = fetch_int ();
  P->date = fetch_int ();
  P->caption = fetch_str_dup ();
  tglf_fetch_geo (&P->geo);
  assert (fetch_int () == CODE_vector);
  P->sizes_num = fetch_int ();
  P->sizes = talloc (sizeof (struct tgl_photo_size) * P->sizes_num);
  int i;
  for (i = 0; i < P->sizes_num; i++) {
    tglf_fetch_photo_size (&P->sizes[i]);
  }
}

void tglf_fetch_video (struct tgl_video *V) {
  memset (V, 0, sizeof (*V));
  unsigned x = fetch_int ();
  V->id = fetch_long ();
  if (x == CODE_video_empty) { return; }
  V->access_hash = fetch_long ();
  V->user_id = fetch_int ();
  V->date = fetch_int ();
  V->caption = fetch_str_dup ();
  V->duration = fetch_int ();
  V->mime_type = fetch_str_dup ();
  V->size = fetch_int ();
  tglf_fetch_photo_size (&V->thumb);
  V->dc_id = fetch_int ();
  V->w = fetch_int ();
  V->h = fetch_int ();
}

void tglf_fetch_audio (struct tgl_audio *V) {
  memset (V, 0, sizeof (*V));
  unsigned x = fetch_int ();
  V->id = fetch_long ();
  if (x == CODE_audio_empty) { return; }
  V->access_hash = fetch_long ();
  V->user_id = fetch_int ();
  V->date = fetch_int ();
  V->duration = fetch_int ();
  V->mime_type = fetch_str_dup ();
  V->size = fetch_int ();
  V->dc_id = fetch_int ();
}

void tglf_fetch_document (struct tgl_document *V) {
  memset (V, 0, sizeof (*V));
  unsigned x = fetch_int ();
  V->id = fetch_long ();
  if (x == CODE_document_empty) { return; }
  V->access_hash = fetch_long ();
  V->user_id = fetch_int ();
  V->date = fetch_int ();
  V->caption = fetch_str_dup ();
  V->mime_type = fetch_str_dup ();
  V->size = fetch_int ();
  tglf_fetch_photo_size (&V->thumb);
  V->dc_id = fetch_int ();
}

void tglf_fetch_message_action (struct tgl_message_action *M) {
  memset (M, 0, sizeof (*M));
  unsigned x = fetch_int ();
  switch (x) {
  case CODE_message_action_empty:
    M->type = tgl_message_action_none;
    break;
  case CODE_message_action_geo_chat_create:
    {
      M->type = tgl_message_action_geo_chat_create;
      int l = prefetch_strlen (); // title
      char *s = fetch_str (l);
      int l2 = prefetch_strlen (); // checkin
      char *s2 = fetch_str (l2);
      vlogprintf (E_ERROR, "Message action: Created geochat %.*s in address %.*s. You are in magic land now, since nobody ever tested geochats in this app\n", l, s, l2, s2);
    }
    break;
  case CODE_message_action_geo_chat_checkin:
    M->type = tgl_message_action_geo_chat_checkin;
    break;
  case CODE_message_action_chat_create:
    M->type = tgl_message_action_chat_create;
    M->title = fetch_str_dup ();
    assert (fetch_int () == (int)CODE_vector);
    M->user_num = fetch_int ();
    M->users = talloc (M->user_num * 4);
    fetch_ints (M->users, M->user_num);
    break;
  case CODE_message_action_chat_edit_title:
    M->type = tgl_message_action_chat_edit_title;
    M->new_title = fetch_str_dup ();
    break;
  case CODE_message_action_chat_edit_photo:
    M->type = tgl_message_action_chat_edit_photo;
    tglf_fetch_photo (&M->photo);
    break;
  case CODE_message_action_chat_delete_photo:
    M->type = tgl_message_action_chat_delete_photo;
    break;
  case CODE_message_action_chat_add_user:
    M->type = tgl_message_action_chat_add_user;
    M->user = fetch_int ();
    break;
  case CODE_message_action_chat_delete_user:
    M->type = tgl_message_action_chat_delete_user;
    M->user = fetch_int ();
    break;
  default:
    assert (0);
  }
}

void tglf_fetch_message_short (struct tgl_message *M) {
  int new = !(M->flags & FLAG_CREATED);

  if (new) {
    int id = fetch_int ();
    int from_id = fetch_int ();
    int to_id = tgl_state.our_id;
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    
    tglu_fetch_pts ();
    
    int date = fetch_int ();
    //tglu_fetch_seq ();
    int seq = fetch_int ();
    assert (seq == tgl_state.seq + 1);

    bl_do_create_message_text (id, from_id, TGL_PEER_USER, to_id, date, 1, l, s);
    //bl_do_msg_seq_update (id);
  } else {
    fetch_int (); // id
    fetch_int (); // from_id
    int l = prefetch_strlen (); 
    fetch_str (l); // text
    
    tglu_fetch_pts ();
    fetch_int ();
    //tglu_fetch_seq ();
    int seq = fetch_int ();
    assert (seq == tgl_state.seq + 1);
    //bl_do_msg_seq_update (id);
  }
}

void tglf_fetch_message_short_chat (struct tgl_message *M) {
  int new = !(M->flags & FLAG_CREATED);

  if (new) {
    int id = fetch_int ();
    int from_id = fetch_int ();
    int to_id = fetch_int ();
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    
    tglu_fetch_pts ();
    
    int date = fetch_int ();
    //tglu_fetch_seq ();

    int seq = fetch_int ();
    assert (seq == tgl_state.seq + 1);
    bl_do_create_message_text (id, from_id, TGL_PEER_CHAT, to_id, date, 1, l, s);
    //bl_do_msg_seq_update (id);
  } else {
    fetch_int (); // id
    fetch_int (); // from_id
    fetch_int (); // to_id
    int l = prefetch_strlen (); 
    fetch_str (l); // text
    
    tglu_fetch_pts ();
    fetch_int ();
    //tglu_fetch_seq ();
    int seq = fetch_int ();
    assert (seq == tgl_state.seq + 1);
    //bl_do_msg_seq_update (id);
  }
}


void tglf_fetch_message_media (struct tgl_message_media *M) {
  memset (M, 0, sizeof (*M));
  //M->type = fetch_int ();
  int x = fetch_int ();
  switch (x) {
  case CODE_message_media_empty:
    M->type = tgl_message_media_none;
    break;
  case CODE_message_media_photo:
    M->type = tgl_message_media_photo;
    tglf_fetch_photo (&M->photo);
    break;
  case CODE_message_media_video:
    M->type = tgl_message_media_video;
    tglf_fetch_video (&M->video);
    break;
  case CODE_message_media_audio:
    M->type = tgl_message_media_audio;
    tglf_fetch_audio (&M->audio);
    break;
  case CODE_message_media_document:
    M->type = tgl_message_media_document;
    tglf_fetch_document (&M->document);
    break;
  case CODE_message_media_geo:
    M->type = tgl_message_media_geo;
    tglf_fetch_geo (&M->geo);
    break;
  case CODE_message_media_contact:
    M->type = tgl_message_media_contact;
    M->phone = fetch_str_dup ();
    M->first_name = fetch_str_dup ();
    M->last_name = fetch_str_dup ();
    M->user_id = fetch_int ();
    break;
  case CODE_message_media_unsupported:
    M->type = tgl_message_media_unsupported;
    M->data_size = prefetch_strlen ();
    M->data = talloc (M->data_size);
    memcpy (M->data, fetch_str (M->data_size), M->data_size);
    break;
  default:
    vlogprintf (E_ERROR, "type = 0x%08x\n", M->type);
    assert (0);
  }
}

void tglf_fetch_message_media_encrypted (struct tgl_message_media *M) {
  memset (M, 0, sizeof (*M));
  unsigned x = fetch_int ();
  int l;
  switch (x) {
  case CODE_decrypted_message_media_empty:
    M->type = tgl_message_media_none;
    //M->type = CODE_message_media_empty;
    break;
  case CODE_decrypted_message_media_photo:
    M->type = tgl_message_media_photo_encr;
    //M->type = x;
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    fetch_int (); // thumb_w
    fetch_int (); // thumb_h
    M->encr_photo.w = fetch_int ();
    M->encr_photo.h = fetch_int ();
    M->encr_photo.size = fetch_int ();
    
    l = prefetch_strlen  ();
    assert (l > 0);
    M->encr_photo.key = talloc (32);
    memset (M->encr_photo.key, 0, 32);
    if (l <= 32) {
      memcpy (M->encr_photo.key + (32 - l), fetch_str (l), l);
    } else {
      memcpy (M->encr_photo.key, fetch_str (l) + (l - 32), 32);
    }
    M->encr_photo.iv = talloc (32);
    l = prefetch_strlen  ();
    assert (l > 0);
    memset (M->encr_photo.iv, 0, 32);
    if (l <= 32) {
      memcpy (M->encr_photo.iv + (32 - l), fetch_str (l), l);
    } else {
      memcpy (M->encr_photo.iv, fetch_str (l) + (l - 32), 32);
    }
    break;
  case CODE_decrypted_message_media_video:
  case CODE_decrypted_message_media_video_l12:
    //M->type = CODE_decrypted_message_media_video;
    M->type = tgl_message_media_video_encr;
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    fetch_int (); // thumb_w
    fetch_int (); // thumb_h
    M->encr_video.duration = fetch_int ();
    if (x == CODE_decrypted_message_media_video) {
      M->encr_video.mime_type = fetch_str_dup ();
    }
    M->encr_video.w = fetch_int ();
    M->encr_video.h = fetch_int ();
    M->encr_video.size = fetch_int ();
    
    l = prefetch_strlen  ();
    assert (l > 0);
    M->encr_video.key = talloc0 (32);
    if (l <= 32) {
      memcpy (M->encr_video.key + (32 - l), fetch_str (l), l);
    } else {
      memcpy (M->encr_video.key, fetch_str (l) + (l - 32), 32);
    }
    M->encr_video.iv = talloc (32);
    l = prefetch_strlen  ();
    assert (l > 0);
    memset (M->encr_video.iv, 0, 32);
    if (l <= 32) {
      memcpy (M->encr_video.iv + (32 - l), fetch_str (l), l);
    } else {
      memcpy (M->encr_video.iv, fetch_str (l) + (l - 32), 32);
    }
    break;
  case CODE_decrypted_message_media_audio:
  case CODE_decrypted_message_media_audio_l12:
    //M->type = CODE_decrypted_message_media_audio;
    M->type = tgl_message_media_audio_encr;
    M->encr_audio.duration = fetch_int ();
    if (x == CODE_decrypted_message_media_audio) {
      M->encr_audio.mime_type = fetch_str_dup ();
    }
    M->encr_audio.size = fetch_int ();
    
    l = prefetch_strlen  ();
    assert (l > 0);
    M->encr_video.key = talloc0 (32);
    if (l <= 32) {
      memcpy (M->encr_video.key + (32 - l), fetch_str (l), l);
    } else {
      memcpy (M->encr_video.key, fetch_str (l) + (l - 32), 32);
    }
    M->encr_video.iv = talloc0 (32);
    l = prefetch_strlen  ();
    assert (l > 0);
    if (l <= 32) {
      memcpy (M->encr_video.iv + (32 - l), fetch_str (l), l);
    } else {
      memcpy (M->encr_video.iv, fetch_str (l) + (l - 32), 32);
    }
    break;
  case CODE_decrypted_message_media_document:
    M->type = tgl_message_media_document_encr;
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    fetch_int (); // thumb_w
    fetch_int (); // thumb_h
    M->encr_document.file_name = fetch_str_dup ();
    M->encr_document.mime_type = fetch_str_dup ();
    M->encr_video.size = fetch_int ();
    
    l = prefetch_strlen  ();
    assert (l > 0);
    M->encr_video.key = talloc0 (32);
    if (l <= 32) {
      memcpy (M->encr_video.key + (32 - l), fetch_str (l), l);
    } else {
      memcpy (M->encr_video.key, fetch_str (l) + (l - 32), 32);
    }
    M->encr_video.iv = talloc0 (32);
    l = prefetch_strlen  ();
    assert (l > 0);
    if (l <= 32) {
      memcpy (M->encr_video.iv + (32 - l), fetch_str (l), l);
    } else {
      memcpy (M->encr_video.iv, fetch_str (l) + (l - 32), 32);
    }
    break;
/*  case CODE_decrypted_message_media_file:
    M->type = x;
    M->encr_file.filename = fetch_str_dup ();
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    l = fetch_int ();
    assert (l > 0);
    M->encr_file.key = talloc (l);
    memcpy (M->encr_file.key, fetch_str (l), l);
    
    l = fetch_int ();
    assert (l > 0);
    M->encr_file.iv = talloc (l);
    memcpy (M->encr_file.iv, fetch_str (l), l);
    break;
  */  
  case CODE_decrypted_message_media_geo_point:
    M->type = tgl_message_media_geo;
    M->geo.longitude = fetch_double ();
    M->geo.latitude = fetch_double ();
    break;
  case CODE_decrypted_message_media_contact:
    M->type = tgl_message_media_contact;
    M->phone = fetch_str_dup ();
    M->first_name = fetch_str_dup ();
    M->last_name = fetch_str_dup ();
    M->user_id = fetch_int ();
    break;
  default:
    vlogprintf (E_ERROR, "type = 0x%08x\n", x);
    assert (0);
  }
}

void tglf_fetch_message_action_encrypted (struct tgl_message_action *M) {
  unsigned x = fetch_int ();
  switch (x) {
  case CODE_decrypted_message_action_set_message_t_t_l:
    M->type = tgl_message_action_set_message_ttl;
    M->ttl = fetch_int ();
    break;
  case CODE_decrypted_message_action_read_messages: 
    M->type = tgl_message_action_read_messages;
    { 
      assert (fetch_int () == CODE_vector);
      int n = fetch_int ();
      M->read_cnt = n;
      while (n -- > 0) {
        long long id = fetch_long ();
        struct tgl_message *N = tgl_message_get (id);
        if (N) {
          N->unread = 0;
        }
      }
    }
    break;
  case CODE_decrypted_message_action_delete_messages: 
    M->type = tgl_message_action_delete_messages;
    { 
      assert (fetch_int () == CODE_vector);
      int n = fetch_int ();
      M->delete_cnt = n;
      while (n -- > 0) {
        fetch_long ();
      }
    }
    break;
  case CODE_decrypted_message_action_screenshot_messages: 
    M->type = tgl_message_action_screenshot_messages;
    { 
      assert (fetch_int () == CODE_vector);
      int n = fetch_int ();
      M->screenshot_cnt = n;
      while (n -- > 0) {
        fetch_long ();
      }
    }
    break;
  case CODE_decrypted_message_action_notify_layer: 
    M->type = tgl_message_action_notify_layer;
    M->layer = fetch_int ();
    break;
  case CODE_decrypted_message_action_flush_history:
    M->type = tgl_message_action_flush_history;
    break;
  default:
    vlogprintf (E_ERROR, "x = 0x%08x\n", x);
    assert (0);
  }
}

tgl_peer_id_t tglf_fetch_peer_id (void) {
  unsigned x =fetch_int ();
  if (x == CODE_peer_user) {
    return TGL_MK_USER (fetch_int ());
  } else {
    assert (CODE_peer_chat);
    return TGL_MK_CHAT (fetch_int ());
  }
}

void tglf_fetch_message (struct tgl_message *M) {
  unsigned x = fetch_int ();
  assert (x == CODE_message_empty || x == CODE_message || x == CODE_message_forwarded || x == CODE_message_service);
  int id = fetch_int ();
  assert (M->id == id);
  if (x == CODE_message_empty) {
    return;
  }
  int fwd_from_id = 0;
  int fwd_date = 0;

  if (x == CODE_message_forwarded) {
    fwd_from_id = fetch_int ();
    fwd_date = fetch_int ();
  }
  int from_id = fetch_int ();
  tgl_peer_id_t to_id = tglf_fetch_peer_id ();

  fetch_bool (); // out.

  int unread = fetch_bool ();
  int date = fetch_int ();

  int new = !(M->flags & FLAG_CREATED);

  if (x == CODE_message_service) {
    int *start = in_ptr;

    assert (skip_type_any (TYPE_TO_PARAM (message_action)) >= 0);
    
    if (new) {
      if (fwd_from_id) {
        bl_do_create_message_service_fwd (id, from_id, tgl_get_peer_type (to_id), tgl_get_peer_id (to_id), date, fwd_from_id, fwd_date, unread, start, (in_ptr - start));
      } else {
        bl_do_create_message_service (id, from_id, tgl_get_peer_type (to_id), tgl_get_peer_id (to_id), date, unread, start, (in_ptr - start));
      }
    }
  } else {
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    int *start = in_ptr;
    
    assert (skip_type_any (TYPE_TO_PARAM (message_media)) >= 0);

    if (new) {
      if (fwd_from_id) {
        bl_do_create_message_media_fwd (id, from_id, tgl_get_peer_type (to_id), tgl_get_peer_id (to_id), date, fwd_from_id, fwd_date, unread, l, s, start, in_ptr - start);
      } else {
        bl_do_create_message_media (id, from_id, tgl_get_peer_type (to_id), tgl_get_peer_id (to_id), date, unread, l, s, start, in_ptr - start);
      }
    }
  }
  bl_do_set_unread (M, unread);
}

void tglf_tglf_fetch_geo_message (struct tgl_message *M) {
  memset (M, 0, sizeof (*M));
  unsigned x = fetch_int ();
  assert (x == CODE_geo_chat_message_empty || x == CODE_geo_chat_message || x == CODE_geo_chat_message_service);
  M->to_id = TGL_MK_GEO_CHAT (fetch_int ());
  M->id = fetch_int ();
  if (x == CODE_geo_chat_message_empty) {
    M->flags |= 1;
    return;
  }
  M->from_id = TGL_MK_USER (fetch_int ());
  M->date = fetch_int ();
  if (x == CODE_geo_chat_message_service) {
    M->service = 1;
    tglf_fetch_message_action (&M->action);
  } else {
    M->message = fetch_str_dup ();
    M->message_len = strlen (M->message);
    tglf_fetch_message_media (&M->media);
  }
}

static int *decr_ptr;
static int *decr_end;

static int decrypt_encrypted_message (struct tgl_secret_chat *E) {
  int *msg_key = decr_ptr;
  decr_ptr += 4;
  assert (decr_ptr < decr_end);
  static unsigned char sha1a_buffer[20];
  static unsigned char sha1b_buffer[20];
  static unsigned char sha1c_buffer[20];
  static unsigned char sha1d_buffer[20];
 
  static unsigned char buf[64];
  memcpy (buf, msg_key, 16);
  memcpy (buf + 16, E->key, 32);
  sha1 (buf, 48, sha1a_buffer);
  
  memcpy (buf, E->key + 8, 16);
  memcpy (buf + 16, msg_key, 16);
  memcpy (buf + 32, E->key + 12, 16);
  sha1 (buf, 48, sha1b_buffer);
  
  memcpy (buf, E->key + 16, 32);
  memcpy (buf + 32, msg_key, 16);
  sha1 (buf, 48, sha1c_buffer);
  
  memcpy (buf, msg_key, 16);
  memcpy (buf + 16, E->key + 24, 32);
  sha1 (buf, 48, sha1d_buffer);

  static unsigned char key[32];
  memcpy (key, sha1a_buffer + 0, 8);
  memcpy (key + 8, sha1b_buffer + 8, 12);
  memcpy (key + 20, sha1c_buffer + 4, 12);

  static unsigned char iv[32];
  memcpy (iv, sha1a_buffer + 8, 12);
  memcpy (iv + 12, sha1b_buffer + 0, 8);
  memcpy (iv + 20, sha1c_buffer + 16, 4);
  memcpy (iv + 24, sha1d_buffer + 0, 8);

  AES_KEY aes_key;
  AES_set_decrypt_key (key, 256, &aes_key);
  AES_ige_encrypt ((void *)decr_ptr, (void *)decr_ptr, 4 * (decr_end - decr_ptr), &aes_key, iv, 0);
  memset (&aes_key, 0, sizeof (aes_key));

  int x = *(decr_ptr);
  if (x < 0 || (x & 3)) {
    return -1;
  }
  assert (x >= 0 && !(x & 3));
  sha1 ((void *)decr_ptr, 4 + x, sha1a_buffer);

  if (memcmp (sha1a_buffer + 4, msg_key, 16)) {
    vlogprintf (E_WARNING, "Sha1 mismatch\n");
    return -1;
  }
  return 0;
}

void tglf_fetch_encrypted_message (struct tgl_message *M) {
  unsigned x = fetch_int ();
  assert (x == CODE_encrypted_message || x == CODE_encrypted_message_service);
  unsigned sx = x;
  int new = !(M->flags & FLAG_CREATED);
  long long id = fetch_long ();
  int to_id = fetch_int ();
  tgl_peer_id_t chat = TGL_MK_ENCR_CHAT (to_id);
  int date = fetch_int ();
  
  tgl_peer_t *P = tgl_peer_get (chat);
  if (!P) {
    vlogprintf (E_WARNING, "Encrypted message to unknown chat. Dropping\n");
    M->flags |= FLAG_MESSAGE_EMPTY;
  }


  int len = prefetch_strlen ();
  assert ((len & 15) == 8);
  decr_ptr = (void *)fetch_str (len);
  decr_end = decr_ptr + (len / 4);
  int ok = 0;
  if (P) {
    if (*(long long *)decr_ptr != P->encr_chat.key_fingerprint) {
      vlogprintf (E_WARNING, "Encrypted message with bad fingerprint to chat %s\n", P->print_name);
      P = 0;
    }
    decr_ptr += 2;
  }
  int l = 0;
  char *s = 0;
  int *start = 0;
  int *end = 0;
  x = 0;
  if (P && decrypt_encrypted_message (&P->encr_chat) >= 0 && new) {
    ok = 1;
    int *save_in_ptr = in_ptr;
    int *save_in_end = in_end;
    in_ptr = decr_ptr;
    int ll = fetch_int ();
    in_end = in_ptr + ll; 
    x = fetch_int ();
    if (x == CODE_decrypted_message_layer) {
      int layer = fetch_int ();
      assert (layer >= 0);
      x = fetch_int ();
    }
    assert (x == CODE_decrypted_message || x == CODE_decrypted_message_service);
    //assert (id == fetch_long ());
    fetch_long ();
    ll = prefetch_strlen ();
    fetch_str (ll); // random_bytes
    if (x == CODE_decrypted_message) {
      l = prefetch_strlen ();
      s = fetch_str (l);
      start = in_ptr;
      assert (skip_type_any (TYPE_TO_PARAM (decrypted_message_media)) >= 0);
      end = in_ptr;
    } else {
      start = in_ptr;
      assert (skip_type_any (TYPE_TO_PARAM (decrypted_message_action)) >= 0);
      end = in_ptr;
    }
    in_ptr = save_in_ptr;
    in_end = save_in_end;
  } 
  if (sx == CODE_encrypted_message) {
    if (ok) {
      int *start_file = in_ptr;
      assert (skip_type_any (TYPE_TO_PARAM (encrypted_file)) >= 0);
      if (x == CODE_decrypted_message) {
        bl_do_create_message_media_encr (id, P->encr_chat.user_id, TGL_PEER_ENCR_CHAT, to_id, date, l, s, start, end - start, start_file, in_ptr - start_file);
      } else if (x == CODE_decrypted_message_service) {
        bl_do_create_message_service_encr (id, P->encr_chat.user_id, TGL_PEER_ENCR_CHAT, to_id, date, start, end - start);
      }
    } else {
      assert (skip_type_any (TYPE_TO_PARAM (encrypted_file)) >= 0);
      M->media.type = CODE_message_media_empty;
    }    
  } else {
    if (ok && x == CODE_decrypted_message_service) {
      bl_do_create_message_service_encr (id, P->encr_chat.user_id, TGL_PEER_ENCR_CHAT, to_id, date, start, end - start);
    }
  }
}

void tglf_fetch_encrypted_message_file (struct tgl_message_media *M) {
  unsigned x = fetch_int ();
  assert (x == CODE_encrypted_file || x == CODE_encrypted_file_empty);
  if (x == CODE_encrypted_file_empty) {
    assert (M->type != tgl_message_media_photo_encr && M->type != tgl_message_media_video_encr);
  } else {
    assert (M->type == tgl_message_media_document_encr || M->type == tgl_message_media_photo_encr || M->type == tgl_message_media_video_encr || M->type == tgl_message_media_audio_encr);

    M->encr_photo.id = fetch_long();
    M->encr_photo.access_hash = fetch_long();
    if (!M->encr_photo.size) {
      M->encr_photo.size = fetch_int ();
    } else {
      fetch_int ();
    }
    M->encr_photo.dc_id = fetch_int();
    M->encr_photo.key_fingerprint = fetch_int();
  }
}

static int id_cmp (struct tgl_message *M1, struct tgl_message *M2) {
  if (M1->id < M2->id) { return -1; }
  else if (M1->id > M2->id) { return 1; }
  else { return 0; }
}

struct tgl_user *tglf_fetch_alloc_user (void) {
  int data[2];
  prefetch_data (data, 8);
  tgl_peer_t *U = tgl_peer_get (TGL_MK_USER (data[1]));
  if (!U) {
    users_allocated ++;
    U = talloc0 (sizeof (*U));
    U->id = TGL_MK_USER (data[1]);
    tgl_peer_tree = tree_insert_peer (tgl_peer_tree, U, lrand48 ());
    assert (peer_num < TGL_MAX_PEER_NUM);
    Peers[peer_num ++] = U;
  }
  tglf_fetch_user (&U->user);
  return &U->user;
}

struct tgl_secret_chat *tglf_fetch_alloc_encrypted_chat (void) {
  int data[2];
  prefetch_data (data, 8);
  tgl_peer_t *U = tgl_peer_get (TGL_MK_ENCR_CHAT (data[1]));
  if (!U) {
    U = talloc0 (sizeof (*U));
    U->id = TGL_MK_ENCR_CHAT (data[1]);
    encr_chats_allocated ++;
    tgl_peer_tree = tree_insert_peer (tgl_peer_tree, U, lrand48 ());
    assert (peer_num < TGL_MAX_PEER_NUM);
    Peers[peer_num ++] = U;
  }
  tglf_fetch_encrypted_chat (&U->encr_chat);
  return &U->encr_chat;
}

struct tgl_user *tglf_fetch_alloc_user_full (void) {
  int data[3];
  prefetch_data (data, 12);
  tgl_peer_t *U = tgl_peer_get (TGL_MK_USER (data[2]));
  if (U) {
    tglf_fetch_user_full (&U->user);
    return &U->user;
  } else {
    users_allocated ++;
    U = talloc0 (sizeof (*U));
    U->id = TGL_MK_USER (data[2]);
    tgl_peer_tree = tree_insert_peer (tgl_peer_tree, U, lrand48 ());
    tglf_fetch_user_full (&U->user);
    assert (peer_num < TGL_MAX_PEER_NUM);
    Peers[peer_num ++] = U;
    return &U->user;
  }
}

struct tgl_message *tglf_fetch_alloc_message (void) {
  int data[2];
  prefetch_data (data, 8);
  struct tgl_message *M = tgl_message_get (data[1]);

  if (!M) {
    M = tglm_message_alloc (data[1]);
  }
  tglf_fetch_message (M);
  return M;
}

struct tgl_message *tglf_fetch_alloc_geo_message (void) {
  struct tgl_message *M = talloc (sizeof (*M));
  tglf_tglf_fetch_geo_message (M);
  struct tgl_message *M1 = tree_lookup_message (message_tree, M);
  messages_allocated ++;
  if (M1) {
    tglm_message_del_use (M1);
    tglm_message_del_peer (M1);
    tgls_free_message (M1);
    memcpy (M1, M, sizeof (*M));
    tfree (M, sizeof (*M));
    tglm_message_add_use (M1);
    tglm_message_add_peer (M1);
    messages_allocated --;
    return M1;
  } else {
    tglm_message_add_use (M);
    tglm_message_add_peer (M);
    message_tree = tree_insert_message (message_tree, M, lrand48 ());
    return M;
  }
}

struct tgl_message *tglf_fetch_alloc_encrypted_message (void) {
  int data[3];
  prefetch_data (data, 12);
  struct tgl_message *M = tgl_message_get (*(long long *)(data + 1));

  if (!M) {
    M = talloc0 (sizeof (*M));
    M->id = *(long long *)(data + 1);
    tglm_message_insert_tree (M);
    messages_allocated ++;
    assert (tgl_message_get (M->id) == M);
  }
  tglf_fetch_encrypted_message (M);
  return M;
}

struct tgl_message *tglf_fetch_alloc_message_short (void) {
  int data[1];
  prefetch_data (data, 4);
  struct tgl_message *M = tgl_message_get (data[0]);

  if (!M) {
    M = talloc0 (sizeof (*M));
    M->id = data[0];
    tglm_message_insert_tree (M);
    messages_allocated ++;
  }
  tglf_fetch_message_short (M);
  return M;
}

struct tgl_message *tglf_fetch_alloc_message_short_chat (void) {
  int data[1];
  prefetch_data (data, 4);
  struct tgl_message *M = tgl_message_get (data[0]);

  if (!M) {
    M = talloc0 (sizeof (*M));
    M->id = data[0];
    tglm_message_insert_tree (M);
    messages_allocated ++;
  }
  tglf_fetch_message_short_chat (M);
  return M;
}

struct tgl_chat *tglf_fetch_alloc_chat (void) {
  int data[2];
  prefetch_data (data, 8);
  tgl_peer_t *U = tgl_peer_get (TGL_MK_CHAT (data[1]));
  if (!U) {
    chats_allocated ++;
    U = talloc0 (sizeof (*U));
    U->id = TGL_MK_CHAT (data[1]);
    tgl_peer_tree = tree_insert_peer (tgl_peer_tree, U, lrand48 ());
    assert (peer_num < TGL_MAX_PEER_NUM);
    Peers[peer_num ++] = U;
  }
  tglf_fetch_chat (&U->chat);
  return &U->chat;
}

struct tgl_chat *tglf_fetch_alloc_chat_full (void) {
  int data[3];
  prefetch_data (data, 12);
  tgl_peer_t *U = tgl_peer_get (TGL_MK_CHAT (data[2]));
  if (U) {
    tglf_fetch_chat_full (&U->chat);
    return &U->chat;
  } else {
    chats_allocated ++;
    U = talloc0 (sizeof (*U));
    U->id = TGL_MK_CHAT (data[2]);
    tgl_peer_tree = tree_insert_peer (tgl_peer_tree, U, lrand48 ());
    tglf_fetch_chat_full (&U->chat);
    assert (peer_num < TGL_MAX_PEER_NUM);
    Peers[peer_num ++] = U;
    return &U->chat;
  }
}
/* }}} */

void tglp_insert_encrypted_chat (tgl_peer_t *P) {
  encr_chats_allocated ++;
  tgl_peer_tree = tree_insert_peer (tgl_peer_tree, P, lrand48 ());
  assert (peer_num < TGL_MAX_PEER_NUM);
  Peers[peer_num ++] = P;
}

void tglp_insert_user (tgl_peer_t *P) {
  users_allocated ++;
  tgl_peer_tree = tree_insert_peer (tgl_peer_tree, P, lrand48 ());
  assert (peer_num < TGL_MAX_PEER_NUM);
  Peers[peer_num ++] = P;
}

void tglp_insert_chat (tgl_peer_t *P) {
  chats_allocated ++;
  tgl_peer_tree = tree_insert_peer (tgl_peer_tree, P, lrand48 ());
  assert (peer_num < TGL_MAX_PEER_NUM);
  Peers[peer_num ++] = P;
}

/* {{{ Free */
void tgls_free_user (struct tgl_user *U) {
  if (U->first_name) { tfree_str (U->first_name); }
  if (U->last_name) { tfree_str (U->last_name); }
  if (U->print_name) { tfree_str (U->print_name); }
  if (U->phone) { tfree_str (U->phone); }
}

void tgls_free_photo_size (struct tgl_photo_size *S) {
  tfree_str (S->type);
  if (S->data) {
    tfree (S->data, S->size);
  }
}

void tgls_free_photo (struct tgl_photo *P) {
  if (!P->access_hash) { return; }
  if (P->caption) { tfree_str (P->caption); }
  if (P->sizes) {
    int i;
    for (i = 0; i < P->sizes_num; i++) {
      tgls_free_photo_size (&P->sizes[i]);
    }
    tfree (P->sizes, sizeof (struct tgl_photo_size) * P->sizes_num);
  }
}

void tgls_free_video (struct tgl_video *V) {
  if (!V->access_hash) { return; }
  tfree_str (V->caption);
  tgls_free_photo_size (&V->thumb);
}

void tgls_free_document (struct tgl_document *D) {
  if (!D->access_hash) { return; }
  tfree_str (D->caption);
  tfree_str (D->mime_type);
  tgls_free_photo_size (&D->thumb);
}

void tgls_free_message_media (struct tgl_message_media *M) {
  switch (M->type) {
  case tgl_message_media_none:
  case tgl_message_media_geo:
  case tgl_message_media_audio:
    return;
  case tgl_message_media_photo:
    tgls_free_photo (&M->photo);
    return;
  case tgl_message_media_video:
    tgls_free_video (&M->video);
    return;
  case tgl_message_media_contact:
    tfree_str (M->phone);
    tfree_str (M->first_name);
    tfree_str (M->last_name);
    return;
  case tgl_message_media_document:
    tgls_free_document (&M->document);
    return;
  case tgl_message_media_unsupported:
    tfree (M->data, M->data_size);
    return;
  case tgl_message_media_photo_encr:
  case tgl_message_media_video_encr:
  case tgl_message_media_audio_encr:
  case tgl_message_media_document_encr:
    tfree_secure (M->encr_photo.key, 32);
    tfree_secure (M->encr_photo.iv, 32);
    return;
  default:
    vlogprintf (E_ERROR, "type = 0x%08x\n", M->type);
    assert (0);
  }
}

void tgls_free_message_action (struct tgl_message_action *M) {
  switch (M->type) {
  case tgl_message_action_none:
    break;
  case tgl_message_action_chat_create:
    tfree_str (M->title);
    tfree (M->users, M->user_num * 4);
    break;
  case tgl_message_action_chat_edit_title:
    tfree_str (M->new_title);
    break;
  case tgl_message_action_chat_edit_photo:
    tgls_free_photo (&M->photo);
    break;
  case tgl_message_action_chat_delete_photo:
    break;
  case tgl_message_action_chat_add_user:
    break;
  case tgl_message_action_chat_delete_user:
    break;
  default:
    assert (0);
  }
}

void tgls_free_message (struct tgl_message *M) {
  if (!M->service) {
    if (M->message) { tfree (M->message, M->message_len + 1); }
    tgls_free_message_media (&M->media);
  } else {
    tgls_free_message_action (&M->action);
  }
}

void tgls_free_chat (struct tgl_chat *U) {
  if (U->title) { tfree_str (U->title); }
  if (U->print_title) { tfree_str (U->print_title); }
}
/* }}} */

/* Messages {{{ */

void tglm_message_del_use (struct tgl_message *M) {
  M->next_use->prev_use = M->prev_use;
  M->prev_use->next_use = M->next_use;
}

void tglm_message_add_use (struct tgl_message *M) {
  M->next_use = message_list.next_use;
  M->prev_use = &message_list;
  M->next_use->prev_use = M;
  M->prev_use->next_use = M;
}

void tglm_message_add_peer (struct tgl_message *M) {
  tgl_peer_id_t id;
  if (!tgl_cmp_peer_id (M->to_id, TGL_MK_USER (tgl_state.our_id))) {
    id = M->from_id;
  } else {
    id = M->to_id;
  }
  tgl_peer_t *P = tgl_peer_get (id);
  if (!P) {
    P = talloc0 (sizeof (*P));
    P->id = id;
    switch (tgl_get_peer_type (id)) {
    case TGL_PEER_USER:
      users_allocated ++;
      break;
    case TGL_PEER_CHAT:
      chats_allocated ++;
      break;
    case TGL_PEER_GEO_CHAT:
      geo_chats_allocated ++;
      break;
    case TGL_PEER_ENCR_CHAT:
      encr_chats_allocated ++;
      break;
    }
    tgl_peer_tree = tree_insert_peer (tgl_peer_tree, P, lrand48 ());
    assert (peer_num < TGL_MAX_PEER_NUM);
    Peers[peer_num ++] = P;
  }
  if (!P->last) {
    P->last = M;
    M->prev = M->next = 0;
  } else {
    if (tgl_get_peer_type (P->id) != TGL_PEER_ENCR_CHAT) {
      struct tgl_message *N = P->last;
      struct tgl_message *NP = 0;
      while (N && N->id > M->id) {
        NP = N;
        N = N->next;
      }
      if (N) { assert (N->id < M->id); }
      M->next = N;
      M->prev = NP;
      if (N) { N->prev = M; }
      if (NP) { NP->next = M; }
      else { P->last = M; }
    } else {
      struct tgl_message *N = P->last;
      struct tgl_message *NP = 0;
      M->next = N;
      M->prev = NP;
      if (N) { N->prev = M; }
      if (NP) { NP->next = M; }
      else { P->last = M; }
    }
  }
}

void tglm_message_del_peer (struct tgl_message *M) {
  tgl_peer_id_t id;
  if (!tgl_cmp_peer_id (M->to_id, TGL_MK_USER (tgl_state.our_id))) {
    id = M->from_id;
  } else {
    id = M->to_id;
  }
  tgl_peer_t *P = tgl_peer_get (id);
  if (M->prev) {
    M->prev->next = M->next;
  }
  if (M->next) {
    M->next->prev = M->prev;
  }
  if (P && P->last == M) {
    P->last = M->next;
  }
}

struct tgl_message *tglm_message_alloc (long long id) {
  struct tgl_message *M = talloc0 (sizeof (*M));
  M->id = id;
  tglm_message_insert_tree (M);
  messages_allocated ++;
  return M;
}

void tglm_update_message_id (struct tgl_message *M, long long id) {
  message_tree = tree_delete_message (message_tree, M);
  M->id = id;
  message_tree = tree_insert_message (message_tree, M, lrand48 ());
}

void tglm_message_insert_tree (struct tgl_message *M) {
  assert (M->id);
  message_tree = tree_insert_message (message_tree, M, lrand48 ());
}

void tglm_message_remove_tree (struct tgl_message *M) {
  assert (M->id);
  message_tree = tree_delete_message (message_tree, M);
}

void tglm_message_insert (struct tgl_message *M) {
  tglm_message_add_use (M);
  tglm_message_add_peer (M);
}

void tglm_message_insert_unsent (struct tgl_message *M) {
  message_unsent_tree = tree_insert_message (message_unsent_tree, M, lrand48 ());
}

void tglm_message_remove_unsent (struct tgl_message *M) {
  message_unsent_tree = tree_delete_message (message_unsent_tree, M);
}

static void __send_msg (struct tgl_message *M) {
  vlogprintf (E_NOTICE, "Resending message...\n");
  //print_message (M);

  tgl_do_send_msg (M, 0, 0);
}

void tglm_send_all_unsent (void ) {
  tree_act_message (message_unsent_tree, __send_msg);
}
/* }}} */

void tglp_peer_insert_name (tgl_peer_t *P) {
  peer_by_name_tree = tree_insert_peer_by_name (peer_by_name_tree, P, lrand48 ());
}

void tglp_peer_delete_name (tgl_peer_t *P) {
  peer_by_name_tree = tree_delete_peer_by_name (peer_by_name_tree, P);
}

tgl_peer_t *tgl_peer_get (tgl_peer_id_t id) {
  static tgl_peer_t U;
  U.id = id;
  return tree_lookup_peer (tgl_peer_tree, &U);
}

struct tgl_message *tgl_message_get (long long id) {
  struct tgl_message M;
  M.id = id;
  return tree_lookup_message (message_tree, &M);
}

tgl_peer_t *tgl_peer_get_by_name (const char *s) {
  static tgl_peer_t P;
  P.print_name = (void *)s;
  tgl_peer_t *R = tree_lookup_peer_by_name (peer_by_name_tree, &P);
  return R;
}

void tgl_peer_iterator_ex (void (*it)(tgl_peer_t *P, void *extra), void *extra) {
  tree_act_ex_peer (tgl_peer_tree, it, extra);
}

int tgl_complete_user_list (int index, const char *text, int len, char **R) {
  index ++;
  while (index < peer_num && (!Peers[index]->print_name || strncmp (Peers[index]->print_name, text, len) || tgl_get_peer_type (Peers[index]->id) != TGL_PEER_USER)) {
    index ++;
  }
  if (index < peer_num) {
    *R = strdup (Peers[index]->print_name);
    return index;
  } else {
    return -1;
  }
}

int tgl_complete_chat_list (int index, const char *text, int len, char **R) {
  index ++;
  while (index < peer_num && (!Peers[index]->print_name || strncmp (Peers[index]->print_name, text, len) || tgl_get_peer_type (Peers[index]->id) != TGL_PEER_CHAT)) {
    index ++;
  }
  if (index < peer_num) {
    *R = strdup (Peers[index]->print_name);
    return index;
  } else {
    return -1;
  }
}

int tgl_complete_encr_chat_list (int index, const char *text, int len, char **R) {
  index ++;
  while (index < peer_num && (!Peers[index]->print_name || strncmp (Peers[index]->print_name, text, len) || tgl_get_peer_type (Peers[index]->id) != TGL_PEER_ENCR_CHAT)) {
    index ++;
  }
  if (index < peer_num) {
    *R = strdup (Peers[index]->print_name);
    return index;
  } else {
    return -1;
  }
}

int tgl_complete_peer_list (int index, const char *text, int len, char **R) {
  index ++;
  while (index < peer_num && (!Peers[index]->print_name || strncmp (Peers[index]->print_name, text, len))) {
    index ++;
  }
  if (index < peer_num) {
    *R = strdup (Peers[index]->print_name);
    return index;
  } else {
    return -1;
  }
}

int tgl_print_stat (char *s, int len) {
  return tsnprintf (s, len, 
    "users_allocated\t%d\n"
    "chats_allocated\t%d\n"
    "secret_chats_allocated\t%d\n"
    "peer_num\t%d\n"
    "messages_allocated\t%d\n",
    users_allocated,
    chats_allocated,
    encr_chats_allocated,
    peer_num,
    messages_allocated
    );
}
