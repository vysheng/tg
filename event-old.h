#ifndef __EVENT_OLD_H__
#define __EVENT_OLD_H__

typedef int evutil_socket_t;
  
static inline struct event *event_new (struct event_base *base, int fd, int what, void(*callback)(int, short, void *), void *arg) {
  struct event *ev = malloc (sizeof (*ec));
  event_set (ev, base, fd, what, callback, arg);
}
  
static inline struct event *evtimer_new (struct event_base *base, void(*callback)(int, short, void *), void *arg) {
  struct event *ev = malloc (sizeof (*ec));
  evtimer_set (ev, base, callback, arg);
}

static void event_free (struct event *ev) {
  event_del (ev);
  free (ev);
}
#endif
