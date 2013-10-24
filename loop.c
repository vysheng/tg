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
#define READLINE_CALLBACKS

#include <assert.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "interface.h"
#include "net.h"
#include "mtproto-client.h"
#include "mtproto-common.h"
#include "queries.h"
#include "telegram.h"

extern char *default_username;
extern char *auth_token;
void set_default_username (const char *s);
int default_dc_num;

extern int unknown_user_list_pos;
extern int unknown_user_list[];

int unread_messages;
void net_loop (int flags, int (*is_end)(void)) {
  while (!is_end ()) {
    struct pollfd fds[101];
    int cc = 0;
    if (flags & 1) {
      fds[0].fd = 0;
      fds[0].events = POLLIN;
      cc ++;
    }

    int x = connections_make_poll_array (fds + cc, 101 - cc) + cc;
    double timer = next_timer_in ();
    if (timer > 1000) { timer = 1000; }
    if (poll (fds, x, timer) < 0) {
      /* resuming from interrupt, so not an error situation,
         this generally happens when you suspend your
         messenger with "C-z" and then "fg". This is allowed "
       */
      if (flags & 1) {
        rl_reset_line_state ();
        rl_forced_update_display ();
      }
      work_timers ();
      continue;
    }
    work_timers ();
    if ((flags & 1) && (fds[0].revents & POLLIN)) {
      unread_messages = 0;
      rl_callback_read_char ();
    }
    connections_poll_result (fds + cc, x - cc);
    if (unknown_user_list_pos) {
      do_get_user_list_info_silent (unknown_user_list_pos, unknown_user_list);
      unknown_user_list_pos = 0;
    }
  }
}

int ret1 (void) { return 0; }

int main_loop (void) {
  net_loop (1, ret1);
  return 0;
}


struct dc *DC_list[MAX_DC_ID + 1];
struct dc *DC_working;
int dc_working_num;
int auth_state;
char *get_auth_key_filename (void);
int zero[512];


void write_dc (int auth_file_fd, struct dc *DC) {
  assert (write (auth_file_fd, &DC->port, 4) == 4);
  int l = strlen (DC->ip);
  assert (write (auth_file_fd, &l, 4) == 4);
  assert (write (auth_file_fd, DC->ip, l) == l);
  if (DC->flags & 1) {
    assert (write (auth_file_fd, &DC->auth_key_id, 8) == 8);
    assert (write (auth_file_fd, DC->auth_key, 256) == 256);
  } else {
    assert (write (auth_file_fd, zero, 256 + 8) == 256 + 8);
  }
 
  assert (write (auth_file_fd, &DC->server_salt, 8) == 8);
}

int our_id;
void write_auth_file (void) {
  int auth_file_fd = open (get_auth_key_filename (), O_CREAT | O_RDWR, S_IRWXU);
  assert (auth_file_fd >= 0);
  int x = DC_SERIALIZED_MAGIC;
  assert (write (auth_file_fd, &x, 4) == 4);
  x = MAX_DC_ID;
  assert (write (auth_file_fd, &x, 4) == 4);
  assert (write (auth_file_fd, &dc_working_num, 4) == 4);
  assert (write (auth_file_fd, &auth_state, 4) == 4);
  int i;
  for (i = 0; i <= MAX_DC_ID; i++) {
    if (DC_list[i]) {
      x = 1;
      assert (write (auth_file_fd, &x, 4) == 4);
      write_dc (auth_file_fd, DC_list[i]);
    } else {
      x = 0;
      assert (write (auth_file_fd, &x, 4) == 4);
    }
  }
  assert (write (auth_file_fd, &our_id, 4) == 4);
  close (auth_file_fd);
}

void read_dc (int auth_file_fd, int id) {
  int port = 0;
  assert (read (auth_file_fd, &port, 4) == 4);
  int l = 0;
  assert (read (auth_file_fd, &l, 4) == 4);
  assert (l >= 0);
  char *ip = malloc (l + 1);
  assert (read (auth_file_fd, ip, l) == l);
  ip[l] = 0;
  struct dc *DC = alloc_dc (id, ip, port);
  assert (read (auth_file_fd, &DC->auth_key_id, 8) == 8);
  assert (read (auth_file_fd, &DC->auth_key, 256) == 256);
  assert (read (auth_file_fd, &DC->server_salt, 8) == 8);
  if (DC->auth_key_id) {
    DC->flags |= 1;
  }
}

void empty_auth_file (void) {
  struct dc *DC = alloc_dc (1, strdup (TG_SERVER), 443);
  assert (DC);
  dc_working_num = 1;
  write_auth_file ();
}

void read_auth_file (void) {
  int auth_file_fd = open (get_auth_key_filename (), O_CREAT | O_RDWR, S_IRWXU);
  if (auth_file_fd < 0) {
    empty_auth_file ();
  }
  assert (auth_file_fd >= 0);
  int x;
  if (read (auth_file_fd, &x, 4) < 4 || x != DC_SERIALIZED_MAGIC) {
    close (auth_file_fd);
    empty_auth_file ();
    return;
  }
  assert (read (auth_file_fd, &x, 4) == 4);
  assert (x >= 0 && x <= MAX_DC_ID);
  assert (read (auth_file_fd, &dc_working_num, 4) == 4);
  assert (read (auth_file_fd, &auth_state, 4) == 4);
  int i;
  for (i = 0; i <= x; i++) {
    int y;
    assert (read (auth_file_fd, &y, 4) == 4);
    if (y) {
      read_dc (auth_file_fd, i);
    }
  }
  int l = read (auth_file_fd, &our_id, 4);
  if (l < 4) {
    assert (!l);
  }
  close (auth_file_fd);
}

int readline_active;
int loop (void) {
  on_start ();
  read_auth_file ();
  assert (DC_list[dc_working_num]);
  DC_working = DC_list[dc_working_num];
  if (!DC_working->auth_key_id) {
    dc_authorize (DC_working);
  } else {
    dc_create_session (DC_working);
  }
  if (!auth_state) {
    if (!default_username) {
      size_t size = 0;
      char *user = 0;

      if (!user && !auth_token) {
        printf ("Telephone number (with '+' sign): ");         
        if (getline (&user, &size, stdin) == -1) {
          perror ("getline()");
          exit (EXIT_FAILURE);
        }
        user[strlen (user) - 1] = 0;      
        set_default_username (user);
      }
    }
    do_send_code (default_username);
    char *code = 0;
    size_t size = 0;
    printf ("Code from sms: ");
    while (1) {
      if (getline (&code, &size, stdin) == -1) {
        perror ("getline()");
        exit (EXIT_FAILURE);
      }
      code[strlen (code) - 1] = 0;      
      if (do_send_code_result (code) >= 0) {
        break;
      }
      printf ("Invalid code. Try again: ");
    }
    auth_state = 1;
  }

  write_auth_file ();
  
  fflush (stdin);
  fflush (stdout);
  fflush (stderr);

  readline_active = 1;
  rl_callback_handler_install (get_default_prompt (), interpreter);
  rl_attempted_completion_function = (CPPFunction *) complete_text;
  rl_completion_entry_function = complete_none;

  do_get_dialog_list ();

  return main_loop ();
}

