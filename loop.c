#define READLINE_CALLBACKS

#include <assert.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <errno.h>

#include "interface.h"
extern char *default_username;
extern char *auth_token;
void set_default_username (const char *s);



int main_loop (void) {
  fd_set inp, outp;
  struct timeval tv;
  while (1) {
    FD_ZERO (&inp);
    FD_ZERO (&outp);
    FD_SET (0, &inp);
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    
    int lfd = 0;

    if (select (lfd + 1, &inp, &outp, NULL, &tv) < 0) {
      if (errno == EINTR) {
        /* resuming from interrupt, so not an error situation,
           this generally happens when you suspend your
           messenger with "C-z" and then "fg". This is allowed "
         */
        rl_reset_line_state ();
        rl_forced_update_display ();
        continue;
      }
      perror ("select()");
      break;
    }
    
    if (FD_ISSET (0, &inp)) {
      rl_callback_read_char ();
    }
  }
  return 0;
}

int loop (void) {
  size_t size = 0;
  char *user = default_username;

  if (!user && !auth_token) {
    printf ("Telephone number (with '+' sign): ");         
    if (getline (&user, &size, stdin) == -1) {
      perror ("getline()");
      exit (EXIT_FAILURE);
    }
    user[strlen (user) - 1] = '\0';      
    set_default_username (user);
  }
  
  fflush (stdin);

  rl_callback_handler_install (get_default_prompt (), interpreter);
  rl_attempted_completion_function = (CPPFunction *) complete_text;
  rl_completion_entry_function = complete_none;

  return main_loop ();
}

