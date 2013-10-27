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

char **_s;
size_t *_l;
int got_it_ok;

void got_it (char *line) {
  *_s = strdup (line);
  *_l = strlen (line);
  got_it_ok = 1;
}

int is_got_it (void) {
  return got_it_ok;
}

int net_getline (char **s, size_t *l) {
  got_it_ok = 0;
  _s = s;
  _l = l;
  rl_callback_handler_install (0, got_it);
  net_loop (1, is_got_it);
  printf ("'%s'\n", *s);
  return 0;
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
  assert (write (auth_file_fd, &DC->has_auth, 4) == 4);
}

int our_id;
void write_auth_file (void) {
  int auth_file_fd = open (get_auth_key_filename (), O_CREAT | O_RDWR, 0600);
  assert (auth_file_fd >= 0);
  int x = DC_SERIALIZED_MAGIC_V2;
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

void read_dc (int auth_file_fd, int id, unsigned ver) {
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
  if (ver != DC_SERIALIZED_MAGIC) {
    assert (read (auth_file_fd, &DC->has_auth, 4) == 4);
  } else {
    DC->has_auth = 0;
  }
}

void empty_auth_file (void) {
  struct dc *DC = alloc_dc (1, strdup (TG_SERVER), 443);
  assert (DC);
  dc_working_num = 1;
  auth_state = 0;
  write_auth_file ();
}

int need_dc_list_update;
void read_auth_file (void) {
  int auth_file_fd = open (get_auth_key_filename (), O_CREAT | O_RDWR, 0600);
  if (auth_file_fd < 0) {
    empty_auth_file ();
  }
  assert (auth_file_fd >= 0);
  unsigned x;
  unsigned m;
  if (read (auth_file_fd, &m, 4) < 4 || (m != DC_SERIALIZED_MAGIC && m != DC_SERIALIZED_MAGIC_V2)) {
    close (auth_file_fd);
    empty_auth_file ();
    return;
  }
  assert (read (auth_file_fd, &x, 4) == 4);
  assert (x <= MAX_DC_ID);
  assert (read (auth_file_fd, &dc_working_num, 4) == 4);
  assert (read (auth_file_fd, &auth_state, 4) == 4);
  if (m == DC_SERIALIZED_MAGIC) {
    auth_state = 700;
  }
  int i;
  for (i = 0; i <= (int)x; i++) {
    int y;
    assert (read (auth_file_fd, &y, 4) == 4);
    if (y) {
      read_dc (auth_file_fd, i, m);
    }
  }
  int l = read (auth_file_fd, &our_id, 4);
  if (l < 4) {
    assert (!l);
  }
  close (auth_file_fd);
  DC_working = DC_list[dc_working_num];
  if (m == DC_SERIALIZED_MAGIC) {
    DC_working->has_auth = 1;
  }
}

extern int max_chat_size;
int mcs (void) {
  return max_chat_size;
}

int readline_active;
int new_dc_num;
int loop (void) {
  on_start ();
  read_auth_file ();
  readline_active = 1;
  rl_set_prompt ("");

  assert (DC_list[dc_working_num]);
  if (auth_state == 0) {
    DC_working = DC_list[dc_working_num];
    assert (!DC_working->auth_key_id);
    dc_authorize (DC_working);
    assert (DC_working->auth_key_id);
    auth_state = 100;
    write_auth_file ();
  }
  
  if (verbosity) {
    logprintf ("Requesting info about DC...\n");
  }
  do_help_get_config ();
  net_loop (0, mcs);
  if (verbosity) {
    logprintf ("DC_info: %d new DC got\n", new_dc_num);
  }
  if (new_dc_num) {
    int i;
    for (i = 0; i <= MAX_DC_NUM; i++) if (DC_list[i] && !DC_list[i]->auth_key_id) {
      dc_authorize (DC_list[i]);
      assert (DC_list[i]->auth_key_id);
      write_auth_file ();
    }
  }

  if (auth_state == 100) {
    if (!default_username) {
      size_t size = 0;
      char *user = 0;

      if (!user) {
        printf ("Telephone number (with '+' sign): ");         
        if (net_getline (&user, &size) == -1) {
          perror ("getline()");
          exit (EXIT_FAILURE);
        }
        set_default_username (user);
      }
    }
    int res = do_auth_check_phone (default_username);
    assert (res >= 0);
    logprintf ("%s\n", res > 0 ? "phone registered" : "phone not registered");
    if (res > 0) {
      do_send_code (default_username);
      char *code = 0;
      size_t size = 0;
      printf ("Code from sms: ");
      while (1) {
        if (net_getline (&code, &size) == -1) {
          perror ("getline()");
          exit (EXIT_FAILURE);
        }
        if (do_send_code_result (code) >= 0) {
          break;
        }
        printf ("Invalid code. Try again: ");
        free (code);
      }
      auth_state = 300;
    } else {
      printf ("User is not registered. Do you want to register? [Y/n] ");
      char *code;
      size_t size;
      if (net_getline (&code, &size) == -1) {
        perror ("getline()");
        exit (EXIT_FAILURE);
      }
      if (!*code || *code == 'y') {
        printf ("Ok, starting registartion.\n");
      } else {
        printf ("Then try again\n");
        exit (EXIT_SUCCESS);
      }
      char *first_name;
      printf ("Name: ");
      if (net_getline (&first_name, &size) == -1) {
        perror ("getline()");
        exit (EXIT_FAILURE);
      }
      char *last_name;
      printf ("Name: ");
      if (net_getline (&last_name, &size) == -1) {
        perror ("getline()");
        exit (EXIT_FAILURE);
      }

      int dc_num = do_get_nearest_dc ();
      assert (dc_num >= 0 && dc_num <= MAX_DC_NUM && DC_list[dc_num]);
      dc_working_num = dc_num;
      DC_working = DC_list[dc_working_num];
      
      do_send_code (default_username);
      printf ("Code from sms: ");
      while (1) {
        if (net_getline (&code, &size) == -1) {
          perror ("getline()");
          exit (EXIT_FAILURE);
        }
        if (do_send_code_result_auth (code, first_name, last_name) >= 0) {
          break;
        }
        printf ("Invalid code. Try again: ");
        free (code);
      }
      auth_state = 300;
    }
  }

  int i;
  for (i = 0; i <= MAX_DC_NUM; i++) if (DC_list[i] && !DC_list[i]->has_auth) {
    do_export_auth (i);
    do_import_auth (i);
    DC_list[i]->has_auth = 1;
    write_auth_file ();
  }
  write_auth_file ();

  fflush (stdin);
  fflush (stdout);
  fflush (stderr);

  rl_callback_handler_install (get_default_prompt (), interpreter);
  rl_attempted_completion_function = (CPPFunction *) complete_text;
  rl_completion_entry_function = complete_none;

  do_get_dialog_list ();

  return main_loop ();
}

