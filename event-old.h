#ifndef __EVENT_OLD_H__
#define __EVENT_OLD_H__

typedef int evutil_socket_t;
  
static inline struct event *event_new (struct event_base *base, int fd, int what, void(*callback)(int, short, void *), void *arg) __attribute__ ((unused));
static inline struct event *event_new (struct event_base *base, int fd, int what, void(*callback)(int, short, void *), void *arg) {
  struct event *ev = malloc (sizeof (*ev));  
  event_set (ev, fd, what, callback, arg);
  event_base_set (base, ev);
  return ev;
}
  
static inline struct event *evtimer_new (struct event_base *base, void(*callback)(int, short, void *), void *arg) __attribute__ ((unused));
static inline struct event *evtimer_new (struct event_base *base, void(*callback)(int, short, void *), void *arg) {
  struct event *ev = malloc (sizeof (*ev));
  event_set (ev, -1, 0, callback, arg);
  event_base_set (base, ev);
  return ev;
}

static void event_free (struct event *ev) __attribute__ ((unused));
static void event_free (struct event *ev) {
  event_del (ev);
  free (ev);
}
#endif
