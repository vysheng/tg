#ifndef __TGL_LAYOUT_H__
#define __TGL_LAYOUT_H__

#define FLAG_MESSAGE_EMPTY 1
#define FLAG_DELETED 2
#define FLAG_FORBIDDEN 4
#define FLAG_HAS_PHOTO 8
#define FLAG_CREATED 16

#define FLAG_USER_SELF 128
#define FLAG_USER_FOREIGN 256
#define FLAG_USER_CONTACT 512
#define FLAG_USER_IN_CONTACT 1024
#define FLAG_USER_OUT_CONTACT 2048

#define FLAG_CHAT_IN_CHAT 128

#define FLAG_ENCRYPTED 4096
#define FLAG_PENDING 8192

#pragma pack(push,4)

typedef struct { int type; int id; } peer_id_t;

struct file_location {
  int dc;
  long long volume;
  int local_id;
  long long secret;
};

struct photo_size {
  char *type;
  struct file_location loc;
  int w;
  int h;
  int size;
  char *data;
};

struct geo {
  double longitude;
  double latitude;
};

struct photo {
  long long id;
  long long access_hash;
  int user_id;
  int date;
  char *caption;
  struct geo geo;
  int sizes_num;
  struct photo_size *sizes;
};

struct encr_photo {
  long long id;
  long long access_hash;
  int dc_id;
  int size;
  int key_fingerprint;

  unsigned char *key;
  unsigned char *iv;
  int w;
  int h;
};

struct encr_video {
  long long id;
  long long access_hash;
  int dc_id;
  int size;
  int key_fingerprint;
  
  unsigned char *key;
  unsigned char *iv;
  int w;
  int h;
  int duration;
  char *mime_type;
};

struct encr_audio {
  long long id;
  long long access_hash;
  int dc_id;
  int size;
  int key_fingerprint;
  
  unsigned char *key;
  unsigned char *iv;
  int duration;
  char *mime_type;
};

struct encr_document {
  long long id;
  long long access_hash;
  int dc_id;
  int size;
  int key_fingerprint;
  
  unsigned char *key;
  unsigned char *iv;
  char *file_name;
  char *mime_type;
};

struct encr_file {
  char *filename;
  unsigned char *key;
  unsigned char *iv;
};


struct user_status {
  int online;
  int when;
};

struct user {
  peer_id_t id;
  int flags;
  struct message *last;
  char *print_name;
  int structure_version;
  struct file_location photo_big;
  struct file_location photo_small;
  long long photo_id;
  struct photo photo;
  char *first_name;
  char *last_name;
  char *phone;
  long long access_hash;
  struct user_status status;
  int blocked;
  char *real_first_name;
  char *real_last_name;
};

struct chat_user {
  int user_id;
  int inviter_id;
  int date;
};

struct chat {
  peer_id_t id;
  int flags;
  struct message *last;
  char *print_title;
  int structure_version;
  struct file_location photo_big;
  struct file_location photo_small;
  struct photo photo;
  char *title;
  int users_num;
  int user_list_size;
  int user_list_version;
  struct chat_user *user_list;
  int date;
  int version;
  int admin_id;
};

enum secret_chat_state {
  sc_none,
  sc_waiting,
  sc_request,
  sc_ok,
  sc_deleted
};

struct secret_chat {
  peer_id_t id;
  int flags;
  struct message *last;
  char *print_name;
  int structure_version;
  struct file_location photo_big;
  struct file_location photo_small;
  struct photo photo;
  int user_id;
  int admin_id;
  int date;
  int ttl;
  int layer;
  long long access_hash;
  unsigned char *g_key;
  unsigned char *nonce;

  enum secret_chat_state state;
  int key[64];
  long long key_fingerprint;
};

typedef union peer {
  struct {
    peer_id_t id;
    int flags;
    struct message *last;
    char *print_name;
    int structure_version;
    struct file_location photo_big;
    struct file_location photo_small;
    struct photo photo;
  };
  struct user user;
  struct chat chat;
  struct secret_chat encr_chat;
} peer_t;

struct video {
  long long id;
  long long access_hash;
  int user_id;
  int date;
  int size;
  int dc_id;
  struct photo_size thumb;
  char *caption;
  int duration;
  int w;
  int h;
  char *mime_type;
};

struct audio {
  long long id;
  long long access_hash;
  int user_id;
  int date;
  int size;
  int dc_id;
  int duration;
  char *mime_type;
};

struct document {
  long long id;
  long long access_hash;
  int user_id;
  int date;
  int size;
  int dc_id;
  struct photo_size thumb;
  char *caption;
  char *mime_type;
};

struct message_action {
  unsigned type;
  union {
    struct {
      char *title;
      int user_num;
      int *users;
    };
    char *new_title;
    struct photo photo;
    int user;
    int ttl;
    int layer;
    int read_cnt;
    int delete_cnt;
    int screenshot_cnt;
  };
};

struct message_media {
  unsigned type;
  union {
    struct photo photo;
    struct video video;
    struct audio audio;
    struct document document;
    struct geo geo;
    struct {
      char *phone;
      char *first_name;
      char *last_name;
      int user_id;
    };
    struct encr_photo encr_photo;
    struct encr_video encr_video;
    struct encr_audio encr_audio;
    struct encr_document encr_document;
    struct encr_file encr_file;
    struct {
      void *data;
      int data_size;
    };
  };
};

struct message {
  struct message *next_use, *prev_use;
  struct message *next, *prev;
  long long id;
  int flags;
  peer_id_t fwd_from_id;
  int fwd_date;
  peer_id_t from_id;
  peer_id_t to_id;
  int out;
  int unread;
  int date;
  int service;
  union {
    struct message_action action;
    struct {
      char *message;
      int message_len;
      struct message_media media;
    };
  };
};
#pragma pack(pop)
#endif
