/* 
    This file is part of tgl-library

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

    Copyright Vitaly Valtman 2013-2014
*/
#include "config.h"
#ifdef EVENT_V2
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#else
#include <event.h>
#include "event-old.h"
#endif

#include "tgl.h"
#include <stdlib.h>

static void timer_alarm (evutil_socket_t fd, short what, void *arg) {
  void **p = arg;
  ((void (*)(struct tgl_state *, void *))p[1]) (p[0], p[2]);
}

struct tgl_timer *tgl_timer_alloc (struct tgl_state *TLS, void (*cb)(struct tgl_state *TLS, void *arg), void *arg) {
  void **p = malloc (sizeof (void *) * 3);
  p[0] = TLS;
  p[1] = cb;
  p[2] = arg;
  return (void *)evtimer_new (TLS->ev_base, timer_alarm, p);
}

void tgl_timer_insert (struct tgl_timer *t, double p) {
  if (p < 0) { p = 0; }
  double e = p - (int)p;
  if (e < 0) { e = 0; }
  struct timeval pv = { (int)p, (int)(e * 1e6)};
  event_add ((void *)t, &pv);
}

void tgl_timer_delete (struct tgl_timer *t) {
  event_del ((void *)t);
}

void tgl_timer_free (struct tgl_timer *t) {
  void *arg = event_get_callback_arg ((void *)t);
  free (arg);
  event_free ((void *)t);
}

struct tgl_timer_methods tgl_libevent_timers = {
  .alloc = tgl_timer_alloc, 
  .insert = tgl_timer_insert,
  .delete = tgl_timer_delete,
  .free = tgl_timer_free
};
