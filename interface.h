#ifndef __INTERFACE_H__
#define __INTERFACE_H__

#define COLOR_RED "\033[0;31m"
#define COLOR_REDB "\033[1;31m"
#define COLOR_NORMAL "\033[0m"
#define COLOR_GREEN "\033[32;1m"
#define COLOR_GREY "\033[37;1m"
#define COLOR_YELLOW "\033[33;1m"
#define COLOR_BLUE "\033[34;1m"
#define COLOR_MAGENTA "\033[35;1m"


char *get_default_prompt (void);
char *complete_none (const char *text, int state);
char **complete_text (char *text, int start, int end);
void interpreter (char *line);

void rprintf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));
void iprintf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));
void logprintf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));
void hexdump (int *in_ptr, int *in_end);

struct message;
union user_chat;
void print_message (struct message *M);
void print_chat_name (int id, union user_chat *C);
void print_user_name (int id, union user_chat *U);
//void print_media (struct message_media *M);
void pop_color (void);
void push_color (const char *color);
void print_start (void);
void print_end (void);
#endif
