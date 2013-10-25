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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <termios.h>
#include <unistd.h>
#include <assert.h>
#include <readline/readline.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <execinfo.h>
#include <signal.h>

#include "loop.h"
#include "mtproto-client.h"

#define PROGNAME "telegram-client"
#define VERSION "0.01"

#define CONFIG_DIRECTORY ".telegram/"
#define CONFIG_FILE CONFIG_DIRECTORY "config"
#define AUTH_KEY_FILE CONFIG_DIRECTORY "auth"
#define DOWNLOADS_DIRECTORY "downloads/"

#define CONFIG_DIRECTORY_MODE 0700

#define DEFAULT_CONFIG_CONTENTS     \
  "# This is an empty config file\n" \
  "# Feel free to put something here\n"

char *default_username;
int setup_mode;
char *auth_token;

void set_default_username (const char *s) {
  if (default_username) { 
    free (default_username);
  }
  default_username = strdup (s);
}

void set_setup_mode (void) {
  setup_mode = 1;
}


/* {{{ TERMINAL */
tcflag_t old_lflag;
cc_t old_vtime;
struct termios term;

void get_terminal_attributes (void) {
  if (tcgetattr (STDIN_FILENO, &term) < 0) {
    perror ("tcgetattr()");
    exit (EXIT_FAILURE);
  }
  old_lflag = term.c_lflag;
  old_vtime = term.c_cc[VTIME];
}

void set_terminal_attributes (void) {
  if (tcsetattr (STDIN_FILENO, 0, &term) < 0) {
    perror ("tcsetattr()");
    exit (EXIT_FAILURE);
  }
}
/* }}} */

char *get_home_directory (void) {
  struct passwd *current_passwd;
  uid_t user_id;
  setpwent ();
  user_id = getuid ();
  while ((current_passwd = getpwent ())) {
    if (current_passwd->pw_uid == user_id) {
      return current_passwd->pw_dir;
    }
  }
  return 0;
}

char *get_config_directory (void) {
  char *config_directory;
  int length = strlen (get_home_directory ()) + strlen (CONFIG_DIRECTORY) + 2;

  config_directory = (char *) calloc (length, sizeof (char));
  sprintf (config_directory, "%s/" CONFIG_DIRECTORY,
     get_home_directory ());

  return config_directory;
}

char *get_config_filename (void) {
  char *config_filename;
  int length = strlen (get_home_directory ()) + strlen (CONFIG_FILE) + 2;

  config_filename = (char *) calloc (length, sizeof (char));
  sprintf (config_filename, "%s/" CONFIG_FILE, get_home_directory ());
  return config_filename;
}

char *get_auth_key_filename (void) {
  char *auth_key_filename;
  int length = strlen (get_home_directory ()) + strlen (AUTH_KEY_FILE) + 2;

  auth_key_filename = (char *) calloc (length, sizeof (char));
  sprintf (auth_key_filename, "%s/" AUTH_KEY_FILE, get_home_directory ());
  return auth_key_filename;
}

char *get_downloads_directory (void)
{
  char *downloads_directory;
  int length = strlen (get_config_directory ()) + strlen (DOWNLOADS_DIRECTORY) + 2;

  downloads_directory = (char *) calloc (length, sizeof (char));
  sprintf (downloads_directory, "%s/" DOWNLOADS_DIRECTORY, get_config_directory ());

  return downloads_directory;
}

void running_for_first_time (void) {
  struct stat *config_file_stat = NULL;
  int config_file_fd;
  char *config_directory = get_config_directory ();
  char *config_filename = get_config_filename ();
  char *downloads_directory = get_downloads_directory ();

  if (mkdir (config_directory, CONFIG_DIRECTORY_MODE) != 0) {
    return;
  } else {
    printf ("\nRunning " PROGNAME " for first time!!\n");
    printf ("[%s] created\n", config_directory);
  }

  // see if config file is there
  if (stat (config_filename, config_file_stat) != 0) {
    // config file missing, so touch it
    config_file_fd = open (config_filename, O_CREAT | O_RDWR, S_IRWXU);
    if (config_file_fd == -1)  {
      perror ("open[config_file]");
      exit (EXIT_FAILURE);
    }
    if (fchmod (config_file_fd, CONFIG_DIRECTORY_MODE) != 0) {
      perror ("fchmod[" CONFIG_FILE "]");
      exit (EXIT_FAILURE);
    }
    if (write (config_file_fd, DEFAULT_CONFIG_CONTENTS, strlen (DEFAULT_CONFIG_CONTENTS)) <= 0) {
      perror ("write[config_file]");
      exit (EXIT_FAILURE);
    }
    close (config_file_fd);
    int auth_file_fd = open (get_auth_key_filename (), O_CREAT | O_RDWR, S_IRWXU);
    int x = -1;
    assert (write (auth_file_fd, &x, 4) == 4);
    close (auth_file_fd);

    printf ("[%s] created\n", config_filename);
  
    /* create downloads directory */
    if (mkdir (downloads_directory, 0755) !=0) {
      perror ("creating download directory");
      exit (EXIT_FAILURE);
    }
  }

  set_setup_mode ();
}

void inner_main (void) {
  loop ();
}

void usage (void) {
  printf ("%s [-u username]\n", PROGNAME);
  exit (1);
}

extern char *rsa_public_key_name;
extern int verbosity;
extern int default_dc_num;

void args_parse (int argc, char **argv) {
  int opt = 0;
  while ((opt = getopt (argc, argv, "u:hk:vn:")) != -1) {
    switch (opt) {
    case 'u':
      set_default_username (optarg);
      break;
    case 'k':
      rsa_public_key_name = strdup (optarg);
      break;
    case 'v':
      verbosity ++;
      break;
    case 'n':
      default_dc_num = atoi (optarg);
      break;
    case 'h':
    default:
      usage ();
      break;
    }
  }
}

void print_backtrace (void) {
  void *buffer[255];
  const int calls = backtrace (buffer, sizeof (buffer) / sizeof (void *));
  backtrace_symbols_fd (buffer, calls, 1);
  exit(EXIT_FAILURE);
}

void sig_handler (int signum) {
  set_terminal_attributes ();
  printf ("signal %d received\n", signum);
  print_backtrace ();
}


int main (int argc, char **argv) {
  signal (SIGSEGV, sig_handler);
  signal (SIGABRT, sig_handler);
  running_for_first_time ();

  printf (
    "Telegram-client version " TG_VERSION ", Copyright (C) 2013 Vitaly Valtman\n"
    "Telegram-client comes with ABSOLUTELY NO WARRANTY; for details type `show_license'.\n"
    "This is free software, and you are welcome to redistribute it\n"
    "under certain conditions; type `show_license' for details.\n"
  );

  get_terminal_attributes ();

  args_parse (argc, argv);

  inner_main ();
  
  return 0;
}
