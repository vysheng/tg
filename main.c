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

    Copyright Vitaly Valtman 2013-2015
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
#ifdef __FreeBSD__
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#if (READLINE == GNU)
#include <readline/readline.h>
#else
#include <editline/readline.h>
#endif
#ifdef EVENT_V2
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#else
#include <event.h>
#include "event-old.h"
#endif

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <fcntl.h>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#include <signal.h>
#ifdef HAVE_LIBCONFIG
#include <libconfig.h>
#endif

#include <grp.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "telegram.h"
#include "loop.h"
#include "interface.h"
#include <tgl/tools.h>
#include <getopt.h>

#ifdef USE_LUA
#  include "lua-tg.h"
#endif

#include <tgl/tgl.h>

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
int msg_num_mode;
char *default_username;
char *config_filename;
char *prefix;
char *auth_file_name;
char *state_file_name;
char *secret_chat_file_name;
char *downloads_directory;
char *config_directory;
char *binlog_file_name;
char *lua_file;
int binlog_enabled;
extern int log_level;
int sync_from_start;
int allow_weak_random;
int disable_colors;
int readline_disabled;
int disable_output;
int reset_authorization;
int port;
int use_ids;
int ipv6_enabled;
char *start_command;
int disable_link_preview;

struct tgl_state *TLS;

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

int str_empty (char *str) {
  return ((str == NULL) || (strlen(str) < 1));
}

char *get_home_directory (void) {
  static char *home_directory = NULL;
  home_directory = getenv("TELEGRAM_HOME");
  if (!str_empty (home_directory)) { return tstrdup (home_directory); }
  home_directory = getenv("HOME");
  if (!str_empty (home_directory)) { return tstrdup (home_directory); }
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
  if (str_empty (home_directory)) { home_directory = tstrdup ("."); }
  return home_directory;
}

char *get_config_directory (void) {
  char *config_directory;
  config_directory = getenv("TELEGRAM_CONFIG_DIR");
  if (!str_empty (config_directory)) { return tstrdup (config_directory); }
  // XDG: http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
  config_directory = getenv("XDG_CONFIG_HOME");
  if (!str_empty (config_directory)) {
    tasprintf (&config_directory, "%s/" PROG_NAME, config_directory);
    // :TODO: someone check whether it could be required to pass tasprintf
    //        a tstrdup()ed config_directory instead; works for me without.
    //        should work b/c this scope's lifespan encompasses tasprintf()
    return config_directory;
  }
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
  if (!str_empty (config_filename)) {
    return; // Do not create custom config file
  }
  if (str_empty (config_directory)) {
    config_directory = get_config_directory ();
  }
  tasprintf (&config_filename, "%s/%s", config_directory, CONFIG_FILE);
  config_filename = make_full_path (config_filename);
  if (!disable_output) {
    printf ("I: config dir=[%s]\n", config_directory);
  }
  // printf ("I: config file=[%s]\n", config_filename);

  int config_file_fd;
  char *config_directory = get_config_directory ();
  //char *downloads_directory = get_downloads_directory ();

  if (!mkdir (config_directory, CONFIG_DIRECTORY_MODE)) {
    if (!disable_output) {
      printf ("[%s] created\n", config_directory);
    }
  }

  tfree_str (config_directory);
  config_directory = NULL;
  // see if config file is there
  if (access (config_filename, R_OK) != 0) {
    // config file missing, so touch it
    config_file_fd = open (config_filename, O_CREAT | O_RDWR, 0600);
    if (config_file_fd == -1)  {
      perror ("open[config_file]");
      printf ("I: config_file=[%s]\n", config_filename);
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
    if (path && *r != '/') {
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
  //config_filename = make_full_path (config_filename);
  
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
    tgl_set_test_mode (TLS);
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
  
  int pfs_enabled = 0;
  strcpy (buf + l, "pfs_enabled");
  config_lookup_bool (&conf, buf, &pfs_enabled);
  if (pfs_enabled) {
    tgl_enable_pfs (TLS);
  }

  if (binlog_enabled) {
    parse_config_val (&conf, &binlog_file_name, "binlog", BINLOG_FILE, config_directory);
    tgl_set_binlog_mode (TLS, 1);
    tgl_set_binlog_path (TLS, binlog_file_name);
  } else {
    tgl_set_binlog_mode (TLS, 0);
    parse_config_val (&conf, &state_file_name, "state_file", STATE_FILE, config_directory);
    parse_config_val (&conf, &secret_chat_file_name, "secret", SECRET_CHAT_FILE, config_directory);
    //tgl_set_auth_file_path (auth_file_name);
  }
  tgl_set_download_directory (TLS, downloads_directory);
  
  if (!mkdir (config_directory, CONFIG_DIRECTORY_MODE)) {
    if (!disable_output) {
      printf ("[%s] created\n", config_directory);
    }
  }
  if (!mkdir (downloads_directory, CONFIG_DIRECTORY_MODE)) {
    if (!disable_output) {
      printf ("[%s] created\n", downloads_directory);
    }
  }
  config_destroy (&conf);
}
#else
void parse_config (void) {
  if (!disable_output) {
    printf ("libconfig not enabled\n");
  }
  tasprintf (&downloads_directory, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, DOWNLOADS_DIRECTORY);
  
  if (binlog_enabled) {
    tasprintf (&binlog_file_name, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, BINLOG_FILE);
    tgl_set_binlog_mode (TLS, 1);
    tgl_set_binlog_path (TLS, binlog_file_name);
  } else {
    tgl_set_binlog_mode (TLS, 0);
    //tgl_set_auth_file_path (auth_file_name;
    tasprintf (&auth_file_name, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, AUTH_KEY_FILE);
    tasprintf (&state_file_name, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, STATE_FILE);
    tasprintf (&secret_chat_file_name, "%s/%s/%s", get_home_directory (), CONFIG_DIRECTORY, SECRET_CHAT_FILE);
  }
  tgl_set_download_directory (TLS, downloads_directory);
  if (!mkdir (downloads_directory, CONFIG_DIRECTORY_MODE)) {
    if (!disable_output) {
      printf ("[%s] created\n", downloads_directory);
    }
  }
}
#endif

void inner_main (void) {
  loop ();
}

void usage (void) {
  printf ("%s Usage\n", PROGNAME);
    
  printf ("  --phone/-u                           specify username (would not be asked during authorization)\n");
  printf ("  --rsa-key/-k                         specify location of public key (possible multiple entries)\n");
  printf ("  --verbosity/-v                       increase verbosity (0-ERROR 1-WARNIN 2-NOTICE 3+-DEBUG-levels)\n");
  printf ("  --enable-msg-id/-N                   message num mode\n");
  #ifdef HAVE_LIBCONFIG
  printf ("  --config/-c                          config file name\n");
  printf ("  --profile/-p                         use specified profile\n");
  #else
  printf ("  --enable-binlog/-B                   enable binlog\n");
  #endif
  printf ("  --log-level/-l                       log level\n");
  printf ("  --sync-from-start/-f                 during authorization fetch all messages since registration\n");
  printf ("  --disable-auto-accept/-E             disable auto accept of encrypted chats\n");
  #ifdef USE_LUA
  printf ("  --lua-script/-s                      lua script file\n");
  #endif
  printf ("  --wait-dialog-list/-W                send dialog_list query and wait for answer before reading input\n");
  printf ("  --disable-colors/-C                  disable color output\n");
  printf ("  --disable-readline/-R                disable readline\n");
  printf ("  --daemonize/-d                       daemon mode\n");
  printf ("  --logname/-L <log-name>              log file name\n");
  printf ("  --username/-U <user-name>            change uid after start\n");
  printf ("  --groupname/-G <group-name>          change gid after start\n");
  printf ("  --disable-output/-D                  disable output\n");
  printf ("  --tcp-port/-P <port>                 port to listen for input commands\n");
  printf ("  --udp-socket/-S <socket-name>        unix socket to create\n");
  printf ("  --exec/-e <commands>                 make commands end exit\n");
  printf ("  --disable-names/-I                   use user and chat IDs in updates instead of names\n");
  printf ("  --enable-ipv6/-6                     use ipv6 (may be unstable)\n");
  printf ("  --help/-h                            prints this help\n");
  printf ("  --accept-any-tcp                     accepts tcp connections from any src (only loopback by default)\n");
  printf ("  --disable-link-preview               disables server-side previews to links\n");

  exit (1);
}

//extern char *rsa_public_key_name;
//extern int default_dc_num;




char *log_net_file;
FILE *log_net_f;

int register_mode;
int disable_auto_accept;
int wait_dialog_list;

char *logname;
int daemonize=0;


void reopen_logs (void) {
  int fd;
  fflush (stdout);
  fflush (stderr);
  if ((fd = open ("/dev/null", O_RDWR, 0)) != -1) {
    dup2 (fd, 0);
    dup2 (fd, 1);
    dup2 (fd, 2);
    if (fd > 2) {
      close (fd);
    }
  }
  if (logname && (fd = open (logname, O_WRONLY|O_APPEND|O_CREAT, 0640)) != -1) {
    dup2 (fd, 1);
    dup2 (fd, 2);
    if (fd > 2) {
      close (fd);
    }
  }
}


static void sigusr1_handler (const int sig) {
  fprintf(stderr, "got SIGUSR1, rotate logs.\n");
  reopen_logs ();
  signal (SIGUSR1, sigusr1_handler);
}

static void sighup_handler (const int sig) {
  fprintf(stderr, "got SIGHUP.\n");
  signal (SIGHUP, sighup_handler);
}

char *set_user_name;
char *set_group_name;
int accept_any_tcp;

int change_user_group () {
  char *username = set_user_name;
  char *groupname = set_group_name;
  struct passwd *pw;
  /* lose root privileges if we have them */
  if (getuid() == 0 || geteuid() == 0) {
    if (username == 0 || *username == '\0') {
      username = "telegramd";
    }
    if ((pw = getpwnam (username)) == 0) {
      fprintf (stderr, "change_user_group: can't find the user %s to switch to\n", username);
      return -1;
    }
    gid_t gid = pw->pw_gid;
    if (setgroups (1, &gid) < 0) {
      fprintf (stderr, "change_user_group: failed to clear supplementary groups list: %m\n");
      return -1;
    }

    if (groupname) {
      struct group *g = getgrnam (groupname);
      if (g == NULL) {
        fprintf (stderr, "change_user_group: can't find the group %s to switch to\n", groupname);
        return -1;
      }
      gid = g->gr_gid;
    }

    if (setgid (gid) < 0) {
      fprintf (stderr, "change_user_group: setgid (%d) failed. %m\n", (int) gid);
      return -1;
    }

    if (setuid (pw->pw_uid) < 0) {
      fprintf (stderr, "change_user_group: failed to assume identity of user %s\n", username);
      return -1;
    } else {
      pw = getpwuid(getuid());
      setenv("USER", pw->pw_name, 1);
      setenv("HOME", pw->pw_dir, 1);
      setenv("SHELL", pw->pw_shell, 1);
    }
  }
  return 0;
}

char *unix_socket;

void args_parse (int argc, char **argv) {
  TLS = tgl_state_alloc ();

  static struct option long_options[] = {
    {"debug-allocator", no_argument, 0,  1000 },
    {"phone", required_argument, 0, 'u'}, 
    {"rsa-key", required_argument, 0, 'k'},
    {"verbosity", no_argument, 0, 'v'},
    {"enable-msg-id", no_argument, 0, 'N'},
#ifdef HAVE_LIBCONFIG
    {"config", required_argument, 0, 'c'},
    {"profile", required_argument, 0, 'p'},
#else
    {"enable-binlog", no_argument, 0, 'B'},
#endif
    {"log-level", required_argument, 0, 'l'},
    {"sync-from-start", no_argument, 0, 'f'},
    {"disable-auto-accept", no_argument, 0, 'E'},
    {"allow-weak-random", no_argument, 0, 'w'},
#ifdef USE_LUA
    {"lua-script", required_argument, 0, 's'},
#endif
    {"wait-dialog-list", no_argument, 0, 'W'},
    {"disable-colors", no_argument, 0, 'C'},
    {"disable-readline", no_argument, 0, 'R'},
    {"daemonize", no_argument, 0, 'd'},
    {"logname", required_argument, 0, 'L'},
    {"username", required_argument, 0, 'U'},
    {"groupname", required_argument, 0, 'G'},
    {"disable-output", no_argument, 0, 'D'},
    {"reset-authorization", no_argument, 0, 'q'},
    {"tcp-port", required_argument, 0, 'P'},
    {"unix-socket", required_argument, 0, 'S'},
    {"exec", required_argument, 0, 'e'},
    {"disable-names", no_argument, 0, 'I'},
    {"enable-ipv6", no_argument, 0, '6'},
    {"help", no_argument, 0, 'h'},
    {"accept-any-tcp", no_argument, 0,  1001},
    {"disable-link-preview", no_argument, 0, 1002},
    {0,         0,                 0,  0 }
  };



  int opt = 0;
  while ((opt = getopt_long (argc, argv, "u:hk:vNl:fEwWCRdL:DU:G:qP:S:e:I6"
#ifdef HAVE_LIBCONFIG
  "c:p:"
#else
  "B"
#endif
#ifdef USE_LUA
  "s:"
#endif
  , long_options, NULL
  )) != -1) {
    switch (opt) {
    case 1000:
      tgl_allocator = &tgl_allocator_debug;
      break;
    case 1001:
      accept_any_tcp = 1;
      break;
    case 'u':
      set_default_username (optarg);
      break;
    case 'k':
      //rsa_public_key_name = tstrdup (optarg);
      tgl_set_rsa_key (TLS, optarg);
      break;
    case 'v':
      tgl_incr_verbosity (TLS);
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
      prefix = optarg;
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
    case 'f':
      sync_from_start = 1;
      break;
    case 'E':
      disable_auto_accept = 1;
      break;
    case 'w':
      allow_weak_random = 1;
      break;
#ifdef USE_LUA
    case 's':
      lua_file = strdup (optarg);
      break;
#endif
    case 'W':
      wait_dialog_list = 1;
      break;
    case 'C':
      disable_colors ++;
      break;
    case 'R':
      readline_disabled ++;
      break;
    case 'd':
      daemonize ++;
      break;
    case 'L':
      logname = optarg;
      break;
    case 'U':
      set_user_name = optarg;
      break;
    case 'G':
      set_group_name = optarg;
      break;
    case 'D':
      disable_output ++;
      break;
    case 'q':
      reset_authorization ++;
      break;
    case 'P':
      port = atoi (optarg);
      break;
    case 'S':
      unix_socket = optarg;
      break;
    case 'e':
      start_command = optarg;
      break;
    case 'I':
      use_ids ++;
      break;
    case '6':
      ipv6_enabled = 1;
      break;
    case 1002:
      disable_link_preview = 2;
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

int sfd;
int usfd;

void termination_signal_handler (int signum) {
  if (!readline_disabled) {
    rl_free_line_state ();
    rl_cleanup_after_signal ();
  }
  
  if (write (1, "SIGNAL received\n", 18) < 0) { 
    // Sad thing
  }
 
  if (unix_socket) {
    unlink (unix_socket);
  }
  
  if (usfd > 0) {
    close (usfd);
  }
  if (sfd > 0) {
    close (sfd);
  }
  print_backtrace ();
  
  exit (EXIT_FAILURE);
}

volatile int sigterm_cnt;

void sig_term_handler (int signum __attribute__ ((unused))) {
  signal (signum, termination_signal_handler);
  //set_terminal_attributes ();
  if (write (1, "SIGTERM/SIGINT received\n", 25) < 0) { 
    // Sad thing
  }
  if (TLS && TLS->ev_base) {
    event_base_loopbreak (TLS->ev_base);
  }
  sigterm_cnt ++;
}

void do_halt (int error) {
  if (daemonize) {
    return;
  }

  if (!readline_disabled) {
    rl_free_line_state ();
    rl_cleanup_after_signal ();
  }

  if (write (1, "halt\n", 5) < 0) { 
    // Sad thing
  }
 
  if (unix_socket) {
    unlink (unix_socket);
  }
  
  if (usfd > 0) {
    close (usfd);
  }
  if (sfd > 0) {
    close (sfd);
  }
  
  exit (error ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main (int argc, char **argv) {
  signal (SIGSEGV, termination_signal_handler);
  signal (SIGABRT, termination_signal_handler);
  signal (SIGBUS, termination_signal_handler);
  signal (SIGQUIT, termination_signal_handler);
  signal (SIGFPE, termination_signal_handler);

  signal (SIGPIPE, SIG_IGN);
  
  signal (SIGTERM, sig_term_handler);
  signal (SIGINT, sig_term_handler);

  rl_catch_signals = 0;


  log_level = 10;
  
  args_parse (argc, argv);
  
  change_user_group ();

  if (port > 0) {
    struct sockaddr_in serv_addr;

    sfd = socket (AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
      perror ("socket");
      exit(1);
    }

    memset (&serv_addr, 0, sizeof (serv_addr));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = accept_any_tcp ? INADDR_ANY : htonl (0x7f000001);
    serv_addr.sin_port = htons (port);
 
    if (bind (sfd, (struct sockaddr *) &serv_addr, sizeof (serv_addr)) < 0) {
      perror ("bind");
      exit(1);
    }

    listen (sfd, 5);
  } else {
    sfd = -1;
  }
  
  if (unix_socket) {
    assert (strlen (unix_socket) < 100);
    struct sockaddr_un serv_addr;

    usfd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (usfd < 0) {
      perror ("socket");
      exit(1);
    }

    memset (&serv_addr, 0, sizeof (serv_addr));
    
    serv_addr.sun_family = AF_UNIX;

    snprintf (serv_addr.sun_path, 108, "%s", unix_socket);
 
    if (bind (usfd, (struct sockaddr *) &serv_addr, sizeof (serv_addr)) < 0) {
      perror ("bind");
      exit(1);
    }

    listen (usfd, 5);    
  } else {
    usfd = -1;
  }

  if (daemonize) {
    signal (SIGHUP, sighup_handler);
    reopen_logs ();
  }
  signal (SIGUSR1, sigusr1_handler);

  if (!disable_output) {
    printf (
      "Telegram-cli version " TELEGRAM_CLI_VERSION ", Copyright (C) 2013-2015 Vitaly Valtman\n"
      "Telegram-cli comes with ABSOLUTELY NO WARRANTY; for details type `show_license'.\n"
      "This is free software, and you are welcome to redistribute it\n"
      "under certain conditions; type `show_license' for details.\n"
      "Telegram-cli uses libtgl version " TGL_VERSION "\n"
    );
  }
  running_for_first_time ();
  parse_config ();

  #ifdef __FreeBSD__
  tgl_set_rsa_key (TLS, "/usr/local/etc/" PROG_NAME "/server.pub");
  #else
  tgl_set_rsa_key (TLS, "/etc/" PROG_NAME "/server.pub");
  #endif
  tgl_set_rsa_key (TLS, "tg-server.pub");


  get_terminal_attributes ();

  #ifdef USE_LUA
  if (lua_file) {
    lua_init (lua_file);
  }
  #endif

  inner_main ();
  
  return 0;
}
