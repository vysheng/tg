#include "net.h"
#ifndef __QUERIES_H__
#define __QUERIES_H__

#define QUERY_ACK_RECEIVED 1

struct query;
struct query_methods {
  int (*on_answer)(struct query *q);
  int (*on_error)(struct query *q, int error_code, int len, char *error);
  int (*on_timeout)(struct query *q);
};

struct event_timer {
  double timeout;
  int (*alarm)(void *self);
  void *self;
};

struct query {
  long long msg_id;
  int data_len;
  int flags;
  void *data;
  struct query_methods *methods;
  struct event_timer ev;
};


struct query *send_query (struct dc *DC, int len, void *data, struct query_methods *methods);
void query_ack (long long id);
void query_error (long long id);
void query_result (long long id);

void insert_event_timer (struct event_timer *ev);
void remove_event_timer (struct event_timer *ev);
double next_timer_in (void);
void work_timers (void);

extern struct query_methods help_get_config_methods;

void do_send_code (const char *user);
int do_send_code_result (const char *code);
double get_double_time (void);

void do_update_contact_list (void);
union user_chat;
void do_send_message (union user_chat *U, const char *msg);
void do_get_history (union user_chat *U, int limit);
void do_get_dialog_list (void);
#endif
