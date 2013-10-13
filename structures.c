#include <assert.h>
#include "structures.h"
#include "mtproto-common.h"
#include "telegram.h"
#include "tree.h"

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

void fetch_user (struct user *U) {
  memset (U, 0, sizeof (*U));
  unsigned x = fetch_int ();
  assert (x == CODE_user_empty || x == CODE_user_self || x == CODE_user_contact ||  x == CODE_user_request || x == CODE_user_foreign || x == CODE_user_deleted);
  U->id = fetch_int ();
  if (x == CODE_user_empty) {
    U->flags = 1;
    return;
  }
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
    U->flags = 2;
    return;
  }
  if (x == CODE_user_self) {
    U->flags = 4;
  } else {
    U->access_hash = fetch_long ();
  }
  if (x == CODE_user_foreign) {
    U->flags |= 8;
  } else {
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
    U->flags |= 16;
  }
}

void fetch_chat (struct chat *C) {
  memset (C, 0, sizeof (*C));
  unsigned x = fetch_int ();
  assert (x == CODE_chat_empty || x == CODE_chat || x == CODE_chat_forbidden);
  C->id = fetch_int ();
  if (x == CODE_chat_empty) {
    C->flags = 1;
    return;
  }
  if (x == CODE_chat_forbidden) {
    C->flags |= 8;
  }
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
  }
 
}

#define user_cmp(a,b) ((a)->id - (b)->id)

DEFINE_TREE(user,struct user *,user_cmp,0)
struct tree_user *user_tree;

int user_num;
struct user *Users[MAX_USER_NUM];
int users_allocated;

struct user *fetch_alloc_user (void) {
  struct user *U = malloc (sizeof (*U));
  fetch_user (U);
  users_allocated ++;
  struct user *U1 = tree_lookup_user (user_tree, U);
  if (U1) {
    free_user (U1);
    memcpy (U1, U, sizeof (*U));
    free (U);
    users_allocated --;
    return U1;
  } else {
    user_tree = tree_insert_user (user_tree, U, lrand48 ());
    Users[user_num ++] = U;
    return U;
  }
}

void free_user (struct user *U) {
  if (U->first_name) { free (U->first_name); }
  if (U->last_name) { free (U->last_name); }
  if (U->print_name) { free (U->print_name); }
  if (U->phone) { free (U->phone); }
}

int print_stat (char *s, int len) {
  return snprintf (s, len, 
    "user_num\t%d\n"
    "users_allocated\t%d\n",
    user_num,
    users_allocated);
}
