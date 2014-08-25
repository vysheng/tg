/*
    This file is part of telegram-cli.

    Telegram-cli is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Telegram-cli is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this telegram-cli.  If not, see <http://www.gnu.org/licenses/>.

    Copyright Vitaly Valtman 2013-2014
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
#include "interface.h"
#include "tools.h"

#ifdef USE_LUA
#  include "lua-tg.h"
#endif

#include "tgl.h"

#define PROGNAME "telegram-cli"
#define VERSION "0.07"

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

int verbosity;
char *default_username;
char *auth_token;
int msg_num_mode;
char *config_filename;
char *prefix;
char *auth_file_name;
char *state_file_name;
char *secret_chat_file_name;
char *downloads_directory;
char *config_directory;
char *binlog_file_name;
int binlog_enabled;
extern int log_level;
int sync_from_start;
int allow_weak_random;
char *lua_file;
int disable_colors;

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
    if (path && default_name) {
      tasprintf (s, "%s/%s", path, default_name);
    } else {
      *s  = default_name ? tstrdup (default_name) : 0;
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
  
  int test_mode = 0;
  strcpy (buf + l, "test");
  config_lookup_bool (&conf, buf, &test_mode);
  if (test_mode) {
    tgl_set_test_mode ();
  }
  
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
  parse_config_val (&conf, &downloads_directory, "downloads", DOWNLOADS_DIRECTORY, config_directory);
  
  if (!lua_file) {
    parse_config_val (&conf, &lua_file, "lua_script", 0, config_directory);
  }
  
  strcpy (buf + l, "binlog_enabled");
  config_lookup_bool (&conf, buf, &binlog_enabled);

  if (binlog_enabled) {
    parse_config_val (&conf, &binlog_file_name, "binlog", BINLOG_FILE, config_directory);
    tgl_set_binlog_mode (1);
    tgl_set_binlog_path (binlog_file_name);
  } else {
    tgl_set_binlog_mode (0);
    parse_config_val (&conf, &state_file_name, "state_file", STATE_FILE, config_directory);
    parse_config_val (&conf, &secret_chat_file_name, "secret", SECRET_CHAT_FILE, config_directory);
    //tgl_set_auth_file_path (auth_file_name);
  }
  tgl_set_download_directory (downloads_directory);
  
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
  tasprintf (&downloads_directory, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, DOWNLOADS_DIRECTORY);
  
  if (binlog_enabled) {
    tasprintf (&binlog_file_name, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, BINLOG_FILE);
    tgl_set_binlog_mode (1);
    tgl_set_binlog_path (binlog_file_name);
  } else {
    tgl_set_binlog_mode (0);
    //tgl_set_auth_file_path (auth_file_name;
    tasprintf (&auth_file_name, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, AUTH_KEY_FILE);
    tasprintf (&state_file_name, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, STATE_FILE);
    tasprintf (&secret_chat_file_name, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, SECRET_CHAT_FILE);
  }
  tgl_set_download_directory (downloads_directory);
}
#endif

void inner_main (void) {
  loop ();
}

void usage (void) {
  printf ("%s Usage\n", PROGNAME);
    
  printf ("  -u                  specify username (would not be asked during authorization)\n");
  printf ("  -k                  specify location of public key (possible multiple entries)\n");
  printf ("  -v                  increase verbosity (0-ERROR 1-WARNIN 2-NOTICE 3+-DEBUG-levels)\n");
  printf ("  -N                  message num mode\n");
  #ifdef HAVE_LIBCONFIG
  printf ("  -c                  config file name\n");
  printf ("  -p                  use specified profile\n");
  #else
  printf ("  -B                  enable binlog\n");
  #endif
  printf ("  -l                  log level\n");
  printf ("  -f                  during authorization fetch all messages since registration\n");
  printf ("  -E                  diable auto accept of encrypted chats\n");
  #ifdef USE_LUA
  printf ("  -s                  lua script file\n");
  #endif
  printf ("  -W                  send dialog_list query and wait for answer before reading input\n");
  printf ("  -C                  disable color output\n");

  exit (1);
}

//extern char *rsa_public_key_name;
//extern int default_dc_num;

char *log_net_file;
FILE *log_net_f;

int register_mode;
int disable_auto_accept;
int wait_dialog_list;


void args_parse (int argc, char **argv) {
  int opt = 0;
  while ((opt = getopt (argc, argv, "u:hk:vNl:fEwWC"
#ifdef HAVE_LIBCONFIG
  "c:p:"
#else
  "B"
#endif
#ifdef USE_LUA
  "c"
#endif
  
  )) != -1) {
    switch (opt) {
    case 'u':
      set_default_username (optarg);
      break;
    case 'k':
      //rsa_public_key_name = tstrdup (optarg);
      tgl_set_rsa_key (optarg);
      break;
    case 'v':
      tgl_incr_verbosity ();
      verbosity ++;
      break;
    case 'N':
      msg_num_mode ++;
      break;
#ifdef HAVE_LIBCONFIG
    case 'c':
      config_filename = tstrdup (optarg);
      break;
    case 'p':
      prefix = tstrdup (optarg);
      assert (strlen (prefix) <= 100);
      break;
#else
    case 'B':
      binlog_enabled = 1;
      break;
#endif
    case 'l':
      log_level = atoi (optarg);
      break;
    //case 'R':
    //  register_mode = 1;
    //  break;
    case 'f':
      sync_from_start = 1;
      break;
    //case 'L':
    //  if (log_net_file) { 
    //    usage ();
    //  }
    //  log_net_file = tstrdup (optarg);
    //  log_net_f = fopen (log_net_file, "a");
    //  assert (log_net_f);
    //  break;
    case 'E':
      disable_auto_accept = 1;
      break;
    case 'w':
      allow_weak_random = 1;
      break;
#ifdef USE_LUA
    case 's':
      lua_file = tstrdup (optarg);
      break;
#endif
    case 'W':
      wait_dialog_list = 1;
      break;
    case 'C':
      disable_colors ++;
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
    "Telegram-client version " TGL_VERSION ", Copyright (C) 2013 Vitaly Valtman\n"
    "Telegram-client comes with ABSOLUTELY NO WARRANTY; for details type `show_license'.\n"
    "This is free software, and you are welcome to redistribute it\n"
    "under certain conditions; type `show_license' for details.\n"
  );
  running_for_first_time ();
  parse_config ();

  tgl_set_rsa_key ("/etc/ " PROG_NAME "/server.pub");
  tgl_set_rsa_key ("tg-server.pub");


  get_terminal_attributes ();

  #ifdef USE_LUA
  if (lua_file) {
    lua_init (lua_file);
  }
  #endif

  inner_main ();
  
  return 0;
}
