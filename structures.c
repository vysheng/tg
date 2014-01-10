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

#include "config.h"

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
#include "binlog.h"

#define sha1 SHA1

static int id_cmp (struct message *M1, struct message *M2);
#define peer_cmp(a,b) (cmp_peer_id (a->id, b->id))
DEFINE_TREE(peer,peer_t *,peer_cmp,0)
DEFINE_TREE(message,struct message *,id_cmp,0)


struct message message_list = {
  .next_use = &message_list,
  .prev_use = &message_list
};

struct tree_peer *peer_tree;
struct tree_message *message_tree;
struct tree_message *message_unsent_tree;

int users_allocated;
int chats_allocated;
int messages_allocated;
int peer_num;
int encr_chats_allocated;
int geo_chats_allocated;

int our_id;
int verbosity;

peer_t *Peers[MAX_PEER_NUM];
extern int binlog_enabled;


void fetch_skip_photo (void);

#define code_assert(x) if (!(x)) { logprintf ("Can not parse at line %d\n", __LINE__); assert (0); return -1; }
#define code_try(x) if ((x) == -1) { return -1; }

/*
 *
 * Fetch simple structures (immediate fetch into buffer)
 *
 */

int fetch_file_location (struct file_location *loc) {
  int x = fetch_int ();
  code_assert (x == CODE_file_location_unavailable || x == CODE_file_location);

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

int fetch_user_status (struct user_status *S) {
  unsigned x = fetch_int ();
  code_assert (x == CODE_user_status_empty || x == CODE_user_status_online || x == CODE_user_status_offline);
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

/*
 *
 * Skip simple structures 
 *
 */

int fetch_skip_file_location (void) {
  int x = fetch_int ();
  code_assert (x == CODE_file_location_unavailable || x == CODE_file_location);

  if (x == CODE_file_location_unavailable) {
    in_ptr += 5;
  } else {
    in_ptr += 6;
  }
  return 0;
}

int fetch_skip_user_status (void) {
  unsigned x = fetch_int ();
  code_assert (x == CODE_user_status_empty || x == CODE_user_status_online || x == CODE_user_status_offline);
  if (x != CODE_user_status_empty) {
    fetch_int ();
  }
  return 0;
}

char *create_print_name (peer_id_t id, const char *a1, const char *a2, const char *a3, const char *a4) {
  const char *d[4];
  d[0] = a1; d[1] = a2; d[2] = a3; d[3] = a4;
  static char buf[10000];
  buf[0] = 0;
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
  int fl = strlen (s);
  int cc = 0;
  while (1) {
    int ok = 1;
    int i;
    for (i = 0; i < peer_num; i++) {
      assert (Peers[i]);
      if (cmp_peer_id (Peers[i]->id, id) && Peers[i]->print_name && !strcmp (Peers[i]->print_name, s)) {
        ok = 0;
        break;
      }
    }
    if (ok) {
      break;
    }
    cc ++;
    assert (cc <= 9999);
    sprintf (s + fl, "#%d", cc);
  }
  return tstrdup (s);
}

/*
 *
 * Fetch with log event
 *
 */

long long fetch_user_photo (struct user *U) {
  unsigned x = fetch_int ();
  code_assert (x == CODE_user_profile_photo || x == CODE_user_profile_photo_old || x == CODE_user_profile_photo_empty);
  if (x == CODE_user_profile_photo_empty) {
    bl_do_set_user_profile_photo (U, 0, 0, 0);
    return 0;
  }
  long long photo_id = 1;
  if (x == CODE_user_profile_photo) {
    photo_id = fetch_long ();
  }
  struct file_location big;
  struct file_location small;
  code_try (fetch_file_location (&small));
  code_try (fetch_file_location (&big));

  bl_do_set_user_profile_photo (U, photo_id, &big, &small);
  return 0;
}

int fetch_user (struct user *U) {
  unsigned x = fetch_int ();
  code_assert (x == CODE_user_empty || x == CODE_user_self || x == CODE_user_contact ||  x == CODE_user_request || x == CODE_user_foreign || x == CODE_user_deleted);
  U->id = MK_USER (fetch_int ());
  if (x == CODE_user_empty) {
    return 0;
  }
  
  if (x == CODE_user_self) {
    assert (!our_id || (our_id == get_peer_id (U->id)));
    if (!our_id) {
      bl_do_set_our_id (get_peer_id (U->id));
      write_auth_file ();
    }
  }
  
  int new = 0;
  if (!(U->flags & FLAG_CREATED)) { 
    new = 1;
  }
  if (new) {
    int l1 = prefetch_strlen ();
    code_assert (l1 >= 0);
    char *s1 = fetch_str (l1);
    int l2 = prefetch_strlen ();
    code_assert (l2 >= 0);
    char *s2 = fetch_str (l2);
    
    if (x == CODE_user_deleted && !(U->flags & FLAG_DELETED)) {
      bl_do_new_user (get_peer_id (U->id), s1, l1, s2, l2, 0, 0, 0, 0);
      bl_do_user_delete (U);
    }
    if (x != CODE_user_deleted) {
      long long access_token = 0;
      if (x != CODE_user_self) {
        access_token = fetch_long ();
      }
      int phone_len = 0;
      char *phone = 0;
      if (x != CODE_user_foreign) {
        phone_len = prefetch_strlen ();
        code_assert (phone_len >= 0);
        phone = fetch_str (phone_len);
      }
      bl_do_new_user (get_peer_id (U->id), s1, l1, s2, l2, access_token, phone, phone_len, x == CODE_user_contact);
      if (fetch_user_photo (U) < 0) { return -1; }
    
      if (fetch_user_status (&U->status) < 0) { return -1; }
      if (x == CODE_user_self) {
        fetch_bool ();
      }
    }
  } else {
    int l1 = prefetch_strlen ();
    char *s1 = fetch_str (l1);
    int l2 = prefetch_strlen ();
    char *s2 = fetch_str (l2);
    
    bl_do_set_user_name (U, s1, l1, s2, l2);

    if (x == CODE_user_deleted && !(U->flags & FLAG_DELETED)) {
      bl_do_user_delete (U);
    }
    if (x != CODE_user_deleted) {
      if (x != CODE_user_self) {
        bl_do_set_user_access_token (U, fetch_long ());
      }
      if (x != CODE_user_foreign) {
        int l = prefetch_strlen ();
        char *s = fetch_str (l);
        bl_do_set_user_phone (U, s, l);
      }
      if (fetch_user_photo (U) < 0) { return -1; }
    
      fetch_user_status (&U->status);
      if (x == CODE_user_self) {
        fetch_bool ();
      }

      if (x == CODE_user_contact) {
        bl_do_set_user_friend (U, 1);
      } else  {
        bl_do_set_user_friend (U, 0);
      }
    }
  }
  return 0;
}

void fetch_encrypted_chat (struct secret_chat *U) {
  unsigned x = fetch_int ();
  assert (x == CODE_encrypted_chat_empty || x == CODE_encrypted_chat_waiting || x == CODE_encrypted_chat_requested ||  x == CODE_encrypted_chat || x == CODE_encrypted_chat_discarded);
  U->id = MK_ENCR_CHAT (fetch_int ());
  if (x == CODE_encrypted_chat_empty) {
    return;
  }
  int new = !(U->flags & FLAG_CREATED);
 
  if (x == CODE_encrypted_chat_discarded) {
    if (new) {
      logprintf ("Unknown chat in deleted state. May be we forgot something...\n");
      return;
    }
    bl_do_encr_chat_delete (U);
    write_secret_chat_file ();
    return;
  }

  static char g_key[256];
  static char nonce[256];
  if (new) {
    long long access_hash = fetch_long ();
    int date = fetch_int ();
    int admin_id = fetch_int ();
    int user_id = fetch_int () + admin_id - our_id;

    if (x == CODE_encrypted_chat_waiting) {
      logprintf ("Unknown chat in waiting state. May be we forgot something...\n");
      return;
    }
    if (x == CODE_encrypted_chat_requested || x == CODE_encrypted_chat) {
      memset (g_key, 0, sizeof (g_key));
      memset (nonce, 0, sizeof (nonce));
    }
    
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    if (l != 256) { logprintf ("l = %d\n", l); }
    if (l < 256) {
      memcpy (g_key + 256 - l, s, l);
    } else {
      memcpy (g_key, s +  (l - 256), 256);
    }
    
    /*l = prefetch_strlen ();
    s = fetch_str (l);
    if (l != 256) { logprintf ("l = %d\n", l); }
    if (l < 256) {
      memcpy (nonce + 256 - l, s, l);
    } else {
      memcpy (nonce, s +  (l - 256), 256);
    }*/
    
    if (x == CODE_encrypted_chat) {
      fetch_long (); // fingerprint
    }

    if (x == CODE_encrypted_chat) {
      logprintf ("Unknown chat in ok state. May be we forgot something...\n");
      return;
    }

    bl_do_encr_chat_requested (U, access_hash, date, admin_id, user_id, (void *)g_key, (void *)nonce);
    write_secret_chat_file ();
  } else {
    bl_do_set_encr_chat_access_hash (U, fetch_long ());
    bl_do_set_encr_chat_date (U, fetch_int ());
    if (fetch_int () != U->admin_id) {
      logprintf ("Changed admin in secret chat. WTF?\n");
      return;
    }
    if (U->user_id != U->admin_id + fetch_int () - our_id) {
      logprintf ("Changed partner in secret chat. WTF?\n");
      return;
    }
    if (x == CODE_encrypted_chat_waiting) {
      bl_do_set_encr_chat_state (U, sc_waiting);
      write_secret_chat_file ();
      return; // We needed only access hash from here
    }
    
    if (x == CODE_encrypted_chat_requested || x == CODE_encrypted_chat) {
      memset (g_key, 0, sizeof (g_key));
      memset (nonce, 0, sizeof (nonce));
    }
    
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    if (l != 256) { logprintf ("l = %d\n", l); }
    if (l < 256) {
      memcpy (g_key + 256 - l, s, l);
    } else {
      memcpy (g_key, s +  (l - 256), 256);
    }
    
    /*l = prefetch_strlen ();
    s = fetch_str (l);
    if (l != 256) { logprintf ("l = %d\n", l); }
    if (l < 256) {
      memcpy (nonce + 256 - l, s, l);
    } else {
      memcpy (nonce, s +  (l - 256), 256);
    }*/
   
    if (x == CODE_encrypted_chat_requested) {
      return; // Duplicate?
    }
    bl_do_encr_chat_accepted (U, (void *)g_key, (void *)nonce, fetch_long ());
  }
  write_secret_chat_file ();
}

void fetch_notify_settings (void);
void fetch_user_full (struct user *U) {
  assert (fetch_int () == CODE_user_full);
  fetch_alloc_user ();
  unsigned x;
  assert (fetch_int () == (int)CODE_contacts_link);
  x = fetch_int ();
  assert (x == CODE_contacts_my_link_empty || x == CODE_contacts_my_link_requested || x == CODE_contacts_my_link_contact);
  if (x == CODE_contacts_my_link_requested) {
    fetch_bool ();
  }
  x = fetch_int ();
  assert (x == CODE_contacts_foreign_link_unknown || x == CODE_contacts_foreign_link_requested || x == CODE_contacts_foreign_link_mutual);
  if (x == CODE_contacts_foreign_link_requested) {
    fetch_bool ();
  }
  fetch_alloc_user ();

  int *start = in_ptr;
  fetch_skip_photo ();
  bl_do_set_user_full_photo (U, start, 4 * (in_ptr - start));

  fetch_notify_settings ();

  bl_do_set_user_blocked (U, fetch_bool ());
  int l1 = prefetch_strlen ();
  char *s1 = fetch_str (l1);
  int l2 = prefetch_strlen ();
  char *s2 = fetch_str (l2);
  bl_do_set_user_real_name (U, s1, l1, s2, l2);
}

void fetch_chat (struct chat *C) {
  unsigned x = fetch_int ();
  assert (x == CODE_chat_empty || x == CODE_chat || x == CODE_chat_forbidden);
  C->id = MK_CHAT (fetch_int ());
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

    struct file_location small;
    struct file_location big;
    memset (&small, 0, sizeof (small));
    memset (&big, 0, sizeof (big));
    int users_num = -1;
    int date = 0;
    int version = -1;

    if (x == CODE_chat) {
      unsigned y = fetch_int ();
      if (y == CODE_chat_photo_empty) {
        small.dc = -2;
        big.dc = -2;
      } else {
        assert (y == CODE_chat_photo);
        fetch_file_location (&small);
        fetch_file_location (&big);
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
    bl_do_set_chat_title (C, s, l);
    
    struct file_location small;
    struct file_location big;
    memset (&small, 0, sizeof (small));
    memset (&big, 0, sizeof (big));
    
    if (x == CODE_chat) {
      unsigned y = fetch_int ();
      if (y == CODE_chat_photo_empty) {
        small.dc = -2;
        big.dc = -2;
      } else {
        assert (y == CODE_chat_photo);
        fetch_file_location (&small);
        fetch_file_location (&big);
      }
      bl_do_set_chat_photo (C, &big, &small);
      int users_num = fetch_int ();
      bl_do_set_chat_date (C, fetch_int ());
      bl_do_set_chat_set_in_chat (C, fetch_bool ());
      bl_do_set_chat_version (C, users_num, fetch_int ());
    } else {
      bl_do_set_chat_date (C, fetch_int ());
    }
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
  //C->flags &= ~(FLAG_DELETED | FLAG_FORBIDDEN | FLAG_CHAT_IN_CHAT);
  //C->flags |= FLAG_CREATED;
  x = fetch_int ();
  int version = 0;
  struct chat_user *users = 0;
  int users_num = 0;
  int admin_id = 0;

  if (x == CODE_chat_participants) {
    assert (fetch_int () == get_peer_id (C->id));
    admin_id =  fetch_int ();
    assert (fetch_int () == CODE_vector);
    users_num = fetch_int ();
    users = talloc (sizeof (struct chat_user) * users_num);
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
  fetch_skip_photo ();
  int *end = in_ptr;
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
  if (admin_id) {
    bl_do_set_chat_admin (C, admin_id);
  }
  if (version > 0) {
    bl_do_set_chat_participants (C, version, users_num, users);
    free (users);
  }
  bl_do_set_chat_full_photo (C, start, 4 * (end - start));
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
//      S->data = talloc (S->size);
      fetch_str (S->size);
//      memcpy (S->data, fetch_str (S->size), S->size);
    }
  }
}

void fetch_skip_photo_size (void) {
  unsigned x = fetch_int ();
  assert (x == CODE_photo_size || x == CODE_photo_cached_size || x == CODE_photo_size_empty);
  int l = prefetch_strlen ();
  fetch_str (l); // type
  if (x != CODE_photo_size_empty) {
    fetch_skip_file_location ();
    in_ptr += 2; // w, h
    if (x == CODE_photo_size) {
      in_ptr ++;
    } else {
      l = prefetch_strlen ();
      fetch_str (l);
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

void fetch_skip_geo (void) {
  unsigned x = fetch_int ();
  assert (x == CODE_geo_point || x == CODE_geo_point_empty);
  if (x == CODE_geo_point) {
    in_ptr += 4;
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
  P->sizes = talloc (sizeof (struct photo_size) * P->sizes_num);
  int i;
  for (i = 0; i < P->sizes_num; i++) {
    fetch_photo_size (&P->sizes[i]);
  }
}

void fetch_skip_photo (void) {
  unsigned x = fetch_int ();
  assert (x == CODE_photo_empty || x == CODE_photo);
  in_ptr += 2; // id
  if (x == CODE_photo_empty) { return; }
  in_ptr += 2  +1 + 1; // access_hash, user_id, date
  int l = prefetch_strlen ();
  fetch_str (l); // caption
  fetch_skip_geo ();
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  int i;
  for (i = 0; i < n; i++) {
    fetch_skip_photo_size ();
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

void fetch_skip_video (void) {
  unsigned x = fetch_int ();
  fetch_long ();
  if (x == CODE_video_empty) { return; }
  fetch_skip (4);
  int l = prefetch_strlen ();
  fetch_str (l);
  fetch_skip (2);
  fetch_skip_photo_size ();
  fetch_skip (3);
}

void fetch_audio (struct audio *V) {
  memset (V, 0, sizeof (*V));
  unsigned x = fetch_int ();
  V->id = fetch_long ();
  if (x == CODE_audio_empty) { return; }
  V->access_hash = fetch_long ();
  V->user_id = fetch_int ();
  V->date = fetch_int ();
  V->duration = fetch_int ();
  V->size = fetch_int ();
  V->dc_id = fetch_int ();
}

void fetch_skip_audio (void) {
  unsigned x = fetch_int ();
  fetch_skip (2);
  if (x == CODE_audio_empty) { return; }
  fetch_skip (7);
}

void fetch_document (struct document *V) {
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
  fetch_photo_size (&V->thumb);
  V->dc_id = fetch_int ();
}

void fetch_skip_document (void) {
  unsigned x = fetch_int ();
  fetch_skip (2);
  if (x == CODE_document_empty) { return; }
  fetch_skip (4);
  int l = prefetch_strlen ();
  fetch_str (l);
  l = prefetch_strlen ();
  fetch_str (l);
  fetch_skip (1);
  fetch_skip_photo_size ();
  fetch_skip (1);
}

void fetch_message_action (struct message_action *M) {
  memset (M, 0, sizeof (*M));
  unsigned x = fetch_int ();
  M->type = x;
  switch (x) {
  case CODE_message_action_empty:
    break;
  case CODE_message_action_geo_chat_create:
    {
      int l = prefetch_strlen (); // title
      char *s = fetch_str (l);
      int l2 = prefetch_strlen (); // checkin
      char *s2 = fetch_str (l2);
      logprintf ("Message action: Created geochat %.*s in address %.*s\n", l, s, l2, s2);
    }
    break;
  case CODE_message_action_geo_chat_checkin:
    break;
  case CODE_message_action_chat_create:
    M->title = fetch_str_dup ();
    assert (fetch_int () == (int)CODE_vector);
    M->user_num = fetch_int ();
    M->users = talloc (M->user_num * 4);
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

void fetch_skip_message_action (void) {
  unsigned x = fetch_int ();
  int l;
  switch (x) {
  case CODE_message_action_empty:
    break;
  case CODE_message_action_geo_chat_create:
    {
      l = prefetch_strlen ();
      fetch_str (l);
      l = prefetch_strlen ();
      fetch_str (l);
    }
    break;
  case CODE_message_action_geo_chat_checkin:
    break;
  case CODE_message_action_chat_create:
    l = prefetch_strlen ();
    fetch_str (l);
    assert (fetch_int () == (int)CODE_vector);
    l = fetch_int ();
    fetch_skip (l);
    break;
  case CODE_message_action_chat_edit_title:
    l = prefetch_strlen ();
    fetch_str (l);
    break;
  case CODE_message_action_chat_edit_photo:
    fetch_skip_photo ();
    break;
  case CODE_message_action_chat_delete_photo:
    break;
  case CODE_message_action_chat_add_user:
    fetch_int ();
    break;
  case CODE_message_action_chat_delete_user:
    fetch_int ();
    break;
  default:
    assert (0);
  }
}

void fetch_message_short (struct message *M) {
  int new = !(M->flags & FLAG_CREATED);

  if (new) {
    int id = fetch_int ();
    int from_id = fetch_int ();
    int to_id = our_id;
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    
    fetch_pts ();
    
    int date = fetch_int ();
    fetch_seq ();

    bl_do_create_message_text (id, from_id, PEER_USER, to_id, date, l, s);
  } else {
    fetch_int (); // id
    fetch_int (); // from_id
    int l = prefetch_strlen (); 
    fetch_str (l); // text
    
    fetch_pts ();
    fetch_int ();
    fetch_seq ();
  }
}

void fetch_message_short_chat (struct message *M) {
  int new = !(M->flags & FLAG_CREATED);

  if (new) {
    int id = fetch_int ();
    int from_id = fetch_int ();
    int to_id = fetch_int ();
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    
    fetch_pts ();
    
    int date = fetch_int ();
    fetch_seq ();

    bl_do_create_message_text (id, from_id, PEER_CHAT, to_id, date, l, s);
  } else {
    fetch_int (); // id
    fetch_int (); // from_id
    fetch_int (); // to_id
    int l = prefetch_strlen (); 
    fetch_str (l); // text
    
    fetch_pts ();
    fetch_int ();
    fetch_seq ();
  }
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
  case CODE_message_media_audio:
    fetch_audio (&M->audio);
    break;
  case CODE_message_media_document:
    fetch_document (&M->document);
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

void fetch_skip_message_media (void) {
  unsigned x = fetch_int ();
  switch (x) {
  case CODE_message_media_empty:
    break;
  case CODE_message_media_photo:
    fetch_skip_photo ();
    break;
  case CODE_message_media_video:
    fetch_skip_video ();
    break;
  case CODE_message_media_audio:
    fetch_skip_audio ();
    break;
  case CODE_message_media_document:
    fetch_skip_document ();
    break;
  case CODE_message_media_geo:
    fetch_skip_geo ();
    break;
  case CODE_message_media_contact:
    {
      int l;
      l = prefetch_strlen ();
      fetch_str (l);
      l = prefetch_strlen ();
      fetch_str (l);
      l = prefetch_strlen ();
      fetch_str (l);
      fetch_int ();
    }
    break;
  case CODE_message_media_unsupported:
    {
      int l = prefetch_strlen ();
      fetch_str (l);
    }
    break;
  default:
    logprintf ("type = 0x%08x\n", x);
    assert (0);
  }
}

void fetch_skip_message_media_encrypted (void) {
  unsigned x = fetch_int ();
  int l;
  switch (x) {
  case CODE_decrypted_message_media_empty:
    break;
  case CODE_decrypted_message_media_photo:
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    fetch_skip (5);
    
    l = prefetch_strlen  ();
    fetch_str (l);
    
    l = prefetch_strlen  ();
    fetch_str (l);
    break;
  case CODE_decrypted_message_media_video:
    l = prefetch_strlen ();
    fetch_str (l); // thumb

    fetch_skip (6);
    
    l = prefetch_strlen  ();
    fetch_str (l);
    
    l = prefetch_strlen  ();
    fetch_str (l);
    break;
  case CODE_decrypted_message_media_audio:
    fetch_skip (2);
    
    l = prefetch_strlen  ();
    fetch_str (l);
    
    l = prefetch_strlen  ();
    fetch_str (l);
    break;
  case CODE_decrypted_message_media_document:
    l = prefetch_strlen ();
    fetch_str (l); // thumb

    fetch_skip (2);
    
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    fetch_skip (1);
    
    l = prefetch_strlen  ();
    fetch_str (l);
    
    l = prefetch_strlen  ();
    fetch_str (l);
    break;
  case CODE_decrypted_message_media_geo_point:
    fetch_skip (4);
    break;
  case CODE_decrypted_message_media_contact:
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    fetch_skip (1);
    break;
  default:
    logprintf ("type = 0x%08x\n", x);
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
    M->type = x;
    l = prefetch_strlen ();
    fetch_str (l); // thumb
    fetch_int (); // thumb_w
    fetch_int (); // thumb_h
    M->encr_video.duration = fetch_int ();
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
    M->type = x;
    M->encr_audio.duration = fetch_int ();
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
    M->type = x;
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
    logprintf ("type = 0x%08x\n", x);
    assert (0);
  }
}

void fetch_skip_message_action_encrypted (void) {
  unsigned x = fetch_int ();
  switch (x) {
  case CODE_decrypted_message_action_set_message_t_t_l:
    fetch_skip (1);
    break;
  default:
    logprintf ("x = 0x%08x\n", x);
    assert (0);
  }
}

void fetch_message_action_encrypted (struct message_action *M) {
  unsigned x = fetch_int ();
  switch (x) {
  case CODE_decrypted_message_action_set_message_t_t_l:
    M->type = x;
    M->ttl = fetch_int ();
    break;
  default:
    logprintf ("x = 0x%08x\n", x);
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
  peer_id_t to_id = fetch_peer_id ();

  fetch_bool (); // out.

  int unread = fetch_bool ();
  int date = fetch_int ();

  int new = !(M->flags & FLAG_CREATED);

  if (x == CODE_message_service) {
    int *start = in_ptr;
    fetch_skip_message_action ();
    if (new) {
      if (fwd_from_id) {
        bl_do_create_message_service_fwd (id, from_id, get_peer_type (to_id), get_peer_id (to_id), date, fwd_from_id, fwd_date, start, (in_ptr - start));
      } else {
        bl_do_create_message_service (id, from_id, get_peer_type (to_id), get_peer_id (to_id), date, start, (in_ptr - start));
      }
    }
  } else {
    int l = prefetch_strlen ();
    char *s = fetch_str (l);
    int *start = in_ptr;
    fetch_skip_message_media ();
    if (new) {
      if (fwd_from_id) {
        bl_do_create_message_media_fwd (id, from_id, get_peer_type (to_id), get_peer_id (to_id), date, fwd_from_id, fwd_date, l, s, start, in_ptr - start);
      } else {
        bl_do_create_message_media (id, from_id, get_peer_type (to_id), get_peer_id (to_id), date, l, s, start, in_ptr - start);
      }
    }
  }
  bl_do_set_unread (M, unread);
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

static int *decr_ptr;
static int *decr_end;

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
  unsigned x = fetch_int ();
  assert (x == CODE_encrypted_message || x == CODE_encrypted_message_service);
  unsigned sx = x;
  int new = !(M->flags & FLAG_CREATED);
  long long id = fetch_long ();
  int to_id = fetch_int ();
  peer_id_t chat = MK_ENCR_CHAT (to_id);
  int date = fetch_int ();
  
  peer_t *P = user_chat_get (chat);
  if (!P) {
    logprintf ("Encrypted message to unknown chat. Dropping\n");
    M->flags |= FLAG_MESSAGE_EMPTY;
  }


  int len = prefetch_strlen ();
  assert ((len & 15) == 8);
  decr_ptr = (void *)fetch_str (len);
  decr_end = decr_ptr + (len / 4);
  int ok = 0;
  if (P) {
    if (*(long long *)decr_ptr != P->encr_chat.key_fingerprint) {
      logprintf ("Encrypted message with bad fingerprint to chat %s\n", P->print_name);
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
      fetch_skip_message_media_encrypted ();
      end = in_ptr;
    } else {
      start = in_ptr;
      fetch_skip_message_action_encrypted ();
      end = in_ptr;
    }
    in_ptr = save_in_ptr;
    in_end = save_in_end;
  }
 
  if (sx == CODE_encrypted_message) {
    if (ok) {
      int *start_file = in_ptr;
      fetch_skip_encrypted_message_file ();
      if (x == CODE_decrypted_message) {
        bl_do_create_message_media_encr (id, P->encr_chat.user_id, PEER_ENCR_CHAT, to_id, date, l, s, start, end - start, start_file, in_ptr - start_file);
      }
    } else {
      x = fetch_int ();
      if (x == CODE_encrypted_file) {
        fetch_skip (7);
      } else {
        assert (x == CODE_encrypted_file_empty);
      }
      M->media.type = CODE_message_media_empty;
    }    
  } else {
    if (ok && x == CODE_decrypted_message_service) {
      bl_do_create_message_service_encr (id, P->encr_chat.user_id, PEER_ENCR_CHAT, to_id, date, start, end - start);
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

void fetch_skip_encrypted_message_file (void) {
  unsigned x = fetch_int ();
  assert (x == CODE_encrypted_file || x == CODE_encrypted_file_empty);
  if (x == CODE_encrypted_file_empty) {
  } else {
    fetch_skip (7);
  }
}

static int id_cmp (struct message *M1, struct message *M2) {
  if (M1->id < M2->id) { return -1; }
  else if (M1->id > M2->id) { return 1; }
  else { return 0; }
}

struct user *fetch_alloc_user (void) {
  int data[2];
  prefetch_data (data, 8);
  peer_t *U = user_chat_get (MK_USER (data[1]));
  if (!U) {
    users_allocated ++;
    U = talloc0 (sizeof (*U));
    U->id = MK_USER (data[1]);
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    assert (peer_num < MAX_PEER_NUM);
    Peers[peer_num ++] = U;
  }
  fetch_user (&U->user);
  return &U->user;
}

struct secret_chat *fetch_alloc_encrypted_chat (void) {
  int data[2];
  prefetch_data (data, 8);
  peer_t *U = user_chat_get (MK_ENCR_CHAT (data[1]));
  if (!U) {
    U = talloc0 (sizeof (*U));
    U->id = MK_ENCR_CHAT (data[1]);
    encr_chats_allocated ++;
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    assert (peer_num < MAX_PEER_NUM);
    Peers[peer_num ++] = U;
  }
  fetch_encrypted_chat (&U->encr_chat);
  return &U->encr_chat;
}

void insert_encrypted_chat (peer_t *P) {
  encr_chats_allocated ++;
  peer_tree = tree_insert_peer (peer_tree, P, lrand48 ());
  assert (peer_num < MAX_PEER_NUM);
  Peers[peer_num ++] = P;
}

void insert_user (peer_t *P) {
  users_allocated ++;
  peer_tree = tree_insert_peer (peer_tree, P, lrand48 ());
  assert (peer_num < MAX_PEER_NUM);
  Peers[peer_num ++] = P;
}

void insert_chat (peer_t *P) {
  chats_allocated ++;
  peer_tree = tree_insert_peer (peer_tree, P, lrand48 ());
  assert (peer_num < MAX_PEER_NUM);
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
    U = talloc0 (sizeof (*U));
    U->id = MK_USER (data[2]);
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    fetch_user_full (&U->user);
    assert (peer_num < MAX_PEER_NUM);
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

void free_document (struct document *D) {
  if (!D->access_hash) { return; }
  free (D->caption);
  free (D->mime_type);
  free_photo_size (&D->thumb);
}

void free_message_media (struct message_media *M) {
  switch (M->type) {
  case CODE_message_media_empty:
  case CODE_message_media_geo:
  case CODE_message_media_audio:
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
  case CODE_message_media_document:
    free_document (&M->document);
    return;
  case CODE_message_media_unsupported:
    free (M->data);
    return;
  case CODE_decrypted_message_media_photo:
  case CODE_decrypted_message_media_video:
  case CODE_decrypted_message_media_audio:
  case CODE_decrypted_message_media_document:
    free (M->encr_photo.key);
    free (M->encr_photo.iv);
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
    P = talloc0 (sizeof (*P));
    P->id = id;
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
    assert (peer_num < MAX_PEER_NUM);
    Peers[peer_num ++] = P;
  }
  if (!P->last) {
    P->last = M;
    M->prev = M->next = 0;
  } else {
    if (get_peer_type (P->id) != PEER_ENCR_CHAT) {
      struct message *N = P->last;
      struct message *NP = 0;
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
      struct message *N = P->last;
      struct message *NP = 0;
      M->next = N;
      M->prev = NP;
      if (N) { N->prev = M; }
      if (NP) { NP->next = M; }
      else { P->last = M; }
    }
  }
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
  int data[2];
  prefetch_data (data, 8);
  struct message *M = message_get (data[1]);

  if (!M) {
    M = talloc0 (sizeof (*M));
    M->id = data[1];
    message_insert_tree (M);
    messages_allocated ++;
  }
  fetch_message (M);
  return M;
}

struct message *fetch_alloc_geo_message (void) {
  struct message *M = talloc (sizeof (*M));
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
  int data[3];
  prefetch_data (data, 12);
  struct message *M = message_get (*(long long *)(data + 1));

  if (!M) {
    M = talloc0 (sizeof (*M));
    M->id = *(long long *)(data + 1);
    message_insert_tree (M);
    messages_allocated ++;
    assert (message_get (M->id) == M);
    logprintf ("id = %lld\n", M->id);
  }
  fetch_encrypted_message (M);
  return M;
}

struct message *fetch_alloc_message_short (void) {
  int data[1];
  prefetch_data (data, 4);
  struct message *M = message_get (data[0]);

  if (!M) {
    M = talloc0 (sizeof (*M));
    M->id = data[0];
    message_insert_tree (M);
    messages_allocated ++;
  }
  fetch_message_short (M);
  return M;
}

struct message *fetch_alloc_message_short_chat (void) {
  int data[1];
  prefetch_data (data, 4);
  struct message *M = message_get (data[0]);

  if (!M) {
    M = talloc0 (sizeof (*M));
    M->id = data[0];
    message_insert_tree (M);
    messages_allocated ++;
  }
  fetch_message_short_chat (M);
  return M;
}

struct chat *fetch_alloc_chat (void) {
  int data[2];
  prefetch_data (data, 8);
  peer_t *U = user_chat_get (MK_CHAT (data[1]));
  if (!U) {
    chats_allocated ++;
    U = talloc0 (sizeof (*U));
    U->id = MK_CHAT (data[1]);
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    assert (peer_num < MAX_PEER_NUM);
    Peers[peer_num ++] = U;
  }
  fetch_chat (&U->chat);
  return &U->chat;
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
    U = talloc0 (sizeof (*U));
    U->id = MK_CHAT (data[2]);
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    fetch_chat_full (&U->chat);
    assert (peer_num < MAX_PEER_NUM);
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
  static peer_t U;
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

void message_insert_tree (struct message *M) {
  assert (M->id);
  message_tree = tree_insert_message (message_tree, M, lrand48 ());
}

void message_remove_tree (struct message *M) {
  assert (M->id);
  message_tree = tree_delete_message (message_tree, M);
}

void message_insert (struct message *M) {
  message_add_use (M);
  message_add_peer (M);
}

void message_insert_unsent (struct message *M) {
  message_unsent_tree = tree_insert_message (message_unsent_tree, M, lrand48 ());
}

void message_remove_unsent (struct message *M) {
  message_unsent_tree = tree_delete_message (message_unsent_tree, M);
}

void __send_msg (struct message *M) {
  logprintf ("Resending message...\n");
  print_message (M);

  do_send_msg (M);
}

void send_all_unsent (void ) {
  tree_act_message (message_unsent_tree, __send_msg);
}
