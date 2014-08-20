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

#include <event2/event.h>

#include "interface.h"
#include "telegram.h"
#include "loop.h"
#include "lua-tg.h"
#include "tgl.h"

int verbosity;

extern char *default_username;
extern char *auth_token;
void set_default_username (const char *s);
extern int binlog_enabled;

extern int unknown_user_list_pos;
extern int unknown_user_list[];
int register_mode;
extern int safe_quit;
int queries_num;
extern int sync_from_start;

void got_it (char *line, int len);

static void stdin_read_callback (evutil_socket_t fd, short what, void *arg) {
  if (((long)arg) & 1) {
    rl_callback_read_char ();
  } else {
    char *line = 0;        
    size_t len = 0;
    assert (getline (&line, &len, stdin) >= 0);
    got_it (line, strlen (line));
  }
}
void net_loop (int flags, int (*is_end)(void)) {
  if (verbosity) {
    logprintf ("Starting netloop\n");
  }
  struct event *ev = 0;
  if (flags & 3) {
    ev = event_new (tgl_state.ev_base, 0, EV_READ | EV_PERSIST, stdin_read_callback, (void *)(long)flags);
    event_add (ev, 0);
  }
  while (!is_end || !is_end ()) {

    event_base_loop (tgl_state.ev_base, EVLOOP_ONCE);

    #ifdef USE_LUA
      lua_do_all ();
    #endif
    if (safe_quit && !queries_num) {
      printf ("All done. Exit\n");
      rl_callback_handler_remove ();
      exit (0);
    }
    if (unknown_user_list_pos) {
      int i;
      for (i = 0; i < unknown_user_list_pos; i++) {
        tgl_do_get_user_info (TGL_MK_USER (unknown_user_list[i]), 0, 0, 0);
      }
      unknown_user_list_pos = 0;
    }   
  }

  if (ev) {
    event_free (ev);
  }
  
  if (verbosity) {
    logprintf ("End of netloop\n");
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

int main_loop (void) {
  net_loop (1, 0);
  return 0;
}

struct dc *cur_a_dc;
int is_authorized (void) {
  return tgl_authorized_dc (cur_a_dc);
}

int config_got;

int got_config (void) {
  return config_got;
}

void on_get_config (void *extra, int success) {
  if (!success) {
    logprintf ("Can not get config.\n");
    exit (1);
  }
  config_got = 1;

}

int should_register;
char *hash;
void sign_in_callback (void *extra, int success, int registered, const char *mhash) {
  if (!success) {
    logprintf ("Can not send code\n");
    exit (1);
  }
  should_register = !registered;
  hash = strdup (mhash);
}


int signed_in_ok;

void sign_in_result (void *extra, int success, struct tgl_user *U) {
  if (!success) {
    logprintf ("Can not login\n");
    exit (1);
  }
  signed_in_ok = 1;
}

int signed_in (void) {
  return signed_in_ok;
}

int sent_code (void) {
  return hash != 0;
}

int dc_signed_in (void) {
  return tgl_signed_dc (cur_a_dc);
}

void export_auth_callback (void *DC, int success) {
  if (!success) {
    logprintf ("Can not export auth\n");
    exit (1);
  }
}

int d_got_ok;
void get_difference_callback (void *extra, int success) {
  assert (success);
  d_got_ok = 1;
}

int dgot (void) {
  return d_got_ok;
}

int zero[512];


int readline_active;
int new_dc_num;
int wait_dialog_list;

extern struct tgl_update_callback upd_cb;

int loop (void) {
  //on_start ();
  tgl_set_callback (&upd_cb);
  tgl_init ();

  double t = tglt_get_double_time ();
  logprintf ("replay log start\n");
  tgl_replay_log ();
  logprintf ("replay log end in %lf seconds\n", tglt_get_double_time () - t);
  tgl_reopen_binlog_for_writing ();
  #ifdef USE_LUA
    lua_binlog_end ();
  #endif
  update_prompt ();

  if (!tgl_authorized_dc (tgl_state.DC_working)) {
    cur_a_dc = tgl_state.DC_working;
    tgl_dc_authorize (tgl_state.DC_working);
    net_loop (0, is_authorized);
  }
  
  tgl_do_help_get_config (on_get_config, 0);
  net_loop (0, got_config);

  if (verbosity) {
    logprintf ("DC_info: %d new DC got\n", new_dc_num);
  }

  int i;
  for (i = 0; i <= tgl_state.max_dc_num; i++) if (tgl_state.DC_list[i] && !tgl_authorized_dc (tgl_state.DC_list[i])) {
    cur_a_dc = tgl_state.DC_list[i];
    tgl_dc_authorize (cur_a_dc);
    net_loop (0, is_authorized);
  }

  if (!tgl_signed_dc (tgl_state.DC_working)) {
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
    tgl_do_send_code (default_username, sign_in_callback, 0);
    net_loop (0, sent_code);
    
    logprintf ("%s\n", should_register ? "phone not registered" : "phone registered");
    if (!should_register) {
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
          tgl_do_phone_call (default_username, hash, 0, 0);
          printf ("Calling you! Code: ");
          continue;
        }
        if (tgl_do_send_code_result (default_username, hash, code, sign_in_result, 0) >= 0) {
          break;
        }
        printf ("Invalid code. Try again: ");
        free (code);
      }
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
      printf ("Code from sms (if you did not receive an SMS and want to be called, type \"call\"): ");
      while (1) {
        if (net_getline (&code, &size) == -1) {
          perror ("getline()");
          exit (EXIT_FAILURE);
        }
        if (!strcmp (code, "call")) {
          printf ("You typed \"call\", switching to phone system.\n");
          tgl_do_phone_call (default_username, hash, 0, 0);
          printf ("Calling you! Code: ");
          continue;
        }
        if (tgl_do_send_code_result_auth (default_username, hash, code, first_name, last_name, sign_in_result, 0) >= 0) {
          break;
        }
        printf ("Invalid code. Try again: ");
        free (code);
      }
    }

    net_loop (0, signed_in);    
    //bl_do_dc_signed (tgl_state.DC_working);
  }

  for (i = 0; i <= tgl_state.max_dc_num; i++) if (tgl_state.DC_list[i] && !tgl_signed_dc (tgl_state.DC_list[i])) {
    tgl_do_export_auth (i, export_auth_callback, (void*)(long)tgl_state.DC_list[i]);    
    cur_a_dc = tgl_state.DC_working;
    net_loop (0, dc_signed_in);
  }
  //write_auth_file ();

  fflush (stdout);
  fflush (stderr);

  //read_state_file ();
  //read_secret_chat_file ();

  set_interface_callbacks ();

  tgl_do_get_difference (sync_from_start, get_difference_callback, 0);
  net_loop (0, dgot);
  #ifdef USE_LUA
    lua_diff_end ();
  #endif
  tglm_send_all_unsent ();


  /*tgl_do_get_dialog_list (get_dialogs_callback, 0);
  if (wait_dialog_list) {
    dialog_list_got = 0;
    net_loop (0, dlgot);
  }*/

  return main_loop ();
}

