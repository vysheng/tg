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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE
#define READLINE_CALLBACKS

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef READLINE_GNU
#include <readline/readline.h>
#include <readline/history.h>
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "interface.h"
#include "telegram.h"
#include "loop.h"
#include "lua-tg.h"
#include "tgl.h"

extern char *default_username;
extern char *auth_token;
void set_default_username (const char *s);
extern int binlog_enabled;

extern int unknown_user_list_pos;
extern int unknown_user_list[];
int register_mode;
extern int safe_quit;
extern int queries_num;

void got_it (char *line, int len);
void net_loop (int flags, int (*is_end)(void)) {
  while (!is_end ()) {
    struct pollfd fds[101];
    int cc = 0;
    if (flags & 3) {
      fds[0].fd = 0;
      fds[0].events = POLLIN;
      cc ++;
    }

    //write_state_file ();
    int x = connections_make_poll_array (fds + cc, 101 - cc) + cc;
    double timer = next_timer_in ();
    if (timer > 1000) { timer = 1000; }
    if (poll (fds, x, timer) < 0) {
      work_timers ();
      continue;
    }
    work_timers ();
    if ((flags & 3) && (fds[0].revents & POLLIN)) {
      tgl_state.unread_messages = 0;
      if (flags & 1) {
        rl_callback_read_char ();
      } else {
        char *line = 0;        
        size_t len = 0;
        assert (getline (&line, &len, stdin) >= 0);
        got_it (line, strlen (line));
      }
    }
    connections_poll_result (fds + cc, x - cc);
    #ifdef USE_LUA
      lua_do_all ();
    #endif
    if (safe_quit && !queries_num) {
      printf ("All done. Exit\n");
      rl_callback_handler_remove ();
      exit (0);
    }
    if (unknown_user_list_pos) {
      tgl_do_get_user_list_info_silent (unknown_user_list_pos, unknown_user_list);
      unknown_user_list_pos = 0;
    }   
  }
}

char **_s;
size_t *_l;
int got_it_ok;

void got_it (char *line, int len) {
  assert (len > 0);
  line[-- len] = 0; // delete end of line
  *_s = line;
  *_l = len;  
  got_it_ok = 1;
}

int is_got_it (void) {
  return got_it_ok;
}

int net_getline (char **s, size_t *l) {
  fflush (stdout);
//  rl_already_prompted = 1;
  got_it_ok = 0;
  _s = s;
  _l = l;
//  rl_callback_handler_install (0, got_it);
  net_loop (2, is_got_it);
  return 0;
}

int ret1 (void) { return 0; }

int main_loop (void) {
  net_loop (1, ret1);
  return 0;
}

char *get_auth_key_filename (void);
char *get_state_filename (void);
char *get_secret_chat_filename (void);
int zero[512];

extern int max_chat_size;
int mcs (void) {
  return max_chat_size;
}

extern int difference_got;
int dgot (void) {
  return difference_got;
}
int dlgot (void) {
  return dialog_list_got;
}

int readline_active;
int new_dc_num;
int wait_dialog_list;

int loop (void) {
  //on_start ();
  tgl_init ();

  double t = get_double_time ();
  logprintf ("replay log start\n");
  tgl_replay_log ();
  logprintf ("replay log end in %lf seconds\n", get_double_time () - t);
  tgl_reopen_binlog_for_writing ();
  #ifdef USE_LUA
    lua_binlog_end ();
  #endif
  update_prompt ();

  assert (DC_list[dc_working_num]);
  if (!DC_working || !DC_working->auth_key_id) {
//  if (auth_state == 0) {
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
  tgl_do_help_get_config ();
  net_loop (0, mcs);
  if (verbosity) {
    logprintf ("DC_info: %d new DC got\n", new_dc_num);
  }
  int i;
  for (i = 0; i <= MAX_DC_NUM; i++) if (DC_list[i] && !DC_list[i]->auth_key_id) {
    dc_authorize (DC_list[i]);
    assert (DC_list[i]->auth_key_id);
    write_auth_file ();
  }

  if (auth_state == 100 || !(DC_working->has_auth)) {
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
    int res = tgl_do_auth_check_phone (default_username);
    assert (res >= 0);
    logprintf ("%s\n", res > 0 ? "phone registered" : "phone not registered");
    if (res > 0 && !register_mode) {
      tgl_do_send_code (default_username);
      char *code = 0;
      size_t size = 0;
      printf ("Code from sms (if you did not receive an SMS and want to be called, type \"call\"): ");
      while (1) {
        if (net_getline (&code, &size) == -1) {
          perror ("getline()");
          exit (EXIT_FAILURE);
        }
        if (!strcmp (code, "call")) {
          printf ("You typed \"call\", switching to phone system.\n");
          tgl_do_phone_call (default_username);
          printf ("Calling you! Code: ");
          continue;
        }
        if (tgl_do_send_code_result (code) >= 0) {
          break;
        }
        printf ("Invalid code. Try again: ");
        tfree_str (code);
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
      if (!*code || *code == 'y' || *code == 'Y') {
        printf ("Ok, starting registartion.\n");
      } else {
        printf ("Then try again\n");
        exit (EXIT_SUCCESS);
      }
      char *first_name;
      printf ("First name: ");
      if (net_getline (&first_name, &size) == -1) {
        perror ("getline()");
        exit (EXIT_FAILURE);
      }
      char *last_name;
      printf ("Last name: ");
      if (net_getline (&last_name, &size) == -1) {
        perror ("getline()");
        exit (EXIT_FAILURE);
      }

      int dc_num = tgl_do_get_nearest_dc ();
      assert (dc_num >= 0 && dc_num <= MAX_DC_NUM && DC_list[dc_num]);
      dc_working_num = dc_num;
      DC_working = DC_list[dc_working_num];
      
      tgl_do_send_code (default_username);
      printf ("Code from sms (if you did not receive an SMS and want to be called, type \"call\"): ");
      while (1) {
        if (net_getline (&code, &size) == -1) {
          perror ("getline()");
          exit (EXIT_FAILURE);
        }
        if (!strcmp (code, "call")) {
          printf ("You typed \"call\", switching to phone system.\n");
          tgl_do_phone_call (default_username);
          printf ("Calling you! Code: ");
          continue;
        }
        if (tgl_do_send_code_result_auth (code, first_name, last_name) >= 0) {
          break;
        }
        printf ("Invalid code. Try again: ");
        tfree_str (code);
      }
      auth_state = 300;
    }
  }

  for (i = 0; i <= MAX_DC_NUM; i++) if (DC_list[i] && !DC_list[i]->has_auth) {
    tgl_do_export_auth (i);
    tgl_do_import_auth (i);
    bl_do_dc_signed (i);
    write_auth_file ();
  }
  write_auth_file ();

  fflush (stdout);
  fflush (stderr);

  read_state_file ();
  read_secret_chat_file ();

  set_interface_callbacks ();

  tgl_do_get_difference ();
  net_loop (0, dgot);
  #ifdef USE_LUA
    lua_diff_end ();
  #endif
  tglm_send_all_unsent ();


  tgl_do_get_dialog_list ();
  if (wait_dialog_list) {
    dialog_list_got = 0;
    net_loop (0, dlgot);
  }

  return main_loop ();
}

