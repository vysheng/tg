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

#define LOG_START 0x8948329a
#define LOG_AUTH_KEY 0x984932aa
#define LOG_DEFAULT_DC 0x95382908

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
  }
}

void create_new_binlog (void) {
  static int s[1000];
  packet_ptr = s;
  out_int (LOG_START);
  out_int (CODE_dc_option);
  out_int (0);
  out_string ("");
  out_string (test_dc ? TG_SERVER_TEST : TG_SERVER);
  out_int (443);
  
  int fd = open (get_binlog_file_name (), O_WRONLY | O_EXCL);
  assert (write (fd, s, (packet_ptr - s) * 4) == (packet_ptr - s) * 4);
  close (fd);
}


void replay_log (void) {
  if (access (get_binlog_file_name (), F_OK) < 0) {
    printf ("No binlog found. Creating new one");
    create_new_binlog ();
  }
  int fd = open (get_binlog_file_name (), O_RDONLY);
  if (fd < 0) {
    perror ("binlog open");
    exit (2);
  }
  int end;
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
