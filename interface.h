#ifndef __INTERFACE_H__
#define __INTERFACE_H__
char *get_default_prompt (void);
char *complete_none (const char *text, int state);
char **complete_text (char *text, int start, int end);
void interpreter (char *line);
#endif
