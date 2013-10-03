#define READLINE_CALLBACKS

#include <assert.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "interface.h"
extern char *default_username;
extern char *auth_token;
void set_default_username (const char *s);



int main_loop (void) {
  assert (0);
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

