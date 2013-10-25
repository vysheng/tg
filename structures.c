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
#include "structures.h"
#include "mtproto-common.h"
#include "telegram.h"
#include "tree.h"
#include "loop.h"
int verbosity;

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
void fetch_user (struct user *U) {
  unsigned x = fetch_int ();
  assert (x == CODE_user_empty || x == CODE_user_self || x == CODE_user_contact ||  x == CODE_user_request || x == CODE_user_foreign || x == CODE_user_deleted);
  U->id = fetch_int ();
  U->flags &= ~(FLAG_EMPTY | FLAG_DELETED | FLAG_USER_SELF | FLAG_USER_FOREIGN | FLAG_USER_CONTACT);
  if (x == CODE_user_empty) {
    U->flags |= FLAG_EMPTY;
    return;
  }
  if (x == CODE_user_self) {
    assert (!our_id || (our_id == U->id));
    if (!our_id) {
      our_id = U->id;
      write_auth_file ();
    }
  }
  if (U->first_name) { free (U->first_name); }
  if (U->last_name) { free (U->last_name); }
  if (U->print_name) { free (U->print_name); }
  U->first_name = fetch_str_dup ();
  U->last_name = fetch_str_dup ();
  if (!strlen (U->first_name)) {
    if (!strlen (U->last_name)) {
      U->print_name = strdup ("none");
    } else {
      U->print_name = strdup (U->last_name);
    }
  } else {
    if (!strlen (U->last_name)) {
      U->print_name = strdup (U->first_name);
    } else {
      U->print_name = malloc (strlen (U->first_name) + strlen (U->last_name) + 2);
      sprintf (U->print_name, "%s_%s", U->first_name, U->last_name);
    }
  }
  char *s = U->print_name;
  while (*s) {
    if (*s == ' ') { *s = '_'; }
    s++;
  }
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
  } else {
    if (U->phone) { free (U->phone); }
    U->phone = fetch_str_dup ();
  }
  unsigned y = fetch_int ();
  if (y == CODE_user_profile_photo_empty) {
    U->photo_small.dc = -2;
    U->photo_big.dc = -2;
  } else {
    assert (y == CODE_user_profile_photo);
    fetch_file_location (&U->photo_small);
    fetch_file_location (&U->photo_big);
  }
  fetch_user_status (&U->status);
  if (x == CODE_user_self) {
    assert (fetch_int () == (int)CODE_bool_false);
  }
  if (x == CODE_user_contact) {
    U->flags |= FLAG_USER_CONTACT;
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
  if (x == CODE_contacts_my_link_requested) {
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
  C->id = -fetch_int ();
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
  C->print_title = strdup (C->title);
  char *s = C->print_title;
  while (*s) {
    if (*s == ' ') { *s = '_'; }
    s ++;
  }
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
  assert (x == CODE_peer_notify_settings || x == CODE_peer_notify_settings_empty);
  if (x == CODE_peer_notify_settings) {
    fetch_int (); // mute_until
    int l = prefetch_strlen ();
    fetch_str (l);
    fetch_bool (); // show_previews
    fetch_int (); // peer notify events
  }
}

void fetch_chat_full (struct chat *C) {
  unsigned x = fetch_int ();
  assert (x == CODE_messages_chat_full);
  assert (fetch_int () == CODE_chat_full); 
  C->id = -fetch_int ();
  C->flags &= ~(FLAG_EMPTY | FLAG_DELETED | FLAG_FORBIDDEN | FLAG_CHAT_IN_CHAT);
  x = fetch_int ();
  if (x == CODE_chat_participants) {
    assert (fetch_int () == -C->id);
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
      S->data = fetch_str_dup ();
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
  M->from_id = fetch_int ();
  M->message = fetch_str_dup ();
  fetch_int (); // pts
  M->date = fetch_int ();
  fetch_int (); // seq
  M->media.type = CODE_message_media_empty;
  M->unread = 1;
}

void fetch_message_short_chat (struct message *M) {
  memset (M, 0, sizeof (*M));
  M->id = fetch_int ();
  M->from_id = fetch_int ();
  M->to_id = -fetch_int ();
  M->message = fetch_str_dup ();
  fetch_int (); // pts
  M->date = fetch_int ();
  fetch_int (); // seq
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

int fetch_peer_id (void) {
  unsigned x =fetch_int ();
  if (x == CODE_peer_user) {
    return fetch_int ();
  } else {
    assert (CODE_peer_chat);
    return -fetch_int ();
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
    M->fwd_from_id = fetch_int ();
    M->fwd_date = fetch_int ();
  }
  M->from_id = fetch_int ();
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

#define user_cmp(a,b) ((a)->id - (b)->id)

DEFINE_TREE(peer,union user_chat *,user_cmp,0)
DEFINE_TREE(message,struct message *,user_cmp,0)
struct tree_peer *peer_tree;
struct tree_message *message_tree;

int chat_num;
int user_num;
int users_allocated;
int chats_allocated;
int messages_allocated;
union user_chat *Peers[MAX_USER_NUM];

struct message message_list = {
  .next_use = &message_list,
  .prev_use = &message_list
};

struct user *fetch_alloc_user (void) {
  int data[2];
  prefetch_data (data, 8);
  union user_chat *U = user_chat_get (data[1]);
  if (U) {
    fetch_user (&U->user);
    return &U->user;
  } else {
    users_allocated ++;
    U = malloc (sizeof (*U));
    memset (U, 0, sizeof (*U));
    fetch_user (&U->user);
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    Peers[chat_num + (user_num ++)] = U;
    return &U->user;
  }
}

struct user *fetch_alloc_user_full (void) {
  int data[3];
  prefetch_data (data, 12);
  union user_chat *U = user_chat_get (data[2]);
  if (U) {
    fetch_user_full (&U->user);
    return &U->user;
  } else {
    users_allocated ++;
    U = malloc (sizeof (*U));
    memset (U, 0, sizeof (*U));
    U->id = data[2];
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    fetch_user_full (&U->user);
    Peers[chat_num + (user_num ++ )] = U;
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

struct message *fetch_alloc_message (void) {
  struct message *M = malloc (sizeof (*M));
  fetch_message (M);
  struct message *M1 = tree_lookup_message (message_tree, M);
  messages_allocated ++;
  if (M1) {
    message_del_use (M1);
    free_message (M1);
    memcpy (M1, M, sizeof (*M));
    free (M);
    message_add_use (M1);
    messages_allocated --;
    return M1;
  } else {
    message_add_use (M);
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
    free_message (M1);
    memcpy (M1, M, sizeof (*M));
    free (M);
    message_add_use (M1);
    messages_allocated --;
    return M1;
  } else {
    message_add_use (M);
    message_tree = tree_insert_message (message_tree, M, lrand48 ());
    return M;
  }
}

struct message *fetch_alloc_message_short_chat (void) {
  struct message *M = malloc (sizeof (*M));
  fetch_message_short_chat (M);
  if (verbosity >= 2) {
    logprintf ("Read message with id %d\n", M->id);
  }
  struct message *M1 = tree_lookup_message (message_tree, M);
  messages_allocated ++;
  if (M1) {
    message_del_use (M1);
    free_message (M1);
    memcpy (M1, M, sizeof (*M));
    free (M);
    message_add_use (M1);
    messages_allocated --;
    return M1;
  } else {
    message_add_use (M);
    message_tree = tree_insert_message (message_tree, M, lrand48 ());
    return M;
  }
}

struct chat *fetch_alloc_chat (void) {
  int data[2];
  prefetch_data (data, 8);
  union user_chat *U = user_chat_get (-data[1]);
  if (U) {
    fetch_chat (&U->chat);
    return &U->chat;
  } else {
    chats_allocated ++;
    U = malloc (sizeof (*U));
    memset (U, 0, sizeof (*U));
    fetch_chat (&U->chat);
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    Peers[(chat_num ++) + user_num] = U;
    return &U->chat;
  }
}

struct chat *fetch_alloc_chat_full (void) {
  int data[3];
  prefetch_data (data, 12);
  union user_chat *U = user_chat_get (-data[2]);
  if (U) {
    fetch_chat_full (&U->chat);
    return &U->chat;
  } else {
    chats_allocated ++;
    U = malloc (sizeof (*U));
    memset (U, 0, sizeof (*U));
    U->id = -data[2];
    peer_tree = tree_insert_peer (peer_tree, U, lrand48 ());
    fetch_chat_full (&U->chat);
    Peers[(chat_num ++) + user_num] = U;
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
    "messages_allocated\t%d\n",
    users_allocated,
    chats_allocated,
    messages_allocated
    );
}

union user_chat *user_chat_get (int id) {
  union user_chat U;
  U.id = id;
  return tree_lookup_peer (peer_tree, &U);
}

struct message *message_get (int id) {
  struct message M;
  M.id = id;
  return tree_lookup_message (message_tree, &M);
}

void update_message_id (struct message *M, int id) {
  message_tree = tree_delete_message (message_tree, M);
  M->id = id;
  message_tree = tree_insert_message (message_tree, M, lrand48 ());
}

void message_insert (struct message *M) {
  message_add_use (M);
  message_tree = tree_insert_message (message_tree, M, lrand48 ());
}
