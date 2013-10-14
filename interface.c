#define _GNU_SOURCE 
#include <readline/readline.h>
#include <readline/history.h>

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "include.h"
#include "queries.h"

#include "interface.h"
#include "telegram.h"
#include "structures.h"
char *default_prompt = ">";

char *get_default_prompt (void) {
  return default_prompt;
}

char *complete_none (const char *text UU, int state UU) {
  return 0;
}

char *commands[] = {
  "help",
  "msg",
  "contact_list",
  "stats",
  0 };

int commands_flags[] = {
  070,
  072,
  07,
  07,
};

char *a = 0;
char *user_list[MAX_USER_NUM + 1];
char **chat_list = &a;

int init_token (char **q) {
  char *r = *q;
  while (*r == ' ') { r ++; }
  if (!*r) { return 0; }
  *q = r;
  return 1;
}

char *get_token (char **q, int *l) {
  char *r = *q;
  while (*r == ' ') { r ++; }
  if (!*r) { 
    *q = r; 
    *l = 0;
    return 0;
  }
  int neg = 0;
  char *s = r;
  while (*r && (*r != ' ' || neg)) {
    if (*r == '\\') {
      neg = 1 - neg;
    } else {
      neg = 0;
    }
    r++;
  }
  *q = r;
  *l = r - s;
  return s;
}


int get_complete_mode (void) {
  char *q = rl_line_buffer;
  if (!init_token (&q)) { return 0; }
  int l = 0;
  char *r = get_token (&q, &l);
  if (!*q) { return 0; }
  
  char **command = commands;
  int n = 0;
  int flags = -1;
  while (*command) {
    if (!strncmp (r, *command, l)) {
      flags = commands_flags[n];
      break;
    }
    n ++;
    command ++;
  }
  if (flags == -1) {
    return -1;
  }
  int s = 0;
  while (1) {
    get_token (&q, &l);
    if (!*q) { return flags ? flags & 7 : 7; }
    s ++;
    if (s <= 4) { flags >>= 3; }
  }
}

extern int user_num;
extern int chat_num;
extern union user_chat *Peers[];
int complete_user_list (int index, const char *text, int len, char **R) {
  index ++;
  while (index < user_num + chat_num && (!Peers[index]->print_name || strncmp (Peers[index]->print_name, text, len) || Peers[index]->id < 0)) {
    index ++;
  }
  if (index < user_num + chat_num) {
    *R = strdup (Peers[index]->print_name);
    return index;
  } else {
    return -1;
  }
}

int complete_user_chat_list (int index, const char *text, int len, char **R) {
  index ++;
  while (index < user_num + chat_num && (!Peers[index]->print_name || strncmp (Peers[index]->print_name, text, len))) {
    index ++;
  }
  if (index < user_num + chat_num) {
    *R = strdup (Peers[index]->print_name);
    return index;
  } else {
    return -1;
  }
}

int complete_string_list (char **list, int index, const char *text, int len, char **R) {
  index ++;
  while (list[index] && strncmp (list[index], text, len)) {
    index ++;
  }
  if (list[index]) {
    *R = strdup (list[index]);
    return index;
  } else {
    *R = 0;
    return -1;
  }
}
char *command_generator (const char *text, int state) {  
  static int len, index, mode;
 
  if (!state) {
    len = strlen (text);
    index = -1;
     
    rl_line_buffer[rl_point] = '\0'; /* the effect should be such
      * that the cursor position
      * is at the end of line for
      * the auto completion regex
      * above (note the $ at end)
      */

    mode = get_complete_mode ();
  } else {
    if (index == -1) { return 0; }
  }

  if (mode == -1) { return 0; }

  char *R = 0;
  switch (mode & 7) {
  case 0:
    index = complete_string_list (commands, index, text, len, &R);
    return R;
  case 1:
    index = complete_user_list (index, text, len, &R);
    return R;
  case 2:
    index = complete_user_chat_list (index, text, len, &R);
    return R;
  case 3:
    return rl_filename_completion_function(text,state);
  default:
    return 0;
  }
}

char **complete_text (char *text, int start UU, int end UU) {
  return (char **) rl_completion_matches (text, command_generator);
}

void interpreter (char *line UU) {
  if (line && *line) {
    add_history (line);
  }
  if (!memcmp (line, "contact_list", 12)) {
    do_update_contact_list ();
  } else if (!memcmp (line, "stats", 5)) {
    static char stat_buf[1 << 15];
    print_stat (stat_buf, (1 << 15) - 1);
    printf ("%s\n", stat_buf);
  }
}

int readline_active;
void rprintf (const char *format, ...) {
  int saved_point = 0;
  char *saved_line = 0;
  if (readline_active) {
    saved_point = rl_point;
    saved_line = rl_copy_text(0, rl_end);
    rl_save_prompt();
    rl_replace_line("", 0);
    rl_redisplay();
  }

  va_list ap;
  va_start (ap, format);
  vfprintf (stdout, format, ap);
  va_end (ap);

  if (readline_active) {
    rl_restore_prompt();
    rl_replace_line(saved_line, 0);
    rl_point = saved_point;
    rl_redisplay();
    free(saved_line);
  }
}

void hexdump (int *in_ptr, int *in_end) {
  int saved_point = 0;
  char *saved_line = 0;
  if (readline_active) {
    saved_point = rl_point;
    saved_line = rl_copy_text(0, rl_end);
    rl_save_prompt();
    rl_replace_line("", 0);
    rl_redisplay();
  }
  int *ptr = in_ptr;
  while (ptr < in_end) { fprintf (stdout, " %08x", *(ptr ++)); }
  fprintf (stdout, "\n");
  
  if (readline_active) {
    rl_restore_prompt();
    rl_replace_line(saved_line, 0);
    rl_point = saved_point;
    rl_redisplay();
    free(saved_line);
  }
}

void logprintf (const char *format, ...) {
  int saved_point = 0;
  char *saved_line = 0;
  if (readline_active) {
    saved_point = rl_point;
    saved_line = rl_copy_text(0, rl_end);
    rl_save_prompt();
    rl_replace_line("", 0);
    rl_redisplay();
  }
  
  printf (COLOR_GREY " *** ");
  va_list ap;
  va_start (ap, format);
  vfprintf (stdout, format, ap);
  va_end (ap);
  printf (COLOR_NORMAL);

  if (readline_active) {
    rl_restore_prompt();
    rl_replace_line(saved_line, 0);
    rl_point = saved_point;
    rl_redisplay();
    free(saved_line);
  }
}
