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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <termios.h>
#include <unistd.h>
#include <assert.h>
#if (READLINE == GNU)
#include <readline/readline.h>
#else
#include <editline/readline.h>
#endif

#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#include <signal.h>
#ifdef HAVE_LIBCONFIG
#include <libconfig.h>
#endif

#include "telegram.h"
#include "loop.h"
#include "mtproto-client.h"
#include "interface.h"
#include "tools.h"

#ifdef USE_LUA
#  include "lua-tg.h"
#endif

#define PROGNAME "telegram-client"
#define VERSION "0.01"

#define CONFIG_DIRECTORY "." PROG_NAME
#define CONFIG_FILE "config"
#define AUTH_KEY_FILE "auth"
#define STATE_FILE "state"
#define SECRET_CHAT_FILE "secret"
#define DOWNLOADS_DIRECTORY "downloads"
#define BINLOG_FILE "binlog"

#define CONFIG_DIRECTORY_MODE 0700

#define DEFAULT_CONFIG_CONTENTS     \
  "# This is an empty config file\n" \
  "# Feel free to put something here\n"

char *default_username;
char *auth_token;
int msg_num_mode;
char *config_filename;
char *prefix;
int test_dc;
char *auth_file_name;
char *state_file_name;
char *secret_chat_file_name;
char *downloads_directory;
char *config_directory;
char *binlog_file_name;
int binlog_enabled;
int unread_disabled;
extern int log_level;
int sync_from_start;
int allow_weak_random;

void set_default_username (const char *s) {
  if (default_username) { 
    tfree_str (default_username);
  }
  default_username = tstrdup (s);
}


/* {{{ TERMINAL */
static struct termios term_in, term_out;
static int term_set_in;
static int term_set_out;

void get_terminal_attributes (void) {
  if (tcgetattr (STDIN_FILENO, &term_in) < 0) {
  } else {
    term_set_in = 1;
  }
  if (tcgetattr (STDOUT_FILENO, &term_out) < 0) {
  } else {
    term_set_out = 1;
  }
}

void set_terminal_attributes (void) {
  if (term_set_in) {
    if (tcsetattr (STDIN_FILENO, 0, &term_in) < 0) {
      perror ("tcsetattr()");
    }
  }
  if (term_set_out) {
    if (tcsetattr (STDOUT_FILENO, 0, &term_out) < 0) {
      perror ("tcsetattr()");
    }
  }
}
/* }}} */

char *get_home_directory (void) {
  static char *home_directory = NULL;
  if (home_directory != NULL) {
    return home_directory;
  }
  struct passwd *current_passwd;
  uid_t user_id;
  setpwent ();
  user_id = getuid ();
  while ((current_passwd = getpwent ())) {
    if (current_passwd->pw_uid == user_id) {
      home_directory = tstrdup (current_passwd->pw_dir);
      break;
    }
  }
  endpwent ();
  if (home_directory == NULL) {
    home_directory = tstrdup (".");
  }
  return home_directory;
}

char *get_config_directory (void) {
  char *config_directory;
  tasprintf (&config_directory, "%s/" CONFIG_DIRECTORY, get_home_directory ());
  return config_directory;
}

char *get_config_filename (void) {
  return config_filename;
}

char *get_auth_key_filename (void) {
  return auth_file_name;
}

char *get_state_filename (void) {
  return state_file_name;
}

char *get_secret_chat_filename (void) {
  return secret_chat_file_name;
}

char *get_downloads_directory (void) {
  return downloads_directory;
}

char *get_binlog_file_name (void) {
  return binlog_file_name;
}

char *make_full_path (char *s) {
  if (*s != '/') {
    char *t = s;
    tasprintf (&s, "%s/%s", get_home_directory (), s);
    tfree_str (t);
  }
  return s;
}

void check_type_sizes (void) {
  if (sizeof (int) != 4u) {
    logprintf ("sizeof (int) isn't equal 4.\n");
    exit (1);
  }
  if (sizeof (char) != 1u) {
    logprintf ("sizeof (char) isn't equal 1.\n");
    exit (1);
  }
}

void running_for_first_time (void) {
  check_type_sizes ();
  if (config_filename) {
    return; // Do not create custom config file
  }
  tasprintf (&config_filename, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, CONFIG_FILE);
  config_filename = make_full_path (config_filename);

  int config_file_fd;
  char *config_directory = get_config_directory ();
  //char *downloads_directory = get_downloads_directory ();

  if (!mkdir (config_directory, CONFIG_DIRECTORY_MODE)) {
    printf ("[%s] created\n", config_directory);
  }

  tfree_str (config_directory);
  config_directory = NULL;
  // see if config file is there
  if (access (config_filename, R_OK) != 0) {
    // config file missing, so touch it
    config_file_fd = open (config_filename, O_CREAT | O_RDWR, 0600);
    if (config_file_fd == -1)  {
      perror ("open[config_file]");
      exit (EXIT_FAILURE);
    }
    if (write (config_file_fd, DEFAULT_CONFIG_CONTENTS, strlen (DEFAULT_CONFIG_CONTENTS)) <= 0) {
      perror ("write[config_file]");
      exit (EXIT_FAILURE);
    }
    close (config_file_fd);
    /*int auth_file_fd = open (get_auth_key_filename (), O_CREAT | O_RDWR, 0600);
    int x = -1;
    assert (write (auth_file_fd, &x, 4) == 4);
    close (auth_file_fd);

    printf ("[%s] created\n", config_filename);*/
  
    /* create downloads directory */
    /*if (mkdir (downloads_directory, 0755) !=0) {
      perror ("creating download directory");
      exit (EXIT_FAILURE);
    }*/
  }
}

#ifdef HAVE_LIBCONFIG
void parse_config_val (config_t *conf, char **s, char *param_name, const char *default_name, const char *path) {
  static char buf[1000]; 
  int l = 0;
  if (prefix) {
    l = strlen (prefix);
    memcpy (buf, prefix, l);
    buf[l ++] = '.';
  }
  *s = 0;
  const char *r = 0;
  strcpy (buf + l, param_name);
  config_lookup_string (conf, buf, &r);
  if (r) {
    if (path) {
      tasprintf (s, "%s/%s", path, r);
    } else {
      *s = tstrdup (r);
    }
  } else {
    if (path) {
      tasprintf (s, "%s/%s", path, default_name);
    } else {
      *s  = tstrdup (default_name);
    }
  }
}

void parse_config (void) {
  config_filename = make_full_path (config_filename);
  
  config_t conf;
  config_init (&conf);
  if (config_read_file (&conf, config_filename) != CONFIG_TRUE) {
    fprintf (stderr, "Can not read config '%s': error '%s' on the line %d\n", config_filename, config_error_text (&conf), config_error_line (&conf));
    exit (2);
  }

  if (!prefix) {
    config_lookup_string (&conf, "default_profile", (void *)&prefix);
  }

  static char buf[1000];
  int l = 0;
  if (prefix) {
    l = strlen (prefix);
    memcpy (buf, prefix, l);
    buf[l ++] = '.';
  }
  test_dc = 0;
  strcpy (buf + l, "test");
  config_lookup_bool (&conf, buf, &test_dc);
  
  strcpy (buf + l, "log_level");
  long long t = log_level;
  config_lookup_int (&conf, buf, (void *)&t);
  log_level = t;
  
  if (!msg_num_mode) {
    strcpy (buf + l, "msg_num");
    config_lookup_bool (&conf, buf, &msg_num_mode);
  }

  parse_config_val (&conf, &config_directory, "config_directory", CONFIG_DIRECTORY, 0);
  config_directory = make_full_path (config_directory);

  parse_config_val (&conf, &auth_file_name, "auth_file", AUTH_KEY_FILE, config_directory);
  parse_config_val (&conf, &state_file_name, "state_file", STATE_FILE, config_directory);
  parse_config_val (&conf, &secret_chat_file_name, "secret", SECRET_CHAT_FILE, config_directory);
  parse_config_val (&conf, &downloads_directory, "downloads", DOWNLOADS_DIRECTORY, config_directory);
  parse_config_val (&conf, &binlog_file_name, "binlog", BINLOG_FILE, config_directory);
  
  strcpy (buf + l, "binlog_enabled");
  config_lookup_bool (&conf, buf, &binlog_enabled);
  
  if (!mkdir (config_directory, CONFIG_DIRECTORY_MODE)) {
    printf ("[%s] created\n", config_directory);
  }
  if (!mkdir (downloads_directory, CONFIG_DIRECTORY_MODE)) {
    printf ("[%s] created\n", downloads_directory);
  }
}
#else
void parse_config (void) {
  printf ("libconfig not enabled\n");
  tasprintf (&auth_file_name, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, AUTH_KEY_FILE);
  tasprintf (&state_file_name, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, STATE_FILE);
  tasprintf (&secret_chat_file_name, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, SECRET_CHAT_FILE);
  tasprintf (&downloads_directory, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, DOWNLOADS_DIRECTORY);
  tasprintf (&binlog_file_name, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, BINLOG_FILE);
}
#endif

void inner_main (void) {
  loop ();
}

void usage (void) {
  printf ("%s Usage\n", PROGNAME);

  printf (" -h                 this help list\n");
  printf (" -u username        specify username\n");
  printf (" -k public-key      specify server public key\n");
  printf (" -v                 verbose\n");
  printf (" -l [1-3]           log level\n");
  printf (" -L log-file        log net file\n");
  printf (" -N                 message num mode\n");
  printf (" -c config-file     specify config file\n");
  printf (" -p prefix          specify prefix\n");
  printf (" -R                 register mode\n");
  printf (" -f                 sync from start\n");
  printf (" -B                 enable binlog\n");
  printf (" -E                 disable auto accept\n");
  printf (" -w                 allow weak random\n");
  printf (" -s                 specify lua script\n");
  printf (" -W                 wait dialog list\n");
  printf (" -y                 disable unread prompt\n");
  printf ("\n");

  exit (1);
}

extern char *rsa_public_key_name;
extern int verbosity;
extern int default_dc_num;

char *log_net_file;
FILE *log_net_f;

int register_mode;
int disable_auto_accept;
int wait_dialog_list;

char *lua_file;

void args_parse (int argc, char **argv) {
  int opt = 0;
  while ((opt = getopt (argc, argv, "u:hk:vn:Nc:p:l:RfBL:Es:wW:y")) != -1) {
    switch (opt) {
    case 'u':
      set_default_username (optarg);
      break;
    case 'k':
      rsa_public_key_name = tstrdup (optarg);
      break;
    case 'v':
      verbosity ++;
      break;
    case 'N':
      msg_num_mode ++;
      break;
    case 'c':
      config_filename = tstrdup (optarg);
      break;
    case 'p':
      prefix = tstrdup (optarg);
      assert (strlen (prefix) <= 100);
      break;
    case 'l':
      log_level = atoi (optarg);
      break;
    case 'R':
      register_mode = 1;
      break;
    case 'f':
      sync_from_start = 1;
      break;
    case 'B':
      binlog_enabled = 1;
      break;
    case 'y':
      unread_disabled = 1;
      break;
    case 'L':
      if (log_net_file) { 
        usage ();
      }
      log_net_file = tstrdup (optarg);
      log_net_f = fopen (log_net_file, "a");
      assert (log_net_f);
      break;
    case 'E':
      disable_auto_accept = 1;
      break;
    case 'w':
      allow_weak_random = 1;
      break;
    case 's':
      lua_file = tstrdup (optarg);
      break;
    case 'W':
      wait_dialog_list = 1;
      break;
    case 'h':
    default:
      usage ();
      break;
    }
  }
}

#ifdef HAVE_EXECINFO_H
void print_backtrace (void) {
  void *buffer[255];
  const int calls = backtrace (buffer, sizeof (buffer) / sizeof (void *));
  backtrace_symbols_fd (buffer, calls, 1);
}
#else
void print_backtrace (void) {
  if (write (1, "No libexec. Backtrace disabled\n", 32) < 0) {
    // Sad thing
  }
}
#endif

void sig_segv_handler (int signum __attribute__ ((unused))) {
  set_terminal_attributes ();
  if (write (1, "SIGSEGV received\n", 18) < 0) { 
    // Sad thing
  }
  print_backtrace ();
  exit (EXIT_FAILURE);
}

void sig_abrt_handler (int signum __attribute__ ((unused))) {
  set_terminal_attributes ();
  if (write (1, "SIGABRT received\n", 18) < 0) { 
    // Sad thing
  }
  print_backtrace ();
  exit (EXIT_FAILURE);
}

int main (int argc, char **argv) {
  signal (SIGSEGV, sig_segv_handler);
  signal (SIGABRT, sig_abrt_handler);

  log_level = 10;
  
  args_parse (argc, argv);
  printf (
    "Telegram-client version " TG_VERSION ", Copyright (C) 2013 Vitaly Valtman\n"
    "Telegram-client comes with ABSOLUTELY NO WARRANTY; for details type `show_license'.\n"
    "This is free software, and you are welcome to redistribute it\n"
    "under certain conditions; type `show_license' for details.\n"
  );
  running_for_first_time ();
  parse_config ();


  get_terminal_attributes ();

  #ifdef USE_LUA
  if (lua_file) {
    lua_init (lua_file);
  }
  #endif

  inner_main ();
  
  return 0;
}
