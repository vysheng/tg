#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "binlog.h"
#include "mtproto-common.h"
#include "net.h"
#include "include.h"
#include "mtproto-client.h"

#include <openssl/sha.h>

#define BINLOG_BUFFER_SIZE (1 << 20)
int binlog_buffer[BINLOG_BUFFER_SIZE];
int *rptr;
int *wptr;
extern int test_dc;

extern int pts;
extern int qts;
extern int last_date;
extern int seq;

#define MAX_LOG_EVENT_SIZE (1 << 17)

char *get_binlog_file_name (void);
extern struct dc *DC_list[];
extern struct dc *DC_working;
extern int dc_working_num;
extern int our_id;
extern int binlog_enabled;
extern int encr_root;
extern unsigned char *encr_prime;
extern int encr_param_version;

int in_replay_log;

void *alloc_log_event (int l UU) {
  return binlog_buffer;
}

long long binlog_pos;

void replay_log_event (void) {
  int *start = rptr;
  in_replay_log = 1;
  assert (rptr < wptr);
  int op = *rptr;

  in_ptr = rptr;
  in_end = wptr;
  if (verbosity >= 2) {
    logprintf ("event = 0x%08x. pos = %lld\n", op, binlog_pos);
  }
  switch (op) {
  case LOG_START:
    rptr ++;
    break;
  case CODE_binlog_dc_option:
    in_ptr ++;
    {
      int id = fetch_int ();
      int l1 = prefetch_strlen ();
      char *name = fetch_str (l1);
      int l2 = prefetch_strlen ();
      char *ip = fetch_str (l2);
      int port = fetch_int ();
      if (verbosity) {
        logprintf ( "id = %d, name = %.*s ip = %.*s port = %d\n", id, l1, name, l2, ip, port);
      }
      alloc_dc (id, strndup (ip, l2), port);
    }
    rptr = in_ptr;
    break;
  case LOG_AUTH_KEY:
    rptr ++;
    {
      int num = *(rptr ++);
      assert (num >= 0 && num <= MAX_DC_ID);
      assert (DC_list[num]);
      DC_list[num]->auth_key_id = *(long long *)rptr;
      rptr += 2;
      memcpy (DC_list[num]->auth_key, rptr, 256);
      rptr += 64;
      DC_list[num]->flags |= 1;
    };
    break;
  case LOG_DEFAULT_DC:
    rptr ++;
    { 
      int num = *(rptr ++);
      assert (num >= 0 && num <= MAX_DC_ID);
      DC_working = DC_list[num];
      dc_working_num = num;
    }
    break;
  case LOG_OUR_ID:
    rptr ++;
    {
      our_id = *(rptr ++);
    }
    break;
  case LOG_DC_SIGNED:
    rptr ++;
    {
      int num = *(rptr ++);
      assert (num >= 0 && num <= MAX_DC_ID);
      assert (DC_list[num]);
      DC_list[num]->has_auth = 1;
    }
    break;
  case LOG_DC_SALT:
    rptr ++;
    {
      int num = *(rptr ++);
      assert (num >= 0 && num <= MAX_DC_ID);
      assert (DC_list[num]);
      DC_list[num]->server_salt = *(long long *)rptr;
      rptr += 2;
    };
    break;
/*  case CODE_user_empty:
  case CODE_user_self:
  case CODE_user_contact:
  case CODE_user_request:
  case CODE_user_foreign:
  case CODE_user_deleted:
    fetch_alloc_user ();
    rptr = in_ptr;
    break;*/
  case LOG_DH_CONFIG:
    get_dh_config_on_answer (0);
    rptr = in_ptr;
    break;
  case LOG_ENCR_CHAT_KEY:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      struct secret_chat *U = (void *)user_chat_get (id);
      assert (U);
      U->key_fingerprint = *(long long *)rptr;
      rptr += 2;
      memcpy (U->key, rptr, 256);
      rptr += 64;
    };
    break;
  case LOG_ENCR_CHAT_SEND_ACCEPT:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      struct secret_chat *U = (void *)user_chat_get (id);
      assert (U);
      U->key_fingerprint = *(long long *)rptr;
      rptr += 2;
      memcpy (U->key, rptr, 256);
      rptr += 64;
      if (!U->g_key) {
        U->g_key = malloc (256);
      }
      memcpy (U->g_key, rptr, 256);
      rptr += 64;
    };
    break;
  case LOG_ENCR_CHAT_SEND_CREATE:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      struct secret_chat *U = (void *)user_chat_get (id);
      assert (!U || !(U->flags & FLAG_CREATED));
      if (!U) {
        U = malloc (sizeof (peer_t));
        memset (U, 0, sizeof (peer_t));
        U->id = id;
        insert_encrypted_chat ((void *)U);
      }
      U->flags |= FLAG_CREATED;
      U->user_id = *(rptr ++);
      memcpy (U->key, rptr, 256);
      rptr += 64;
      if (!U->print_name) {  
        peer_t *P = user_chat_get (MK_USER (U->user_id));
        if (P) {
          U->print_name = create_print_name (U->id, "!", P->user.first_name, P->user.last_name, 0);
        } else {
          static char buf[100];
          sprintf (buf, "user#%d", U->user_id);
          U->print_name = create_print_name (U->id, "!", buf, 0, 0);
        }
      }
    };
    break;
  case LOG_ENCR_CHAT_DELETED:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      struct secret_chat *U = (void *)user_chat_get (id);
      if (!U) {
        U = malloc (sizeof (peer_t));
        memset (U, 0, sizeof (peer_t));
        U->id = id;
        insert_encrypted_chat ((void *)U);
      }
      U->flags |= FLAG_CREATED;
      U->state = sc_deleted;
    };
    break;
  case LOG_ENCR_CHAT_WAITING:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      struct secret_chat *U = (void *)user_chat_get (id);
      assert (U);
      U->state = sc_waiting;
      U->date = *(rptr ++);
      U->admin_id = *(rptr ++);
      U->user_id = *(rptr ++);
      U->access_hash = *(long long *)rptr;
      rptr += 2;
    };
    break;
  case LOG_ENCR_CHAT_REQUESTED:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      struct secret_chat *U = (void *)user_chat_get (id);
      if (!U) {
        U = malloc (sizeof (peer_t));
        memset (U, 0, sizeof (peer_t));
        U->id = id;
        insert_encrypted_chat ((void *)U);
      }
      U->flags |= FLAG_CREATED;
      U->state = sc_request;
      U->date = *(rptr ++);
      U->admin_id = *(rptr ++);
      U->user_id = *(rptr ++);
      U->access_hash = *(long long *)rptr;
      if (!U->print_name) {  
        peer_t *P = user_chat_get (MK_USER (U->user_id));
        if (P) {
          U->print_name = create_print_name (U->id, "!", P->user.first_name, P->user.last_name, 0);
        } else {
          static char buf[100];
          sprintf (buf, "user#%d", U->user_id);
          U->print_name = create_print_name (U->id, "!", buf, 0, 0);
        }
      }
      rptr += 2;
    };
    break;
  case LOG_ENCR_CHAT_OK:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      struct secret_chat *U = (void *)user_chat_get (id);
      assert (U);
      U->state = sc_ok;
    }
    break;
  case CODE_binlog_new_user:
    in_ptr ++;
    {
      peer_id_t id = MK_USER (fetch_int ());
      peer_t *_U = user_chat_get (id);
      if (!_U) {
        _U = malloc (sizeof (*_U));
        memset (_U, 0, sizeof (*_U));
        _U->id = id;
        insert_user (_U);
      } else {
        assert (!(_U->flags & FLAG_CREATED));
      }
      struct user *U = (void *)_U;
      U->flags |= FLAG_CREATED;
      if (get_peer_id (id) == our_id) {
        U->flags |= FLAG_USER_SELF;
      }
      U->first_name = fetch_str_dup ();
      U->last_name = fetch_str_dup ();
      U->print_name = create_print_name (U->id, U->first_name, U->last_name, 0, 0);
      U->access_hash = fetch_long ();
      U->phone = fetch_str_dup ();
      if (fetch_int ()) {
        U->flags |= FLAG_USER_CONTACT;
      }
    }
    rptr = in_ptr;
    break;
  case CODE_binlog_user_delete:
    rptr ++;
    {
      peer_id_t id = MK_USER (*(rptr ++));
      peer_t *U = user_chat_get (id);
      assert (U);
      U->flags |= FLAG_DELETED;
    }
    break;
  case CODE_binlog_set_user_access_token:
    rptr ++;
    {
      peer_id_t id = MK_USER (*(rptr ++));
      peer_t *U = user_chat_get (id);
      assert (U);
      U->user.access_hash = *(long long *)rptr;
      rptr += 2;
    }
    break;
  case CODE_binlog_set_user_phone:
    in_ptr ++;
    {
      peer_id_t id = MK_USER (fetch_int ());
      peer_t *U = user_chat_get (id);
      assert (U);
      if (U->user.phone) { free (U->user.phone); }
      U->user.phone = fetch_str_dup ();
    }
    rptr = in_ptr;
    break;
  case CODE_binlog_set_user_friend:
    rptr ++;
    {
      peer_id_t id = MK_USER (*(rptr ++));
      peer_t *U = user_chat_get (id);
      assert (U);
      int friend = *(rptr ++);
      if (friend) { U->flags |= FLAG_USER_CONTACT; }
      else { U->flags &= ~FLAG_USER_CONTACT; }
    }
    break;
  case CODE_binlog_user_full_photo:
    in_ptr ++;
    {
      peer_id_t id = MK_USER (fetch_int ());
      peer_t *U = user_chat_get (id);
      assert (U);
      if (U->flags & FLAG_HAS_PHOTO) {
        free_photo (&U->user.photo);
      }
      fetch_photo (&U->user.photo);
    }
    rptr = in_ptr;
    break;
  case CODE_binlog_user_blocked:
    rptr ++;
    {
      peer_id_t id = MK_USER (*(rptr ++));
      peer_t *U = user_chat_get (id);
      assert (U);
      U->user.blocked = *(rptr ++);
    }
    break;
  case CODE_binlog_set_user_full_name:
    in_ptr ++;
    {
      peer_id_t id = MK_USER (fetch_int ());
      peer_t *U = user_chat_get (id);
      assert (U);
      if (U->user.real_first_name) { free (U->user.real_first_name); }
      if (U->user.real_last_name) { free (U->user.real_last_name); }
      U->user.real_first_name = fetch_str_dup ();
      U->user.real_last_name = fetch_str_dup ();
    }
    rptr = in_ptr;
    break;
  case CODE_binlog_encr_chat_delete:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      peer_t *_U = user_chat_get (id);
      assert (_U);
      struct secret_chat *U = &_U->encr_chat;
      memset (U->key, 0, sizeof (U->key));
      U->flags |= FLAG_DELETED;
      U->state = sc_deleted;
      if (U->nonce) {
        memset (U->nonce, 0, 256);
        free (U->nonce);
        U->nonce = 0;
      }
      if (U->g_key) {
        memset (U->g_key, 0, 256);
        free (U->g_key);
        U->g_key = 0;
      }
    }
    break;
  case CODE_binlog_encr_chat_requested:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      peer_t *_U = user_chat_get (id);
      if (!_U) {
        _U = malloc (sizeof (*_U));
        memset (_U, 0, sizeof (*_U));
        _U->id = id;
        insert_encrypted_chat (_U);
      } else {
        assert (!(_U->flags & FLAG_CREATED));
      }
      struct secret_chat *U = (void *)_U;
      U->access_hash = *(long long *)rptr;
      rptr += 2;
      U->date = *(rptr ++);
      U->admin_id = *(rptr ++);
      U->user_id = *(rptr ++);

      peer_t *Us = user_chat_get (MK_USER (U->user_id));
      if (Us) {
        U->print_name = create_print_name (id, "!", Us->user.first_name, Us->user.last_name, 0);
      } else {
        static char buf[20];
        sprintf (buf, "user#%d", U->user_id);
        U->print_name = create_print_name (id, "!", buf, 0, 0);
      }
      U->g_key = malloc (256);
      U->nonce = malloc (256);
      memcpy (U->g_key, rptr, 256);
      rptr += 64;
      memcpy (U->nonce, rptr, 256);
      rptr += 64;

      U->flags |= FLAG_CREATED;
      U->state = sc_request;
    }
    break;
  case CODE_binlog_set_encr_chat_access_hash:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      peer_t *U = user_chat_get (id);
      assert (U);
      U->encr_chat.access_hash = *(long long *)rptr;
      rptr += 2;
    }
    break;
  case CODE_binlog_set_encr_chat_date:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      peer_t *U = user_chat_get (id);
      assert (U);
      U->encr_chat.date = *(rptr ++);
    }
    break;
  case CODE_binlog_set_encr_chat_state:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      peer_t *U = user_chat_get (id);
      assert (U);
      U->encr_chat.state = *(rptr ++);
    }
    break;
  case CODE_binlog_encr_chat_accepted:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      peer_t *_U = user_chat_get (id);
      assert (_U);
      struct secret_chat *U = &_U->encr_chat;
      if (!U->g_key) {
        U->g_key = malloc (256);
      }
      if (!U->nonce) {
        U->nonce = malloc (256);
      }
      memcpy (U->g_key, rptr, 256);
      rptr += 64;
      memcpy (U->nonce, rptr, 256);
      rptr += 64;
      U->key_fingerprint = *(long long *)rptr;
      rptr += 2;
      if (U->state == sc_waiting) {
        do_create_keys_end (U);
      }
      U->state = sc_ok;
    }
    break;
  case CODE_binlog_set_encr_chat_key:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      peer_t *_U = user_chat_get (id);
      assert (_U);
      struct secret_chat *U = &_U->encr_chat;
      memcpy (U->key, rptr, 256);
      rptr += 64;
      U->key_fingerprint = *(long long *)rptr;
      rptr += 2;
    }
    break;
  case CODE_binlog_set_dh_params:
    rptr ++;
    {
      if (encr_prime) { free (encr_prime); }
      encr_root = *(rptr ++);
      encr_prime = malloc (256);
      memcpy (encr_prime, rptr, 256);
      rptr += 64;
      encr_param_version = *(rptr ++);
    }
    break;
  case CODE_binlog_encr_chat_init:
    rptr ++;
    {
      peer_t *P = malloc (sizeof (*P));
      memset (P, 0, sizeof (*P));
      P->id = MK_ENCR_CHAT (*(rptr ++));
      assert (!user_chat_get (P->id));
      P->encr_chat.user_id = *(rptr ++);
      P->encr_chat.admin_id = our_id;
      insert_encrypted_chat (P);
      peer_t *Us = user_chat_get (MK_USER (P->encr_chat.user_id));
      assert (Us);
      P->print_name = create_print_name (P->id, "!", Us->user.first_name, Us->user.last_name, 0);
      memcpy (P->encr_chat.key, rptr, 256);
      rptr += 64;
      P->encr_chat.g_key = malloc (256);
      memcpy (P->encr_chat.g_key, rptr, 256);
      rptr += 64;
      P->flags |= FLAG_CREATED;
    }
    break;
  case CODE_binlog_set_pts:
    rptr ++;
    pts = *(rptr ++);
    break;
  case CODE_binlog_set_qts:
    rptr ++;
    qts = *(rptr ++);
    break;
  case CODE_binlog_set_date:
    rptr ++;
    last_date = *(rptr ++);
    break;
  case CODE_binlog_set_seq:
    rptr ++;
    seq = *(rptr ++);
    break;
  case CODE_update_user_photo:
  case CODE_update_user_name:
    work_update_binlog ();
    rptr = in_ptr;
    break;
  default:
    logprintf ("Unknown logevent [0x%08x] 0x%08x [0x%08x]\n", *(rptr - 1), op, *(rptr + 1));

    assert (0);
  }
  if (verbosity >= 2) {
    logprintf ("Event end\n");
  }
  in_replay_log = 0;
  binlog_pos += (rptr - start) * 4;
}

void create_new_binlog (void) {
  static int s[1000];
  packet_ptr = s;
  out_int (LOG_START);
  out_int (CODE_binlog_dc_option);
  out_int (1);
  out_string ("");
  out_string (test_dc ? TG_SERVER_TEST : TG_SERVER);
  out_int (443);
  out_int (LOG_DEFAULT_DC);
  out_int (1);
  
  int fd = open (get_binlog_file_name (), O_WRONLY | O_EXCL | O_CREAT, 0600);
  if (fd < 0) {
    perror ("Write new binlog");
    exit (2);
  }
  assert (write (fd, s, (packet_ptr - s) * 4) == (packet_ptr - s) * 4);
  close (fd);
}


void replay_log (void) {
  if (access (get_binlog_file_name (), F_OK) < 0) {
    printf ("No binlog found. Creating new one\n");
    create_new_binlog ();
  }
  int fd = open (get_binlog_file_name (), O_RDONLY);
  if (fd < 0) {
    perror ("binlog open");
    exit (2);
  }
  int end = 0;
  while (1) {
    if (!end && wptr - rptr < MAX_LOG_EVENT_SIZE / 4) {
      if (wptr == rptr) {
        wptr = rptr = binlog_buffer;
      } else {
        int x = wptr - rptr;
        memcpy (binlog_buffer, rptr, 4 * x);
        wptr -= x;
        rptr -= x;
      }
      int l = (binlog_buffer + BINLOG_BUFFER_SIZE - wptr) * 4;
      int k = read (fd, wptr, l);
      if (k < 0) {
        perror ("read binlog");
        exit (2);
      }
      assert (!(k & 3));
      if (k < l) { 
        end = 1;
      }
      wptr += (k / 4);
    }
    if (wptr == rptr) { break; }
    replay_log_event ();
  }
  close (fd);
}

int binlog_fd;
void write_binlog (void) {
  binlog_fd = open (get_binlog_file_name (), O_WRONLY);
  if (binlog_fd < 0) {
    perror ("binlog open");
    exit (2);
  }
  lseek (binlog_fd, 0, SEEK_END);
}

void add_log_event (const int *data, int len) {
  if (verbosity) {
    logprintf ("Add log event: magic = 0x%08x, len = %d\n", data[0], len);
  }
  assert (!(len & 3));
  if (in_replay_log) { return; }
  rptr = (void *)data;
  wptr = rptr + (len / 4);
  int *in = in_ptr;
  int *end = in_end;
  replay_log_event ();
  if (rptr != wptr) {
    logprintf ("Unread %ld ints. Len = %d\n", wptr - rptr, len);
    assert (rptr == wptr);
  }
  if (binlog_enabled) {
    assert (write (binlog_fd, data, len) == len);
  }
  in_ptr = in;
  in_end = end;
}

void bl_do_set_auth_key_id (int num, unsigned char *buf) {
  static unsigned char sha1_buffer[20];
  SHA1 (buf, 256, sha1_buffer);
  long long fingerprint = *(long long *)(sha1_buffer + 12);
  int *ev = alloc_log_event (8 + 8 + 256);
  ev[0] = LOG_AUTH_KEY;
  ev[1] = num;
  *(long long *)(ev + 2) = fingerprint;
  memcpy (ev + 4, buf, 256);
  add_log_event (ev, 8 + 8 + 256);
}

void bl_do_set_our_id (int id) {
  int *ev = alloc_log_event (8);
  ev[0] = LOG_OUR_ID;
  ev[1] = id;
  add_log_event (ev, 8);
}

void bl_do_new_user (int id, const char *f, int fl, const char *l, int ll, long long access_token, const char *p, int pl, int contact) {
  clear_packet ();
  out_int (CODE_binlog_new_user);
  out_int (id);
  out_cstring (f ? f : "", fl);
  out_cstring (l ? l : "", ll);
  out_long (access_token);
  out_cstring (p ? p : "", pl);
  out_int (contact);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_user_delete (struct user *U) {
  if (U->flags & FLAG_DELETED) { return; }
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_user_delete;
  ev[1] = get_peer_id (U->id);
  add_log_event (ev, 8);
}

extern int last_date;
void bl_do_set_user_profile_photo (struct user *U, long long photo_id, struct file_location *big, struct file_location *small) {
  if (photo_id == U->photo_id) { return; }
  if (!photo_id) {
    int *ev = alloc_log_event (20);
    ev[0] = CODE_update_user_photo;
    ev[1] = get_peer_id (U->id);
    ev[2] = last_date;
    ev[3] = CODE_user_profile_photo_empty;
    ev[4] = CODE_bool_false;
    add_log_event (ev, 20);
  } else {
    clear_packet ();
    out_int (CODE_update_user_photo);
    out_int (get_peer_id (U->id));
    out_int (last_date);
    out_int (CODE_user_profile_photo);
    out_long (photo_id);
    if (small->dc >= 0) {
      out_int (CODE_file_location);
      out_int (small->dc);
      out_long (small->volume);
      out_int (small->local_id);
      out_long (small->secret);
    } else {
      out_int (CODE_file_location_unavailable);
      out_long (small->volume);
      out_int (small->local_id);
      out_long (small->secret);
    }
    if (big->dc >= 0) {
      out_int (CODE_file_location);
      out_int (big->dc);
      out_long (big->volume);
      out_int (big->local_id);
      out_long (big->secret);
    } else {
      out_int (CODE_file_location_unavailable);
      out_long (big->volume);
      out_int (big->local_id);
      out_long (big->secret);
    }
    out_int (CODE_bool_false);
    add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
  }
}

void bl_do_set_user_name (struct user *U, const char *f, int fl, const char *l, int ll) {
  if ((U->first_name && (int)strlen (U->first_name) == fl && !strncmp (U->first_name, f, fl)) && 
      (U->last_name  && (int)strlen (U->last_name)  == ll && !strncmp (U->last_name,  l, ll))) {
    return;
  }
  clear_packet ();
  out_int (CODE_update_user_name);
  out_int (get_peer_id (U->id));
  out_cstring (f, fl);
  out_cstring (l, ll);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_set_user_access_token (struct user *U, long long access_token) {
  if (U->access_hash == access_token) { return; }
  int *ev = alloc_log_event (16);
  ev[0] = CODE_binlog_set_user_access_token;
  ev[1] = get_peer_id (U->id);
  *(long long *)(ev + 2) = access_token;
  add_log_event (ev, 16);
}

void bl_do_set_user_phone (struct user *U, const char *p, int pl) {
  if (U->phone && (int)strlen (U->phone) == pl && !strncmp (U->phone, p, pl)) {
    return;
  }
  clear_packet ();
  out_int (CODE_binlog_set_user_phone);
  out_int (get_peer_id (U->id));
  out_cstring (p, pl);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_set_user_friend (struct user *U, int friend) {
  if (friend == ((U->flags & FLAG_USER_CONTACT) != 0)) { return ; }
  int *ev = alloc_log_event (12);
  ev[0] = CODE_binlog_set_user_friend;
  ev[1] = get_peer_id (U->id);
  ev[2] = friend;
  add_log_event (ev, 12);
}

void bl_do_dc_option (int id, int l1, const char *name, int l2, const char *ip, int port) {
  struct dc *DC = DC_list[id];
  if (DC) { return; }
  
  clear_packet ();
  out_int (CODE_binlog_dc_option);
  out_int (id);
  out_cstring (name, l1);
  out_cstring (ip, l2);
  out_int (port);

  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_dc_signed (int id) {
  int *ev = alloc_log_event (8);
  ev[0] = LOG_DC_SIGNED;
  ev[1] = id;
  add_log_event (ev, 8);
}

void bl_do_set_working_dc (int num) {
  int *ev = alloc_log_event (8);
  ev[0] = LOG_DEFAULT_DC;
  ev[1] = num;
  add_log_event (ev, 8);
}

void bl_do_set_user_full_photo (struct user *U, const int *start, int len) {
  if (U->photo.id == *(long long *)(start + 1)) { return; }
  int *ev = alloc_log_event (len + 8);
  ev[0] = CODE_binlog_user_full_photo;
  ev[1] = get_peer_id (U->id);
  memcpy (ev + 2, start, len);
  add_log_event (ev, len + 8);
}

void bl_do_set_user_blocked (struct user *U, int blocked) {
  if (U->blocked == blocked) { return; }
  int *ev = alloc_log_event (12);
  ev[0] = CODE_binlog_user_blocked;
  ev[1] = get_peer_id (U->id);
  ev[2] = blocked;
  add_log_event (ev, 12);
}

void bl_do_set_user_real_name (struct user *U, const char *f, int fl, const char *l, int ll) {
  if ((U->real_first_name && (int)strlen (U->real_first_name) == fl && !strncmp (U->real_first_name, f, fl)) && 
      (U->real_last_name  && (int)strlen (U->real_last_name)  == ll && !strncmp (U->real_last_name,  l, ll))) {
    return;
  }
  clear_packet ();
  out_int (CODE_binlog_set_user_full_name);
  out_int (get_peer_id (U->id));
  out_cstring (f, fl);
  out_cstring (l, ll);
  add_log_event (packet_buffer, 4 * (packet_ptr - packet_buffer));
}

void bl_do_encr_chat_delete (struct secret_chat *U) {
  if (!(U->flags & FLAG_CREATED) || U->state == sc_deleted || U->state == sc_none) { return; }
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_encr_chat_delete;
  ev[1] = get_peer_id (U->id);
  add_log_event (ev, 8);
}

void bl_do_encr_chat_requested (struct secret_chat *U, long long access_hash, int date, int admin_id, int user_id, unsigned char g_key[], unsigned char nonce[]) {
  int *ev = alloc_log_event (540);
  ev[0] = CODE_binlog_encr_chat_requested;
  ev[1] = get_peer_id (U->id);
  *(long long *)(ev + 2) = access_hash;
  ev[4] = date;
  ev[5] = admin_id;
  ev[6] = user_id;
  memcpy (ev + 7, g_key, 256);
  memcpy (ev + 7 + 64, nonce, 256);
  add_log_event (ev, 540);
}

void bl_do_set_encr_chat_access_hash (struct secret_chat *U, long long access_hash) {
  if (U->access_hash == access_hash) { return; }
  int *ev = alloc_log_event (16);
  ev[0] = CODE_binlog_set_encr_chat_access_hash;
  ev[1] = get_peer_id (U->id);
  *(long long *)(ev + 2) = access_hash;
  add_log_event (ev, 16);
}

void bl_do_set_encr_chat_date (struct secret_chat *U, int date) {
  if (U->date == date) { return; }
  int *ev = alloc_log_event (12);
  ev[0] = CODE_binlog_set_encr_chat_date;
  ev[1] = get_peer_id (U->id);
  ev[2] = date;
  add_log_event (ev, 12);
}

void bl_do_set_encr_chat_state (struct secret_chat *U, enum secret_chat_state state) {
  if (U->state == state) { return; }
  int *ev = alloc_log_event (12);
  ev[0] = CODE_binlog_set_encr_chat_state;
  ev[1] = get_peer_id (U->id);
  ev[2] = state;
  add_log_event (ev, 12);
}

void bl_do_encr_chat_accepted (struct secret_chat *U, const unsigned char g_key[], const unsigned char nonce[], long long key_fingerprint) {
  if (U->state != sc_waiting && U->state != sc_request) { return; }
  int *ev = alloc_log_event (528);
  ev[0] = CODE_binlog_encr_chat_accepted;
  ev[1] = get_peer_id (U->id);
  memcpy (ev + 2, g_key, 256);
  memcpy (ev + 66, nonce, 256);
  *(long long *)(ev + 130) = key_fingerprint;
  add_log_event (ev, 528);
}

void bl_do_set_encr_chat_key (struct secret_chat *E, unsigned char key[], long long key_fingerprint) {
  int *ev = alloc_log_event (272);
  ev[0] = CODE_binlog_set_encr_chat_key;
  ev[1] = get_peer_id (E->id);
  memcpy (ev + 2, key, 256);
  *(long long *)(ev + 66) = key_fingerprint;
  add_log_event (ev, 272);
}

void bl_do_set_dh_params (int root, unsigned char prime[], int version) {
  int *ev = alloc_log_event (268);
  ev[0] = CODE_binlog_set_dh_params;
  ev[1] = root;
  memcpy (ev + 2, prime, 256);
  ev[66] = version;
  add_log_event (ev, 268);
}

void bl_do_encr_chat_init (int id, int user_id, unsigned char random[], unsigned char g_a[]) {
  int *ev = alloc_log_event (524);
  ev[0] = CODE_binlog_encr_chat_init;
  ev[1] = id;
  ev[2] = user_id;
  memcpy (ev + 3, random, 256);
  memcpy (ev + 67, g_a, 256);
  add_log_event (ev, 524);
}

void bl_do_set_pts (int pts) {
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_set_pts;
  ev[1] = pts;
  add_log_event (ev, 8);
}

void bl_do_set_qts (int qts) {
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_set_qts;
  ev[1] = qts;
  add_log_event (ev, 8);
}

void bl_do_set_date (int date) {
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_set_date;
  ev[1] = date;
  add_log_event (ev, 8);
}

void bl_do_set_seq (int seq) {
  int *ev = alloc_log_event (8);
  ev[0] = CODE_binlog_set_seq;
  ev[1] = seq;
  add_log_event (ev, 8);
}
