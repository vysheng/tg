#include <assert.h>
#include "structures.h"
#include "mtproto-common.h"

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
