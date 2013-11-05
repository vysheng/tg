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
#include <assert.h>
#include <string.h>
#include "structures.h"
#include "mtproto-common.h"
#include "telegram.h"
#include "tree.h"
#include "loop.h"
#include <openssl/rand.h>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include "queries.h"

#define sha1 SHA1

int verbosity;
peer_t *Peers[MAX_USER_NUM];

int peer_num;
int encr_chats_allocated;
int geo_chats_allocated;

void fetch_file_location (struct file_location *loc) {
  int x = fetch_int ();
  if (x == CODE_file_location_unavailable) {
    loc->dc = -1;
    loc->volume = fetch_long ();
    loc->local_id = fetch_int ();
    loc->secret = fetch_long ();
  } else {
    assert (x == CODE_file_location);
    loc->dc = fetch_int ();;
    loc->volume = fetch_long ();
    loc->local_id = fetch_int ();
    loc->secret = fetch_long ();
  }
}

void fetch_user_status (struct user_status *S) {
  int x = fetch_int ();
  switch (x) {
  case CODE_user_status_empty:
    S->online = 0;
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
}

int our_id;

char *create_print_name (peer_id_t id, const char *a1, const char *a2, const char *a3, const char *a4) {
  const char *d[4];
  d[0] = a1; d[1] = a2; d[2] = a3; d[3] = a4;
  static char buf[10000];
  int i;
  int p = 0;
  for (i = 0; i < 4; i++) {
    if (d[i] && strlen (d[i])) {
      p += snprintf (buf + p, 9999 - p, "%s%s", p ? "_" : "", d[i]);
      assert (p < 9990);
    }
  }
  char *s = buf;
  while (*s) {
    if (*s == ' ') { *s = '_'; }
    s++;
  }
  s = buf;
  int cc = 0;
  while (1) {
    int ok = 1;
    int i;
    for (i = 0; i < peer_num; i++) {
      if (cmp_peer_id (Peers[i]->id, id) && Peers[i]->print_name && !strcmp (Peers[i]->print_name, s)) {
        ok = 0;
        break;
      }
    }
    if (ok) {
      break;
    }
    cc ++;
    assert (cc <= 99);
    if (cc == 1) {
      int l = strlen (s);
      s[l + 2] = 0;
      s[l] = '#';
      s[l + 1] = '1';
    } else if (cc == 10) {
      int l = strlen (s);
      s[l + 1] = 0;
      s[l] = '0';
      s[l - 1] = '1';
    } else {
      int l = strlen (s);
      s[l - 1] ++;
      int cc = l - 1;
      while (s[cc] > '9') {
        s[cc] = '0';
        s[cc - 1] ++;
        cc --;
      }
    }
  }
  return strdup (s);
}

void fetch_user (struct user *U) {
  unsigned x = fetch_int ();
  assert (x == CODE_user_empty || x == CODE_user_self || x == CODE_user_contact ||  x == CODE_user_request || x == CODE_user_foreign || x == CODE_user_deleted);
  U->id = MK_USER (fetch_int ());
  U->flags &= ~(FLAG_EMPTY | FLAG_DELETED | FLAG_USER_SELF | FLAG_USER_FOREIGN | FLAG_USER_CONTACT);
  if (x == CODE_user_empty) {
    U->flags |= FLAG_EMPTY;
    return;
  }
  if (x == CODE_user_self) {
    assert (!our_id || (our_id == get_peer_id (U->id)));
    if (!our_id) {
      our_id = get_peer_id (U->id);
      write_auth_file ();
    }
  }
  if (U->first_name) { free (U->first_name); }
  if (U->last_name) { free (U->last_name); }
  if (U->print_name) { free (U->print_name); }
  U->first_name = fetch_str_dup ();
  U->last_name = fetch_str_dup ();

  U->print_name = create_print_name (U->id, U->first_name, U->last_name, 0, 0);
  if (x == CODE_user_deleted) {
    U->flags |= FLAG_DELETED;
    return;
  }
  if (x == CODE_user_self) {
    U->flags |= FLAG_USER_SELF;
  } else {
    U->access_hash = fetch_long ();
  }
  if (x == CODE_user_foreign) {
    U->flags |= FLAG_USER_FOREIGN;
    U->phone = 0;
  } else {
    if (U->phone) { free (U->phone); }
    U->phone = fetch_str_dup ();
  }
  //logprintf ("name = %s, surname = %s, phone = %s\n", U->first_name, U->last_name, U->phone);
  unsigned y = fetch_int ();
  //fprintf (stderr, "y = 0x%08x\n", y);
  if (y == CODE_user_profile_photo_empty) {
    U->photo_small.dc = -2;
    U->photo_big.dc = -2;
  } else {
    assert (y == CODE_user_profile_photo || y == 0x990d1493);
    if (y == CODE_user_profile_photo) {
      fetch_long ();
    }
    fetch_file_location (&U->photo_small);
    fetch_file_location (&U->photo_big);
  }
  fetch_user_status (&U->status);
  if (x == CODE_user_self) {
    fetch_bool ();
  }
  if (x == CODE_user_contact) {
    U->flags |= FLAG_USER_CONTACT;
  }
}

void fetch_encrypted_chat (struct secret_chat *U) {
  unsigned x = fetch_int ();
  assert (x == CODE_encrypted_chat_empty || x == CODE_encrypted_chat_waiting || x == CODE_encrypted_chat_requested ||  x == CODE_encrypted_chat || x == CODE_encrypted_chat_discarded);
  U->id = MK_ENCR_CHAT (fetch_int ());
  U->flags &= ~(FLAG_EMPTY | FLAG_DELETED);
  enum secret_chat_state old_state = U->state;
  if (x == CODE_encrypted_chat_empty) {
    U->state = sc_none;
    U->flags |= FLAG_EMPTY;
    if (U->state != old_state) {
      write_secret_chat_file ();
    }
    return;
  }
  if (x == CODE_encrypted_chat_discarded) {
    U->state = sc_deleted;
    U->flags |= FLAG_DELETED;
    if (U->state != old_state) {
      write_secret_chat_file ();
    }
    return;
  }
  U->access_hash = fetch_long ();
  U->date = fetch_int ();
  U->admin_id = fetch_int ();
  U->user_id = fetch_int () + U->admin_id - our_id;
  if (U->print_name) { free (U->print_name); }
  
  peer_t *P = user_chat_get (MK_USER (U->user_id));
  if (P) {
    U->print_name = create_print_name (U->id, "!", P->user.first_name, P->user.last_name, 0);
  } else {
    static char buf[100];
    sprintf (buf, "user#%d", U->user_id);
    U->print_name = create_print_name (U->id, "!", buf, 0, 0);
  }

  if (x == CODE_encrypted_chat_waiting) {
    U->state = sc_waiting;
  } else if (x == CODE_encrypted_chat_requested) {
    U->state = sc_request;
    if (!U->g_key) {
      U->g_key = malloc (256);      
    }
    memset (U->g_key, 0, 256);
    if (!U->nonce) {
      U->nonce = malloc (256);
    }
    memset (U->nonce, 0, 256);
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    if (l < 256) {
      memcpy (U->g_key + 256 - l, s, l);
    } else {
      memcpy (U->g_key, s +  (l - 256), 256);
    }
    l = prefetch_strlen ();
    s = fetch_str (l);
    if (l < 256) {
      memcpy (U->nonce + 256 - l, s, l);
    } else {
      memcpy (U->nonce, s +  (l - 256), 256);
    }
  } else {
    U->state = sc_ok;
    if (!U->g_key) {
      U->g_key = malloc (256);
    }
    memset (U->g_key, 0, 256);
    if (!U->nonce) {
      U->nonce = malloc (256);
    }
    memset (U->nonce, 0, 256);
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    if (l < 256) {
      memcpy (U->g_key + 256 - l, s, l);
    } else {
      memcpy (U->g_key, s +  (l - 256), 256);
    }
    l = prefetch_strlen ();
    s = fetch_str (l);
    if (l < 256) {
      memcpy (U->nonce + 256 - l, s, l);
    } else {
      memcpy (U->nonce, s +  (l - 256), 256);
    }
    if (!U->key_fingerprint) {
      U->key_fingerprint = fetch_long ();
    } else {
      assert (U->key_fingerprint == fetch_long ());
    }
    if (old_state == sc_waiting) {
      do_create_keys_end (U);
    }
  }
  if (U->state != old_state) {
    write_secret_chat_file ();
  }
}

void fetch_notify_settings (void);
void fetch_user_full (struct user *U) {
  assert (fetch_int () == CODE_user_full);
  fetch_alloc_user ();
  unsigned x;
  assert (fetch_int () == (int)CODE_contacts_link);
  x = fetch_int ();
  assert (x == CODE_contacts_my_link_empty || x == CODE_contacts_my_link_requested || x == CODE_contacts_my_link_contact);
  U->flags &= ~(FLAG_USER_IN_CONTACT | FLAG_USER_OUT_CONTACT);
  if (x == CODE_contacts_my_link_contact) {
    U->flags |= FLAG_USER_IN_CONTACT; 
  }
  if (x == CODE_contacts_my_link_requested) {
    fetch_bool ();
  }
  x = fetch_int ();
  assert (x == CODE_contacts_foreign_link_unknown || x == CODE_contacts_foreign_link_requested || x == CODE_contacts_foreign_link_mutual);
  U->flags &= ~(FLAG_USER_IN_CONTACT | FLAG_USER_OUT_CONTACT);
  if (x == CODE_contacts_foreign_link_mutual) {
    U->flags |= FLAG_USER_IN_CONTACT | FLAG_USER_OUT_CONTACT; 
  }
  if (x == CODE_contacts_foreign_link_requested) {
    U->flags |= FLAG_USER_OUT_CONTACT;
    fetch_bool ();
  }
  fetch_alloc_user ();
  if (U->flags & FLAG_HAS_PHOTO) {
    free_photo (&U->photo);
  }
  fetch_photo (&U->photo);
  fetch_notify_settings ();
  U->blocked = fetch_int ();
  if (U->real_first_name) { free (U->real_first_name); }
  if (U->real_last_name) { free (U->real_last_name); }
  U->real_first_name = fetch_str_dup ();
  U->real_last_name = fetch_str_dup ();
}

void fetch_chat (struct chat *C) {
  unsigned x = fetch_int ();
  assert (x == CODE_chat_empty || x == CODE_chat || x == CODE_chat_forbidden);
  C->id = MK_CHAT (fetch_int ());
  C->flags &= ~(FLAG_EMPTY | FLAG_DELETED | FLAG_FORBIDDEN | FLAG_CHAT_IN_CHAT);
  if (x == CODE_chat_empty) {
    C->flags |= FLAG_EMPTY;
    return;
  }
  if (x == CODE_chat_forbidden) {
    C->flags |= FLAG_FORBIDDEN;
  }
  if (C->title) { free (C->title); }
  if (C->print_title) { free (C->print_title); }
  C->title = fetch_str_dup ();
  C->print_title = create_print_name (C->id, C->title, 0, 0, 0);
  if (x == CODE_chat) {
    unsigned y = fetch_int ();
    if (y == CODE_chat_photo_empty) {
      C->photo_small.dc = -2;
      C->photo_big.dc = -2;
    } else {
      assert (y == CODE_chat_photo);
      fetch_file_location (&C->photo_small);
      fetch_file_location (&C->photo_big);
    }
    C->users_num = fetch_int ();
    C->date = fetch_int ();
    if (fetch_int () == (int)CODE_bool_true) {
      C->flags |= FLAG_CHAT_IN_CHAT;
    }
    C->version = fetch_int ();
  } else {
    C->photo_small.dc = -2;
    C->photo_big.dc = -2;
    C->users_num = -1;
    C->date = fetch_int ();
    C->version = -1;
  }
}

void fetch_notify_settings (void) {
  unsigned x = fetch_int ();
  assert (x == CODE_peer_notify_settings || x == CODE_peer_notify_settings_empty || x == CODE_peer_notify_settings_old);
  if (x == CODE_peer_notify_settings_old) {
    fetch_int (); // mute_until
    int l = prefetch_strlen ();
    fetch_str (l);
    fetch_bool (); // show_previews
    fetch_int (); // peer notify events
  }
  if (x == CODE_peer_notify_settings) {
    fetch_int (); // mute_until
    int l = prefetch_strlen ();
    fetch_str (l);
    fetch_bool (); // show_previews
    fetch_int (); // events_mask
  }
}

void fetch_chat_full (struct chat *C) {
  unsigned x = fetch_int ();
  assert (x == CODE_messages_chat_full);
  assert (fetch_int () == CODE_chat_full); 
  C->id = MK_CHAT (fetch_int ());
  C->flags &= ~(FLAG_EMPTY | FLAG_DELETED | FLAG_FORBIDDEN | FLAG_CHAT_IN_CHAT);
  x = fetch_int ();
  if (x == CODE_chat_participants) {
    assert (fetch_int () == get_peer_id (C->id));
    C->admin_id =  fetch_int ();
    assert (fetch_int () == CODE_vector);
    if (C->users) {
      free (C->users);
    }
    C->users_num = fetch_int ();
    C->users = malloc (sizeof (struct chat_user) * C->users_num);
    int i;
    for (i = 0; i < C->users_num; i++) {
      assert (fetch_int () == (int)CODE_chat_participant);
      C->users[i].user_id = fetch_int ();
      C->users[i].inviter_id = fetch_int ();
      C->users[i].date = fetch_int ();
    }
    C->version = fetch_int ();
  } else {
    C->flags |= FLAG_FORBIDDEN;
    assert (x == CODE_chat_participants_forbidden);
  }
  if (C->flags & FLAG_HAS_PHOTO) {
    free_photo (&C->photo);
  }
  fetch_photo (&C->photo);
  C->flags |= FLAG_HAS_PHOTO;
  fetch_notify_settings ();

  int n, i;
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_chat ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_user ();
  }
}

void fetch_photo_size (struct photo_size *S) {
  memset (S, 0, sizeof (*S));
  unsigned x = fetch_int ();
  assert (x == CODE_photo_size || x == CODE_photo_cached_size || x == CODE_photo_size_empty);
  S->type = fetch_str_dup ();
  if (x != CODE_photo_size_empty) {
    fetch_file_location (&S->loc);
    S->w = fetch_int ();
    S->h = fetch_int ();
    if (x == CODE_photo_size) {
      S->size = fetch_int ();
    } else {
      S->size = prefetch_strlen ();
      S->data = malloc (S->size);
      memcpy (S->data, fetch_str (S->size), S->size);
    }
  }
}

void fetch_geo (struct geo *G) {
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

void fetch_photo (struct photo *P) {
  memset (P, 0, sizeof (*P));
  unsigned x = fetch_int ();
  assert (x == CODE_photo_empty || x == CODE_photo);
  P->id = fetch_long ();
  if (x == CODE_photo_empty) { return; }
  P->access_hash = fetch_long ();
  P->user_id = fetch_int ();
  P->date = fetch_int ();
  P->caption = fetch_str_dup ();
  fetch_geo (&P->geo);
  assert (fetch_int () == CODE_vector);
  P->sizes_num = fetch_int ();
  P->sizes = malloc (sizeof (struct photo_size) * P->sizes_num);
  int i;
  for (i = 0; i < P->sizes_num; i++) {
    fetch_photo_size (&P->sizes[i]);
  }
}

void fetch_video (struct video *V) {
  memset (V, 0, sizeof (*V));
  unsigned x = fetch_int ();
  V->id = fetch_long ();
  if (x == CODE_video_empty) { return; }
  V->access_hash = fetch_long ();
  V->user_id = fetch_int ();
  V->date = fetch_int ();
  V->caption = fetch_str_dup ();
  V->duration = fetch_int ();
  V->size = fetch_int ();
  fetch_photo_size (&V->thumb);
  V->dc_id = fetch_int ();
  V->w = fetch_int ();
  V->h = fetch_int ();
}

void fetch_message_action (struct message_action *M) {
  memset (M, 0, sizeof (*M));
  unsigned x = fetch_int ();
  M->type = x;
  switch (x) {
  case CODE_message_action_empty:
    break;
  case CODE_message_action_chat_create:
    M->title = fetch_str_dup ();
    assert (fetch_int () == (int)CODE_vector);
    M->user_num = fetch_int ();
    M->users = malloc (M->user_num * 4);
    fetch_ints (M->users, M->user_num);
    break;
  case CODE_message_action_chat_edit_title:
    M->new_title = fetch_str_dup ();
    break;
  case CODE_message_action_chat_edit_photo:
    fetch_photo (&M->photo);
    break;
  case CODE_message_action_chat_delete_photo:
    break;
  case CODE_message_action_chat_add_user:
    M->user = fetch_int ();
    break;
  case CODE_message_action_chat_delete_user:
    M->user = fetch_int ();
    break;
  default:
    assert (0);
  }
}

void fetch_message_short (struct message *M) {
  memset (M, 0, sizeof (*M));
  M->id = fetch_int ();
  M->to_id = MK_USER (our_id);
  M->from_id = MK_USER (fetch_int ());
  M->message = fetch_str_dup ();
  fetch_pts ();
  M->date = fetch_int ();
  fetch_seq ();
  M->media.type = CODE_message_media_empty;
  M->unread = 1;
}

void fetch_message_short_chat (struct message *M) {
  memset (M, 0, sizeof (*M));
  M->id = fetch_int ();
  M->from_id = MK_USER (fetch_int ());
  M->to_id = MK_CHAT (fetch_int ());
  M->message = fetch_str_dup ();
  fetch_pts ();
  M->date = fetch_int ();
  fetch_seq ();
  M->media.type = CODE_message_media_empty;
  M->unread = 1;
}


void fetch_message_media (struct message_media *M) {
  memset (M, 0, sizeof (*M));
  M->type = fetch_int ();
  switch (M->type) {
  case CODE_message_media_empty:
    break;
  case CODE_message_media_photo:
    fetch_photo (&M->photo);
    break;
  case CODE_message_media_video:
    fetch_video (&M->video);
    break;
  case CODE_message_media_geo:
    fetch_geo (&M->geo);
    break;
  case CODE_message_media_contact:
    M->phone = fetch_str_dup ();
    M->first_name = fetch_str_dup ();
    M->last_name = fetch_str_dup ();
    M->user_id = fetch_int ();
    break;
  case CODE_message_media_unsupported:
    M->data = fetch_str_dup ();
    break;
  default:
    logprintf ("type = 0x%08x\n", M->type);
    assert (0);
  }
}

void fetch_message_media_encrypted (struct message_media *M) {
  memset (M, 0, sizeof (*M));
  unsigned x = fetch_int ();
  int l;
  switch (x) {
  case CODE_decrypted_message_media_empty:
    M->type = CODE_message_media_empty;
    break;
  case CODE_decrypted_message_media_photo:
    M->type = x;
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    fetch_int (); // thumb_w
    fetch_int (); // thumb_h
    M->encr_photo.w = fetch_int ();
    M->encr_photo.h = fetch_int ();
    M->encr_photo.size = fetch_int ();
    
    l = prefetch_strlen  ();
    assert (l > 0);
    M->encr_photo.key = malloc (32);
    memset (M->encr_photo.key, 0, 32);
    if (l <= 32) {
      memcpy (M->encr_photo.key + (32 - l), fetch_str (l), l);
    } else {
      memcpy (M->encr_photo.key, fetch_str (l) + (l - 32), 32);
    }
    M->encr_photo.iv = malloc (32);
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
    M->type = x;
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    fetch_int (); // thumb_w
    fetch_int (); // thumb_h
    M->encr_video.w = fetch_int ();
    M->encr_video.h = fetch_int ();
    M->encr_video.size = fetch_int ();
    M->encr_video.duration = fetch_int ();
    
    l = prefetch_strlen  ();
    assert (l > 0);
    M->encr_video.key = malloc (32);
    memset (M->encr_photo.key, 0, 32);
    if (l <= 32) {
      memcpy (M->encr_video.key + (32 - l), fetch_str (l), l);
    } else {
      memcpy (M->encr_video.key, fetch_str (l) + (l - 32), 32);
    }
    M->encr_video.iv = malloc (32);
    l = prefetch_strlen  ();
    assert (l > 0);
    memset (M->encr_video.iv, 0, 32);
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
    M->encr_file.key = malloc (l);
    memcpy (M->encr_file.key, fetch_str (l), l);
    
    l = fetch_int ();
    assert (l > 0);
    M->encr_file.iv = malloc (l);
    memcpy (M->encr_file.iv, fetch_str (l), l);
    break;
  */  
  case CODE_decrypted_message_media_geo_point:
    M->geo.longitude = fetch_double ();
    M->geo.latitude = fetch_double ();
    M->type = CODE_message_media_geo;
    break;
  case CODE_decrypted_message_media_contact:
    M->type = CODE_message_media_contact;
    M->phone = fetch_str_dup ();
    M->first_name = fetch_str_dup ();
    M->last_name = fetch_str_dup ();
    M->user_id = fetch_int ();
    break;
  default:
    logprintf ("type = 0x%08x\n", M->type);
    assert (0);
  }
}

peer_id_t fetch_peer_id (void) {
  unsigned x =fetch_int ();
  if (x == CODE_peer_user) {
    return MK_USER (fetch_int ());
  } else {
    assert (CODE_peer_chat);
    return MK_CHAT (fetch_int ());
  }
}

void fetch_message (struct message *M) {
  memset (M, 0, sizeof (*M));
  unsigned x = fetch_int ();
  assert (x == CODE_message_empty || x == CODE_message || x == CODE_message_forwarded || x == CODE_message_service);
  M->id = fetch_int ();
  if (x == CODE_message_empty) {
    M->flags |= 1;
    return;
  }
  if (x == CODE_message_forwarded) {
    M->fwd_from_id = MK_USER (fetch_int ());
    M->fwd_date = fetch_int ();
  }
  M->from_id = MK_USER (fetch_int ());
  M->to_id = fetch_peer_id ();
  M->out = fetch_bool ();
  M->unread = fetch_bool ();
  M->date = fetch_int ();
  if (x == CODE_message_service) {
    M->service = 1;
    fetch_message_action (&M->action);
  } else {
    M->message = fetch_str_dup ();
    fetch_message_media (&M->media);
  }
}

void fetch_geo_message (struct message *M) {
  memset (M, 0, sizeof (*M));
  unsigned x = fetch_int ();
  assert (x == CODE_geo_chat_message_empty || x == CODE_geo_chat_message || x == CODE_geo_chat_message_service);
  M->to_id = MK_GEO_CHAT (fetch_int ());
  M->id = fetch_int ();
  if (x == CODE_geo_chat_message_empty) {
    M->flags |= 1;
    return;
  }
  M->from_id = MK_USER (fetch_int ());
  M->date = fetch_int ();
  if (x == CODE_geo_chat_message_service) {
    M->service = 1;
    fetch_message_action (&M->action);
  } else {
    M->message = fetch_str_dup ();
    fetch_message_media (&M->media);
  }
}

int *decr_ptr;
int *decr_end;

int decrypt_encrypted_message (struct secret_chat *E) {
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

  int x = *(decr_ptr);
  if (x < 0 || (x & 3)) {
    return -1;
  }
  assert (x >= 0 && !(x & 3));
  sha1 ((void *)decr_ptr, 4 + x, sha1a_buffer);

  if (memcmp (sha1a_buffer + 4, msg_key, 16)) {
    logprintf ("Sha1 mismatch\n");
    return -1;
  }
  return 0;
}

void fetch_encrypted_message (struct message *M) {
  memset (M, 0, sizeof (*M));
  unsigned x = fetch_int ();
  assert (x == CODE_encrypted_message || x == CODE_encrypted_message_service);
  unsigned sx = x;
  M->id = fetch_long ();
  peer_id_t chat = MK_ENCR_CHAT (fetch_int ());
  M->to_id = chat;
  peer_t *P = user_chat_get (chat);
  M->flags &= ~(FLAG_EMPTY | FLAG_DELETED);
  M->flags |= FLAG_ENCRYPTED;
  if (!P) {
    logprintf ("Encrypted message to unknown chat. Dropping\n");
    M->flags |= FLAG_EMPTY;
  }
  M->date = fetch_int ();


  int len = prefetch_strlen ();
  assert ((len & 15) == 8);
  decr_ptr = (void *)fetch_str (len);
  decr_end = decr_ptr + (len / 4);
  M->flags |= FLAG_ENCRYPTED;
  int ok = 0;
  if (P) {
    if (*(long long *)decr_ptr != P->encr_chat.key_fingerprint) {
      logprintf ("Encrypted message with bad fingerprint to chat %s\n", P->print_name);
      P = 0;
    }
    decr_ptr += 2;
  }
  if (P && decrypt_encrypted_message (&P->encr_chat) >= 0) {
    ok = 1;
    int *save_in_ptr = in_ptr;
    int *save_in_end = in_end;
    in_ptr = decr_ptr;
    int l = fetch_int ();
    in_end = in_ptr + l; 
    unsigned x = fetch_int ();
    if (x == CODE_decrypted_message_layer) {
      int layer = fetch_int ();
      assert (layer >= 0);
      x = fetch_int ();
    }
    assert (x == CODE_decrypted_message || x == CODE_decrypted_message_service);
    assert (M->id = fetch_long ());
    l = prefetch_strlen ();
    fetch_str (l); // random_bytes
    if (x == CODE_decrypted_message) {
      M->message = fetch_str_dup ();
      fetch_message_media_encrypted (&M->media);
    } else {
      assert (fetch_int () == (int)CODE_decrypted_message_action_set_message_t_t_l);
      P->encr_chat.ttl = fetch_int ();
      M->service = 1;
    }
    in_ptr = save_in_ptr;
    in_end = save_in_end;
  }
 
  if (sx == CODE_encrypted_message) {
    if (ok) {
      fetch_encrypted_message_file (&M->media);
    } else {
      x = fetch_int ();
      if (x == CODE_encrypted_file) {
        fetch_skip (7);
      } else {
        assert (x == CODE_encrypted_file_empty);
      }
      M->media.type = CODE_message_media_empty;
    }    
  }
}

void fetch_encrypted_message_file (struct message_media *M) {
  unsigned x = fetch_int ();
  assert (x == CODE_encrypted_file || x == CODE_encrypted_file_empty);
  if (x == CODE_encrypted_file_empty) {
    assert (M->type != CODE_decrypted_message_media_photo && M->type != CODE_decrypted_message_media_video);
  } else {
    assert (M->type == CODE_decrypted_message_media_photo || M->type == CODE_decrypted_message_media_video);
    M->encr_photo.id = fetch_long ();
    M->encr_photo.access_hash = fetch_long ();
    fetch_int ();
    //assert (M->encr_photo.size == fetch_int ());
    //M->encr_photo.size = fetch_int (); // Why it is not the same?
    M->encr_photo.dc_id = fetch_int ();
    M->encr_photo.key_fingerprint = fetch_int ();
    
  }
}

static int id_cmp (struct message *M1, struct message *M2) {
  if (M1->id < M2->id) { return -1; }
  else if (M1->id > M2->id) { return 1; }
  else { return 0; }
}

#define peer_cmp(a,b) (cmp_peer_id (a->id, b->id))

DEFINE_TREE(peer,peer_t *,peer_cmp,0)
DEFINE_TREE(message,struct message *,id_cmp,0)
struct tree_peer *peer_tree;
struct tree_message *message_tree;

int users_allocated;
int chats_allocated;
int messages_allocated;

struct message message_list = {
  .next_use = &message_list,
  .prev_use = &message_list
};

struct user *fetch_alloc_user (void) {
  int data[2];
  prefetch_data (data, 8);
  peer_t *U = user_chat_get (MK_USER (data[1]));
  if (U) {
    fetch_user (&U->user);
    return &U->user;
  } else {
    users_allocated ++;
    U = malloc (sizeof (*U));
    memset (U, 0, sizeof (*U));
    fetch_user (&U->user);
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    Peers[peer_num ++] = U;
    return &U->user;
  }
}

struct secret_chat *fetch_alloc_encrypted_chat (void) {
  int data[2];
  prefetch_data (data, 8);
  peer_t *U = user_chat_get (MK_ENCR_CHAT (data[1]));
  if (U) {
    fetch_encrypted_chat (&U->encr_chat);
    return &U->encr_chat;
  } else {
    encr_chats_allocated ++;
    U = malloc (sizeof (*U));
    memset (U, 0, sizeof (*U));
    fetch_encrypted_chat (&U->encr_chat);
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    Peers[peer_num ++] = U;
    return &U->encr_chat;
  }
}

void insert_encrypted_chat (peer_t *P) {
  encr_chats_allocated ++;
  peer_tree = tree_insert_peer (peer_tree, P, lrand48 ());
  Peers[peer_num ++] = P;
}

struct user *fetch_alloc_user_full (void) {
  int data[3];
  prefetch_data (data, 12);
  peer_t *U = user_chat_get (MK_USER (data[2]));
  if (U) {
    fetch_user_full (&U->user);
    return &U->user;
  } else {
    users_allocated ++;
    U = malloc (sizeof (*U));
    memset (U, 0, sizeof (*U));
    U->id = MK_USER (data[2]);
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    fetch_user_full (&U->user);
    Peers[peer_num ++] = U;
    return &U->user;
  }
}

void free_user (struct user *U) {
  if (U->first_name) { free (U->first_name); }
  if (U->last_name) { free (U->last_name); }
  if (U->print_name) { free (U->print_name); }
  if (U->phone) { free (U->phone); }
}

void free_photo_size (struct photo_size *S) {
  free (S->type);
  if (S->data) {
    free (S->data);
  }
}

void free_photo (struct photo *P) {
  if (!P->access_hash) { return; }
  if (P->caption) { free (P->caption); }
  if (P->sizes) {
    int i;
    for (i = 0; i < P->sizes_num; i++) {
      free_photo_size (&P->sizes[i]);
    }
    free (P->sizes);
  }
}

void free_video (struct video *V) {
  if (!V->access_hash) { return; }
  free (V->caption);
  free_photo_size (&V->thumb);
}

void free_message_media (struct message_media *M) {
  switch (M->type) {
  case CODE_message_media_empty:
  case CODE_message_media_geo:
    return;
  case CODE_message_media_photo:
    free_photo (&M->photo);
    return;
  case CODE_message_media_video:
    free_video (&M->video);
    return;
  case CODE_message_media_contact:
    free (M->phone);
    free (M->first_name);
    free (M->last_name);
    return;
  case CODE_message_media_unsupported:
    free (M->data);
    return;
  case CODE_decrypted_message_media_photo:
    free (M->encr_photo.key);
    free (M->encr_photo.iv);
    return;
  case CODE_decrypted_message_media_video:
    free (M->encr_video.key);
    free (M->encr_video.iv);
    return;
  case 0:
    break;
  default:
    logprintf ("%08x\n", M->type);
    assert (0);
  }
}

void free_message_action (struct message_action *M) {
  switch (M->type) {
  case CODE_message_action_empty:
    break;
  case CODE_message_action_chat_create:
    free (M->title);
    free (M->users);
    break;
  case CODE_message_action_chat_edit_title:
    free (M->new_title);
    break;
  case CODE_message_action_chat_edit_photo:
    free_photo (&M->photo);
    break;
  case CODE_message_action_chat_delete_photo:
    break;
  case CODE_message_action_chat_add_user:
    break;
  case CODE_message_action_chat_delete_user:
    break;
  case 0:
    break;
  default:
    assert (0);
  }
}

void free_message (struct message *M) {
  if (!M->service) {
    if (M->message) { free (M->message); }
    free_message_media (&M->media);
  } else {
    free_message_action (&M->action);
  }
}

void message_del_use (struct message *M) {
  M->next_use->prev_use = M->prev_use;
  M->prev_use->next_use = M->next_use;
}

void message_add_use (struct message *M) {
  M->next_use = message_list.next_use;
  M->prev_use = &message_list;
  M->next_use->prev_use = M;
  M->prev_use->next_use = M;
}

void message_add_peer (struct message *M) {
  peer_id_t id;
  if (!cmp_peer_id (M->to_id, MK_USER (our_id))) {
    id = M->from_id;
  } else {
    id = M->to_id;
  }
  peer_t *P = user_chat_get (id);
  if (!P) {
    P = malloc (sizeof (*P));
    memset (P, 0, sizeof (*P));
    P->id = id;
    P->flags = FLAG_EMPTY;
    switch (get_peer_type (id)) {
    case PEER_USER:
      users_allocated ++;
      break;
    case PEER_CHAT:
      chats_allocated ++;
      break;
    case PEER_GEO_CHAT:
      geo_chats_allocated ++;
      break;
    case PEER_ENCR_CHAT:
      encr_chats_allocated ++;
      break;
    }
    peer_tree = tree_insert_peer (peer_tree, P, lrand48 ());
    Peers[peer_num ++] = P;
  }
  M->next = P->last;
  if (M->next) { M->next->prev = M; }
  M->prev = 0;
  P->last = M;
}

void message_del_peer (struct message *M) {
  peer_id_t id;
  if (!cmp_peer_id (M->to_id, MK_USER (our_id))) {
    id = M->from_id;
  } else {
    id = M->to_id;
  }
  peer_t *P = user_chat_get (id);
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

struct message *fetch_alloc_message (void) {
  struct message *M = malloc (sizeof (*M));
  fetch_message (M);
  struct message *M1 = tree_lookup_message (message_tree, M);
  messages_allocated ++;
  if (M1) {
    message_del_use (M1);
    message_del_peer (M1);
    free_message (M1);
    memcpy (M1, M, sizeof (*M));
    free (M);
    message_add_use (M1);
    message_add_peer (M1);
    messages_allocated --;
    return M1;
  } else {
    message_add_use (M);
    message_add_peer (M);
    message_tree = tree_insert_message (message_tree, M, lrand48 ());
    return M;
  }
}

struct message *fetch_alloc_geo_message (void) {
  struct message *M = malloc (sizeof (*M));
  fetch_geo_message (M);
  struct message *M1 = tree_lookup_message (message_tree, M);
  messages_allocated ++;
  if (M1) {
    message_del_use (M1);
    message_del_peer (M1);
    free_message (M1);
    memcpy (M1, M, sizeof (*M));
    free (M);
    message_add_use (M1);
    message_add_peer (M1);
    messages_allocated --;
    return M1;
  } else {
    message_add_use (M);
    message_add_peer (M);
    message_tree = tree_insert_message (message_tree, M, lrand48 ());
    return M;
  }
}

struct message *fetch_alloc_encrypted_message (void) {
  struct message *M = malloc (sizeof (*M));
  fetch_encrypted_message (M);
  struct message *M1 = tree_lookup_message (message_tree, M);
  messages_allocated ++;
  if (M1) {
    message_del_use (M1);
    message_del_peer (M1);
    free_message (M1);
    memcpy (M1, M, sizeof (*M));
    free (M);
    message_add_use (M1);
    message_add_peer (M1);
    messages_allocated --;
    return M1;
  } else {
    message_add_use (M);
    message_add_peer (M);
    message_tree = tree_insert_message (message_tree, M, lrand48 ());
    return M;
  }
}

struct message *fetch_alloc_message_short (void) {
  struct message *M = malloc (sizeof (*M));
  fetch_message_short (M);
  struct message *M1 = tree_lookup_message (message_tree, M);
  messages_allocated ++;
  if (M1) {
    message_del_use (M1);
    message_del_peer (M1);
    free_message (M1);
    memcpy (M1, M, sizeof (*M));
    free (M);
    message_add_use (M1);
    message_add_peer (M1);
    messages_allocated --;
    return M1;
  } else {
    message_add_use (M);
    message_add_peer (M);
    message_tree = tree_insert_message (message_tree, M, lrand48 ());
    return M;
  }
}

struct message *fetch_alloc_message_short_chat (void) {
  struct message *M = malloc (sizeof (*M));
  fetch_message_short_chat (M);
  if (verbosity >= 2) {
    logprintf ("Read message with id %lld\n", M->id);
  }
  struct message *M1 = tree_lookup_message (message_tree, M);
  messages_allocated ++;
  if (M1) {
    message_del_use (M1);
    message_del_peer (M1);
    free_message (M1);
    memcpy (M1, M, sizeof (*M));
    free (M);
    message_add_use (M1);
    message_add_peer (M1);
    messages_allocated --;
    return M1;
  } else {
    message_add_use (M);
    message_add_peer (M);
    message_tree = tree_insert_message (message_tree, M, lrand48 ());
    return M;
  }
}

struct chat *fetch_alloc_chat (void) {
  int data[2];
  prefetch_data (data, 8);
  peer_t *U = user_chat_get (MK_CHAT (data[1]));
  if (U) {
    fetch_chat (&U->chat);
    return &U->chat;
  } else {
    chats_allocated ++;
    U = malloc (sizeof (*U));
    memset (U, 0, sizeof (*U));
    fetch_chat (&U->chat);
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    Peers[peer_num ++] = U;
    return &U->chat;
  }
}

struct chat *fetch_alloc_chat_full (void) {
  int data[3];
  prefetch_data (data, 12);
  peer_t *U = user_chat_get (MK_CHAT (data[2]));
  if (U) {
    fetch_chat_full (&U->chat);
    return &U->chat;
  } else {
    chats_allocated ++;
    U = malloc (sizeof (*U));
    memset (U, 0, sizeof (*U));
    U->id = MK_CHAT (data[2]);
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    fetch_chat_full (&U->chat);
    Peers[peer_num ++] = U;
    return &U->chat;
  }
}

void free_chat (struct chat *U) {
  if (U->title) { free (U->title); }
  if (U->print_title) { free (U->print_title); }
}

int print_stat (char *s, int len) {
  return snprintf (s, len, 
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

peer_t *user_chat_get (peer_id_t id) {
  peer_t U;
  U.id = id;
  return tree_lookup_peer (peer_tree, &U);
}

struct message *message_get (long long id) {
  struct message M;
  M.id = id;
  return tree_lookup_message (message_tree, &M);
}

void update_message_id (struct message *M, long long id) {
  message_tree = tree_delete_message (message_tree, M);
  M->id = id;
  message_tree = tree_insert_message (message_tree, M, lrand48 ());
}

void message_insert (struct message *M) {
  message_add_use (M);
  message_add_peer (M);
  message_tree = tree_insert_message (message_tree, M, lrand48 ());
}
