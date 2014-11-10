#ifndef __EVENT_OLD_H__
#define __EVENT_OLD_H__

#include <assert.h>
#include <stdlib.h>

#define BEV_EVENT_READ EVBUFFER_READ
#define BEV_EVENT_WRITE EVBUFFER_WRITE
#define BEV_EVENT_EOF EVBUFFER_EOF
#define BEV_EVENT_ERROR EVBUFFER_ERROR
#define BEV_EVENT_TIMEOUT EVBUFFER_TIMEOUT

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

static struct bufferevent *bufferevent_socket_new (struct event_base *base, int fd, int flags) __attribute__ ((unused));
static struct bufferevent *bufferevent_socket_new (struct event_base *base, int fd, int flags) {
  assert (!flags);
  struct bufferevent *bev = bufferevent_new(fd, 0, 0, 0, 0);
  bufferevent_base_set (base, bev);
  return bev;
}

static inline void *event_get_callback_arg(const struct event *ev) {
  return ev->ev_arg;
}
#endif
