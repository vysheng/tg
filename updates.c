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
  if (p != tgl_state.pts + 1) {
    if (tgl_state.pts) {
      //vlogprintf (E_NOTICE, "Hole in pts p = %d, pts = %d\n", p, tgl_state.pts);

      // get difference should be here
      tgl_state.pts = p;
    } else {
      tgl_state.pts = p;
    }
  } else {
    tgl_state.pts ++;
  }
  bl_do_set_pts (tgl_state.pts);
}

void tglu_fetch_qts (void) {
  int p = fetch_int ();
  if (p <= tgl_state.qts) { return; }
  if (p != tgl_state.qts + 1) {
    if (tgl_state.qts) {
      //logprintf ("Hole in qts\n");
      // get difference should be here
      tgl_state.qts = p;
    } else {
      tgl_state.qts = p;
    }
  } else {
    tgl_state.qts ++;
  }
  bl_do_set_qts (tgl_state.qts);
}

void tglu_fetch_date (void) {
  int p = fetch_int ();
  if (p > tgl_state.date) {
    tgl_state.date = p;
    bl_do_set_date (tgl_state.date);
  }
}

void tglu_fetch_seq (void) {
  int x = fetch_int ();
  if (x > tgl_state.seq + 1) {
    vlogprintf (E_NOTICE, "Hole in seq: seq = %d, x = %d\n", tgl_state.seq, x);
    //tgl_do_get_difference ();
    //seq = x;
  } else if (x == tgl_state.seq + 1) {
    tgl_state.seq = x;
    bl_do_set_seq (tgl_state.seq);
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

      //if (tgl_state.callback.new_msg) {
      //  tgl_state.callback.new_msg (M);
      //}
      //unread_messages ++;
      //print_message (M);
      //update_prompt ();
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
      /*if (log_level >= 1) {
        print_start ();
        push_color (COLOR_YELLOW);
        print_date (time (0));
        printf (" %d messages marked as read\n", n);
        pop_color ();
        print_end ();
      }*/
    }
    break;
  case CODE_update_user_typing:
    {
      tgl_peer_id_t id = TGL_MK_USER (fetch_int ());
      tgl_peer_t *U = tgl_peer_get (id);

      if (tgl_state.callback.type_notification && U) {
        tgl_state.callback.type_notification ((void *)U);
      }
      /*if (log_level >= 2) {
        print_start ();
        push_color (COLOR_YELLOW);
        print_date (time (0));
        printf (" User ");
        print_user_name (id, U);
        printf (" is typing....\n");
        pop_color ();
        print_end ();
      }*/
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
      /*if (log_level >= 2) {
        print_start ();
        push_color (COLOR_YELLOW);
        print_date (time (0));
        printf (" User ");
        print_user_name (id, U);
        printf (" is typing in chat ");
        print_chat_name (chat_id, C);
        printf ("....\n");
        pop_color ();
        print_end ();
      }*/
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
        /*if (log_level >= 3) {
          print_start ();
          push_color (COLOR_YELLOW);
          print_date (time (0));
          printf (" User ");
          print_user_name (user_id, U);
          printf (" is now ");
          printf ("%s\n", (U->user.status.online > 0) ? "online" : "offline");
          pop_color ();
          print_end ();
        }*/
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
        /*print_start ();
        push_color (COLOR_YELLOW);
        print_date (time (0));
        printf (" User ");
        print_user_name (user_id, UC);
        printf (" changed name to ");
        print_user_name (user_id, UC);
        printf ("\n");
        pop_color ();
        print_end ();*/
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
        
        /*print_start ();
        push_color (COLOR_YELLOW);
        print_date (time (0));
        printf (" User ");
        print_user_name (user_id, UC);
        printf (" updated profile photo\n");
        pop_color ();
        print_end ();*/
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
      /*print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" Restored %d messages\n", n);
      pop_color ();
      print_end ();*/
      fetch_skip (n);
      tglu_fetch_pts ();
    }
    break;
  case CODE_update_delete_messages:
    {
      assert (fetch_int () == CODE_vector);
      int n = fetch_int ();
      /*print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" Deleted %d messages\n", n);
      pop_color ();
      print_end ();*/
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
      /*print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" Chat ");
      print_chat_name (chat_id, C);
      if (x == CODE_chat_participants) {
        printf (" changed list: now %d members\n", n);
      } else {
        printf (" changed list, but we are forbidden to know about it (Why this update even was sent to us?\n");
      }
      pop_color ();
      print_end ();*/
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
      /*print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" User ");
      print_user_name (user_id, U);
      printf (" registered\n");
      pop_color ();
      print_end ();*/
    }
    break;
  case CODE_update_contact_link:
    {
      tgl_peer_id_t user_id = TGL_MK_USER (fetch_int ());
      tgl_peer_t *U = tgl_peer_get (user_id);
      /*print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" Updated link with user ");
      print_user_name (user_id, U);
      printf ("\n");
      pop_color ();
      print_end ();*/
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
      /*print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" User ");
      print_user_name (user_id, U);
      printf (" activated\n");
      pop_color ();
      print_end ();*/
    }
    break;
  case CODE_update_new_authorization:
    {
      fetch_long (); // auth_key_id
      fetch_int (); // date
      char *s = fetch_str_dup ();
      char *location = fetch_str_dup ();
      /*print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" New autorization: device='%s' location='%s'\n",
        s, location);
      pop_color ();
      print_end ();*/
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
      //if (tgl_state.callback.new_msg) {
      //  tgl_state.callback.new_msg (M);
      //}
      //unread_messages ++;
      //print_message (M);
      //update_prompt ();
    }
    break;
  case CODE_update_new_encrypted_message:
    {
      struct tgl_message *M = tglf_fetch_alloc_encrypted_message ();
      assert (M);
      //unread_messages ++;
      //print_message (M);
      //update_prompt ();
      tglu_fetch_qts ();
      //if (tgl_state.callback.new_msg) {
      //  tgl_state.callback.new_msg (M);
      //}
    }
    break;
  case CODE_update_encryption:
    {
      struct tgl_secret_chat *E = tglf_fetch_alloc_encrypted_chat ();
      vlogprintf (E_DEBUG, "Secret chat state = %d\n", E->state);
      /*print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      switch (E->state) {
      case sc_none:
        break;
      case sc_waiting:
        printf (" Encrypted chat ");
        print_encr_chat_name (E->id, (void *)E);
        printf (" is now in wait state\n");
        break;
      case sc_request:
        printf (" Encrypted chat ");
        print_encr_chat_name (E->id, (void *)E);
        printf (" is now in request state. Sending request ok\n");
        break;
      case sc_ok:
        printf (" Encrypted chat ");
        print_encr_chat_name (E->id, (void *)E);
        printf (" is now in ok state\n");
        break;
      case sc_deleted:
        printf (" Encrypted chat ");
        print_encr_chat_name (E->id, (void *)E);
        printf (" is now in deleted state\n");
        break;
      }
      pop_color ();
      print_end ();*/

      if (E->state == sc_request) {
        if (tgl_state.callback.secret_chat_request) {
          tgl_state.callback.secret_chat_request (E);
        }
      } else if (E->state == sc_ok) {
        if (tgl_state.callback.secret_chat_established) {
          tgl_state.callback.secret_chat_established (E);
        }
      } else if (E->state == sc_deleted) {
        if (tgl_state.callback.secret_chat_deleted) {
          tgl_state.callback.secret_chat_deleted (E);
        }
      }
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
      /*print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      if (P) {
        printf (" User ");
        tgl_peer_id_t user_id = TGL_MK_USER (P->encr_chat.user_id);
        print_user_name (user_id, tgl_peer_get (user_id));
        printf (" typing in secret chat ");
        print_encr_chat_name (id, P);
        printf ("\n");
      } else {
        printf (" Some user is typing in unknown secret chat\n");
      }
      pop_color ();
      print_end ();*/
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
      /*if (log_level >= 1) {
        print_start ();
        push_color (COLOR_YELLOW);
        print_date (time (0));
        printf (" Encrypted chat ");
        print_encr_chat_name_full (id, tgl_peer_get (id));
        printf (": %d messages marked read \n", x);
        pop_color ();
        print_end ();
      }*/
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

      /*print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" Chat ");
      print_chat_name (chat_id, tgl_peer_get (chat_id));
      printf (": user ");
      print_user_name (user_id, tgl_peer_get (user_id));
      printf (" added by user ");
      print_user_name (inviter_id, tgl_peer_get (inviter_id));
      printf ("\n");
      pop_color ();
      print_end ();*/
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

      /*print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" Chat ");
      print_chat_name (chat_id, tgl_peer_get (chat_id));
      printf (": user ");
      print_user_name (user_id, tgl_peer_get (user_id));
      printf (" deleted\n");
      pop_color ();
      print_end ();*/
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
      tgl_do_get_difference (0, 0, 0);
      return -1;
    }
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
  fetch_int ();
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
  /*unread_messages ++;
  print_message (M);
  update_prompt ();
  if (M->date > last_date) {
    last_date = M->date;
  }*/

  assert (save_end == in_ptr);
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
  /*unread_messages ++;
  print_message (M);
  update_prompt ();
  if (M->date > last_date) {
    last_date = M->date;
  }*/
  assert (save_end == in_ptr);

  bl_do_msg_seq_update (M->id);
}

void tglu_work_updates_to_long (struct connection *c, long long msg_id) {
  assert (fetch_int () == (int)CODE_updates_too_long);
  vlogprintf (E_NOTICE, "updates to long... Getting difference\n");
  tgl_do_get_difference (0, 0, 0);
}
