#ifndef __STRUCTURES_H__
#define __STRUCTURES_H__


struct file_location {
  int dc;
  long long volume;
  int local_id;
  long long secret;
};

struct user_status {
  int online;
  int when;
};

struct user {
  int id;
  int flags;
  char *first_name;
  char *last_name;
  char *phone;
  long long access_hash;
  struct file_location photo_big;
  struct file_location photo_small;
  struct user_status status;
};

void fetch_file_location (struct file_location *loc);
void fetch_user (struct user *U);
#endif
