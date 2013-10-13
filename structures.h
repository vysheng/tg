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
  char *print_name;
  long long access_hash;
  struct file_location photo_big;
  struct file_location photo_small;
  struct user_status status;
};

struct chat {
  int id;
  int flags;
  char *title;
  char *print_title;
  int user_num;
  int date;
  int version;
  struct file_location photo_big;
  struct file_location photo_small;
};

void fetch_file_location (struct file_location *loc);
void fetch_user (struct user *U);
struct user *fetch_alloc_user (void);

void free_user (struct user *U);

int print_stat (char *s, int len);
#endif
