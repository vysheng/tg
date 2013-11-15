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

#define BINLOG_BUFFER_SIZE (1 << 20)
int binlog_buffer[BINLOG_BUFFER_SIZE];
int *rptr;
int *wptr;
extern int test_dc;

#define MAX_LOG_EVENT_SIZE (1 << 17)

char *get_binlog_file_name (void);
extern struct dc *DC_list[];
extern struct dc *DC_working;
extern int dc_working_num;
extern int our_id;
extern int binlog_enabled;

int in_replay_log;

void *alloc_log_event (int l UU) {
  return binlog_buffer;
}

void replay_log_event (void) {
  assert (rptr < wptr);
  int op = *rptr;

  in_ptr = rptr;
  in_end = wptr;
  switch (op) {
  case LOG_START:
    rptr ++;
    return;
  case CODE_dc_option:
    fetch_dc_option ();
    rptr = in_ptr;
    return;
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
    return;
  case LOG_DEFAULT_DC:
    rptr ++;
    { 
      int num = *(rptr ++);
      assert (num >= 0 && num <= MAX_DC_ID);
      DC_working = DC_list[num];
      dc_working_num = num;
    }
    return;
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
  case CODE_user_empty:
  case CODE_user_self:
  case CODE_user_contact:
  case CODE_user_request:
  case CODE_user_foreign:
  case CODE_user_deleted:
    fetch_alloc_user ();
    rptr = in_ptr;
    return;
  case LOG_DH_CONFIG:
    get_dh_config_on_answer (0);
    rptr = in_ptr;
    return;
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
    return;
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
    return;
  case LOG_ENCR_CHAT_SEND_CREATE:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      struct secret_chat *U = (void *)user_chat_get (id);
      assert (!U || (U->flags & FLAG_EMPTY));
      if (!U) {
        U = malloc (sizeof (peer_t));
        memset (U, 0, sizeof (peer_t));
        U->id = id;
        insert_encrypted_chat ((void *)U);
      } else {
        U->flags &= ~FLAG_EMPTY;
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
    return;
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
      } else {
        U->flags &= ~FLAG_EMPTY;
      }
      U->flags |= FLAG_CREATED;
      U->state = sc_deleted;
    };
    return;
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
    return;
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
      } else {
        U->flags &= ~FLAG_EMPTY;
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
    return;
  case LOG_ENCR_CHAT_OK:
    rptr ++;
    {
      peer_id_t id = MK_ENCR_CHAT (*(rptr ++));
      struct secret_chat *U = (void *)user_chat_get (id);
      assert (U);
      U->state = sc_ok;
    }
    return;

  default:
    logprintf ("Unknown logevent [0x%08x] 0x%08x [0x%08x]\n", *(rptr - 1), op, *(rptr + 1));

    assert (0);
  }
}

void create_new_binlog (void) {
  static int s[1000];
  packet_ptr = s;
  out_int (LOG_START);
  out_int (CODE_dc_option);
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
  in_replay_log = 1;
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
  in_replay_log = 0;
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
  if (in_replay_log) { return; }
  assert (write (binlog_fd, data, len) == len);
}
