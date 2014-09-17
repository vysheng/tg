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
#include "tgl.h"
#include "updates.h"
#include "mtproto-common.h"
#include "binlog.h"
#include "auto.h"
#include "structures.h"

#include <assert.h>

void tglu_fetch_pts (void) {
  int p = fetch_int ();
  if (p <= tgl_state.pts) { return; }
  bl_do_set_pts (p);
}

void tglu_fetch_qts (void) {
  int p = fetch_int ();
  if (p <= tgl_state.qts) { return; }
  bl_do_set_qts (p);
}

void tglu_fetch_date (void) {
  int p = fetch_int ();
  if (p > tgl_state.date) {
    //tgl_state.date = p;
    bl_do_set_date (tgl_state.date);
  }
}

static void fetch_dc_option (void) {
  assert (fetch_int () == CODE_dc_option);
  int id = fetch_int ();
  int l1 = prefetch_strlen ();
  char *name = fetch_str (l1);
  int l2 = prefetch_strlen ();
  char *ip = fetch_str (l2);
  int port = fetch_int ();
  vlogprintf (E_DEBUG, "id = %d, name = %.*s ip = %.*s port = %d\n", id, l1, name, l2, ip, port);

  bl_do_dc_option (id, l1, name, l2, ip, port);
}

void tglu_work_update (struct connection *c, long long msg_id) {
  unsigned op = fetch_int ();
  switch (op) {
  case CODE_update_new_message:
    {
      struct tgl_message *M = tglf_fetch_alloc_message ();
      assert (M);
      tglu_fetch_pts ();
      bl_do_msg_update (M->id);
      break;
    };
  case CODE_update_message_i_d:
    {
      int id = fetch_int (); // id
      int new = fetch_long (); // random_id
      struct tgl_message *M = tgl_message_get (new);
      if (M) {
        bl_do_set_msg_id (M, id);
      }
    }
    break;
  case CODE_update_read_messages:
    {
      assert (fetch_int () == (int)CODE_vector);
      int n = fetch_int ();
      
      //int p = 0;
      int i;
      for (i = 0; i < n; i++) {
        int id = fetch_int ();
        struct tgl_message *M = tgl_message_get (id);
        if (M) {
          bl_do_set_unread (M, 0);
        }
      }
      tglu_fetch_pts ();
    }
    break;
  case CODE_update_user_typing:
    {
      tgl_peer_id_t id = TGL_MK_USER (fetch_int ());
      tgl_peer_t *U = tgl_peer_get (id);

      if (tgl_state.callback.type_notification && U) {
        tgl_state.callback.type_notification ((void *)U);
      }
    }
    break;
  case CODE_update_chat_user_typing:
    {
      tgl_peer_id_t chat_id = TGL_MK_CHAT (fetch_int ());
      tgl_peer_id_t id = TGL_MK_USER (fetch_int ());
      tgl_peer_t *C = tgl_peer_get (chat_id);
      tgl_peer_t *U = tgl_peer_get (id);
      
      if (U && C) {
        if (tgl_state.callback.type_in_chat_notification) {
          tgl_state.callback.type_in_chat_notification ((void *)U, (void *)C);
        }
      }
    }
    break;
  case CODE_update_user_status:
    {
      tgl_peer_id_t user_id = TGL_MK_USER (fetch_int ());
      tgl_peer_t *U = tgl_peer_get (user_id);
      if (U) {
        tglf_fetch_user_status (&U->user.status);

        if (tgl_state.callback.status_notification) {
          tgl_state.callback.status_notification ((void *)U);
        }
      } else {
        struct tgl_user_status t;
        tglf_fetch_user_status (&t);
      }
    }
    break;
  case CODE_update_user_name:
    {
      tgl_peer_id_t user_id = TGL_MK_USER (fetch_int ());
      tgl_peer_t *UC = tgl_peer_get (user_id);
      if (UC && (UC->flags & FLAG_CREATED)) {
        int l1 = prefetch_strlen ();
        char *f = fetch_str (l1);
        int l2 = prefetch_strlen ();
        char *l = fetch_str (l2);
        struct tgl_user *U = &UC->user;
        bl_do_user_set_real_name (U, f, l1, l, l2);
      } else {
        fetch_skip_str ();
        fetch_skip_str ();
      }
    }
    break;
  case CODE_update_user_photo:
    {
      tgl_peer_id_t user_id = TGL_MK_USER (fetch_int ());
      tgl_peer_t *UC = tgl_peer_get (user_id);
      tglu_fetch_date ();
      if (UC && (UC->flags & FLAG_CREATED)) {
        struct tgl_user *U = &UC->user;
        unsigned y = fetch_int ();
        long long photo_id;
        struct tgl_file_location big;
        struct tgl_file_location small;
        memset (&big, 0, sizeof (big));
        memset (&small, 0, sizeof (small));
        if (y == CODE_user_profile_photo_empty) {
          photo_id = 0;
          big.dc = -2;
          small.dc = -2;
        } else {
          assert (y == CODE_user_profile_photo);
          photo_id = fetch_long ();
          tglf_fetch_file_location (&small);
          tglf_fetch_file_location (&big);
        }
        bl_do_set_user_profile_photo (U, photo_id, &big, &small);
      } else {
        struct tgl_file_location t;
        unsigned y = fetch_int ();
        if (y == CODE_user_profile_photo_empty) {
        } else {
          assert (y == CODE_user_profile_photo);
          fetch_long (); // photo_id
          tglf_fetch_file_location (&t);
          tglf_fetch_file_location (&t);
        }
      }
      fetch_bool ();
    }
    break;
  case CODE_update_restore_messages:
    {
      assert (fetch_int () == CODE_vector);
      int n = fetch_int ();
      fetch_skip (n);
      tglu_fetch_pts ();
    }
    break;
  case CODE_update_delete_messages:
    {
      assert (fetch_int () == CODE_vector);
      int n = fetch_int ();
      fetch_skip (n);
      tglu_fetch_pts ();
    }
    break;
  case CODE_update_chat_participants:
    {
      unsigned x = fetch_int ();
      assert (x == CODE_chat_participants || x == CODE_chat_participants_forbidden);
      tgl_peer_id_t chat_id = TGL_MK_CHAT (fetch_int ());
      int n = 0;
      tgl_peer_t *C = tgl_peer_get (chat_id);
      if (C && (C->flags & FLAG_CREATED)) {
        if (x == CODE_chat_participants) {
          bl_do_chat_set_admin (&C->chat, fetch_int ());
          assert (fetch_int () == CODE_vector);
          n = fetch_int ();
          struct tgl_chat_user *users = talloc (12 * n);
          int i;
          for (i = 0; i < n; i++) {
            assert (fetch_int () == (int)CODE_chat_participant);
            users[i].user_id = fetch_int ();
            users[i].inviter_id = fetch_int ();
            users[i].date = fetch_int ();
          }
          int version = fetch_int (); 
          bl_do_chat_set_participants (&C->chat, version, n, users);
        }
      } else {
        if (x == CODE_chat_participants) {
          fetch_int (); // admin_id
          assert (fetch_int () == CODE_vector);
          n = fetch_int ();
          fetch_skip (n * 4);
          fetch_int (); // version
        }
      }
    }
    break;
  case CODE_update_contact_registered:
    {
      tgl_peer_id_t user_id = TGL_MK_USER (fetch_int ());
      tgl_peer_t *U = tgl_peer_get (user_id);
      fetch_int (); // date
      if (tgl_state.callback.user_registered && U) {
        tgl_state.callback.user_registered ((void *)U);
      }
    }
    break;
  case CODE_update_contact_link:
    {
      tgl_peer_id_t user_id = TGL_MK_USER (fetch_int ());
      tgl_peer_t *U = tgl_peer_get (user_id);
      unsigned t = fetch_int ();
      assert (t == CODE_contacts_my_link_empty || t == CODE_contacts_my_link_requested || t == CODE_contacts_my_link_contact);
      if (t == CODE_contacts_my_link_requested) {
        fetch_bool (); // has_phone
      }
      t = fetch_int ();
      assert (t == CODE_contacts_foreign_link_unknown || t == CODE_contacts_foreign_link_requested || t == CODE_contacts_foreign_link_mutual);
      if (t == CODE_contacts_foreign_link_requested) {
        fetch_bool (); // has_phone
      }
      if (U) {}
    }
    break;
  case CODE_update_activation:
    {
      tgl_peer_id_t user_id = TGL_MK_USER (fetch_int ());
      tgl_peer_t *U = tgl_peer_get (user_id);
     
      if (tgl_state.callback.user_activated && U) {
        tgl_state.callback.user_activated ((void *)U);
      }
    }
    break;
  case CODE_update_new_authorization:
    {
      fetch_long (); // auth_key_id
      fetch_int (); // date
      char *s = fetch_str_dup ();
      char *location = fetch_str_dup ();
      if (tgl_state.callback.new_authorization) {
        tgl_state.callback.new_authorization (s, location);
      }
      tfree_str (s);
      tfree_str (location);
    }
    break;
  case CODE_update_new_geo_chat_message:
    {
      struct tgl_message *M = tglf_fetch_alloc_geo_message ();
      assert (M);
      bl_do_msg_update (M->id);
    }
    break;
  case CODE_update_new_encrypted_message:
    {
      struct tgl_message *M = tglf_fetch_alloc_encrypted_message ();
      assert (M);
      tglu_fetch_qts ();
      bl_do_msg_update (M->id);
    }
    break;
  case CODE_update_encryption:
    {
      struct tgl_secret_chat *E = tglf_fetch_alloc_encrypted_chat ();
      vlogprintf (E_DEBUG, "Secret chat state = %d\n", E->state);
      if (E->state == sc_ok) {
        tgl_do_send_encr_chat_layer (E);
      }
      fetch_int (); // date
    }
    break;
  case CODE_update_encrypted_chat_typing:
    {
      tgl_peer_id_t id = TGL_MK_ENCR_CHAT (fetch_int ());
      tgl_peer_t *P = tgl_peer_get (id);
      
      if (P) {
        if (tgl_state.callback.type_in_secret_chat_notification) {
          tgl_state.callback.type_in_secret_chat_notification ((void *)P);
        }
      }
    }
    break;
  case CODE_update_encrypted_messages_read:
    {
      tgl_peer_id_t id = TGL_MK_ENCR_CHAT (fetch_int ()); // chat_id
      fetch_int (); // max_date
      fetch_int (); // date
      tgl_peer_t *P = tgl_peer_get (id);
      //int x = -1;
      if (P && P->last) {
        //x = 0;
        struct tgl_message *M = P->last;
        while (M && (!M->out || M->unread)) {
          if (M->out) {
            bl_do_set_unread (M, 0);
          }
          M = M->next;
        }
      }
    }
    break;
  case CODE_update_chat_participant_add:
    {
      tgl_peer_id_t chat_id = TGL_MK_CHAT (fetch_int ());
      tgl_peer_id_t user_id = TGL_MK_USER (fetch_int ());
      tgl_peer_id_t inviter_id = TGL_MK_USER (fetch_int ());
      int  version = fetch_int (); 
      
      tgl_peer_t *C = tgl_peer_get (chat_id);
      if (C && (C->flags & FLAG_CREATED)) {
        bl_do_chat_add_user (&C->chat, version, tgl_get_peer_id (user_id), tgl_get_peer_id (inviter_id), time (0));
      }
    }
    break;
  case CODE_update_chat_participant_delete:
    {
      tgl_peer_id_t chat_id = TGL_MK_CHAT (fetch_int ());
      tgl_peer_id_t user_id = TGL_MK_USER (fetch_int ());
      int version = fetch_int ();
      
      tgl_peer_t *C = tgl_peer_get (chat_id);
      if (C && (C->flags & FLAG_CREATED)) {
        bl_do_chat_del_user (&C->chat, version, tgl_get_peer_id (user_id));
      }
    }
    break;
  case CODE_update_dc_options:
    {
      assert (fetch_int () == CODE_vector);
      int n = fetch_int ();
      assert (n >= 0);
      int i;
      for (i = 0; i < n; i++) {
        fetch_dc_option ();
      }
    }
    break;
  case CODE_update_user_blocked:
    {
       int id = fetch_int ();
       int blocked = fetch_bool ();
       tgl_peer_t *P = tgl_peer_get (TGL_MK_USER (id));
       if (P && (P->flags & FLAG_CREATED)) {
         bl_do_user_set_blocked (&P->user, blocked);
       }
    }
    break;
  case CODE_update_notify_settings:
    {
       assert (skip_type_any (TYPE_TO_PARAM (notify_peer)) >= 0);
       assert (skip_type_any (TYPE_TO_PARAM (peer_notify_settings)) >= 0);
    }
    break;
  default:
    vlogprintf (E_ERROR, "Unknown update type %08x\n", op);
    ;
  }
}

void tglu_work_update_short (struct connection *c, long long msg_id) {
  int *save = in_ptr;
  assert (!skip_type_any (TYPE_TO_PARAM (updates)));
  int *save_end = in_ptr;
  in_ptr = save;

  assert (fetch_int () == CODE_update_short);
  tglu_work_update (c, msg_id);
  tglu_fetch_date ();
  
  assert (save_end == in_ptr);
}
  
static int do_skip_seq (int seq) {
  if (tgl_state.seq) {
    if (seq <= tgl_state.seq) {
      vlogprintf (E_NOTICE, "Duplicate message with seq=%d\n", seq);
      return -1;
    }
    if (seq > tgl_state.seq + 1) {
      vlogprintf (E_NOTICE, "Hole in seq (seq = %d, cur_seq = %d)\n", seq, tgl_state.seq);
      //vlogprintf (E_NOTICE, "lock_diff = %s\n", (tgl_state.locks & TGL_LOCK_DIFF) ? "true" : "false");
      tgl_do_get_difference (0, 0, 0);
      return -1;
    }
    if (tgl_state.locks & TGL_LOCK_DIFF) {
      vlogprintf (E_DEBUG, "Update during get_difference. seq = %d\n", seq);
      return -1;
    }
    vlogprintf (E_DEBUG, "Ok update. seq = %d\n", seq);
    return 0;
  } else {
    return -1;
  }
}

void tglu_work_updates (struct connection *c, long long msg_id) {
  int *save = in_ptr;
  assert (!skip_type_any (TYPE_TO_PARAM (updates)));
  if (do_skip_seq (*(in_ptr - 1)) < 0) {
    return;
  }
  int *save_end = in_ptr;
  in_ptr = save;
  assert (fetch_int () == CODE_updates);
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  int i;
  for (i = 0; i < n; i++) {
    tglu_work_update (c, msg_id);
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_user ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    tglf_fetch_alloc_chat ();
  }
  bl_do_set_date (fetch_int ());
  //bl_do_set_seq (fetch_int ());
  int seq = fetch_int ();
  assert (seq == tgl_state.seq + 1);
  bl_do_set_seq (seq);
  assert (save_end == in_ptr);
}

void tglu_work_update_short_message (struct connection *c, long long msg_id) {
  int *save = in_ptr;
  assert (!skip_type_any (TYPE_TO_PARAM (updates)));  
  if (do_skip_seq (*(in_ptr - 1)) < 0) {
    return;
  }
  int *save_end = in_ptr;
  in_ptr = save;

  assert (fetch_int () == (int)CODE_update_short_message);
  struct tgl_message *M = tglf_fetch_alloc_message_short ();  
  assert (M);

  assert (save_end == in_ptr);
  if (!(tgl_state.locks & TGL_LOCK_DIFF)) {
    bl_do_msg_seq_update (M->id);
  }
}

void tglu_work_update_short_chat_message (struct connection *c, long long msg_id) {
  int *save = in_ptr;
  assert (!skip_type_any (TYPE_TO_PARAM (updates)));  
  if (do_skip_seq (*(in_ptr - 1)) < 0) {
    return;
  }
  int *save_end = in_ptr;
  in_ptr = save;

  assert (fetch_int () == CODE_update_short_chat_message);
  struct tgl_message *M = tglf_fetch_alloc_message_short_chat ();  
  assert (M);
  assert (save_end == in_ptr);

  if (!(tgl_state.locks & TGL_LOCK_DIFF)) {
    bl_do_msg_seq_update (M->id);
  }
}

void tglu_work_updates_to_long (struct connection *c, long long msg_id) {
  assert (fetch_int () == (int)CODE_updates_too_long);
  vlogprintf (E_NOTICE, "updates too long... Getting difference\n");
  tgl_do_get_difference (0, 0, 0);
}
