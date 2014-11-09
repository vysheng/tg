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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <openssl/rand.h>
#include <arpa/inet.h>
#ifdef EVENT_V2
#include <event2/event.h>
#else
#include <event.h>
#include "event-old.h"
#endif
#include <sys/time.h>
#include <time.h>

#include "net.h"
#include "include.h"
#include "tgl.h"
#include "tgl-inner.h"
//#include "mtproto-client.h"
//#include "mtproto-common.h"
#include "tree.h"
#include "tools.h"

#ifndef POLLRDHUP
#define POLLRDHUP 0
#endif

//double get_utime (int clock_id);

//extern struct mtproto_methods auth_methods;

static void fail_connection (struct connection *c);

#define PING_TIMEOUT 10

static void start_ping_timer (struct connection *c);
static void ping_alarm (evutil_socket_t fd, short what, void *arg) {
  struct connection *c = arg;
  struct tgl_state *TLS = c->TLS;
  vlogprintf (E_DEBUG + 2,"ping alarm\n");
  assert (c->state == conn_ready || c->state == conn_connecting);
  if (tglt_get_double_time () - c->last_receive_time > 6 * PING_TIMEOUT) {
    vlogprintf (E_WARNING, "fail connection: reason: ping timeout\n");
    c->state = conn_failed;
    fail_connection (c);
  } else if (tglt_get_double_time () - c->last_receive_time > 3 * PING_TIMEOUT && c->state == conn_ready) {
    tgl_do_send_ping (c->TLS, c);
    start_ping_timer (c);
  } else {
    start_ping_timer (c);
  }
}

static void stop_ping_timer (struct connection *c) {
  event_del (c->ping_ev);
}

static void start_ping_timer (struct connection *c) {
  static struct timeval ptimeout = { PING_TIMEOUT, 0};
  event_add (c->ping_ev, &ptimeout);
}

static void restart_connection (struct connection *c);

static void fail_alarm (evutil_socket_t fd, short what, void *arg) {
  struct connection *c = arg;
  c->in_fail_timer = 0;
  restart_connection (c);
}

static void start_fail_timer (struct connection *c) {
  if (c->in_fail_timer) { return; }
  c->in_fail_timer = 1;  

  static struct timeval ptimeout = { 10, 0};
  event_add (c->fail_ev, &ptimeout);
}

static struct connection_buffer *new_connection_buffer (int size) {
  struct connection_buffer *b = talloc0 (sizeof (*b));
  b->start = talloc (size);
  b->end = b->start + size;
  b->rptr = b->wptr = b->start;
  return b;
}

static void delete_connection_buffer (struct connection_buffer *b) {
  tfree (b->start, b->end - b->start);
  tfree (b, sizeof (*b));
}

int tgln_write_out (struct connection *c, const void *_data, int len) {
  struct tgl_state *TLS = c->TLS;
  vlogprintf (E_DEBUG, "write_out: %d bytes\n", len);
  const unsigned char *data = _data;
  if (!len) { return 0; }
  assert (len > 0);
  int x = 0;
  if (!c->out_bytes) {
    event_add (c->write_ev, 0);
  }
  if (!c->out_head) {
    struct connection_buffer *b = new_connection_buffer (1 << 20);
    c->out_head = c->out_tail = b;
  }
  while (len) {
    if (c->out_tail->end - c->out_tail->wptr >= len) {
      memcpy (c->out_tail->wptr, data, len);
      c->out_tail->wptr += len;
      c->out_bytes += len;
      return x + len;
    } else {
      int y = c->out_tail->end - c->out_tail->wptr;
      assert (y < len);
      memcpy (c->out_tail->wptr, data, y);
      x += y;
      len -= y;
      data += y;
      struct connection_buffer *b = new_connection_buffer (1 << 20);
      c->out_tail->next = b;
      b->next = 0;
      c->out_tail = b;
      c->out_bytes += y;
    }
  }
  return x;
}

int tgln_read_in (struct connection *c, void *_data, int len) {
  unsigned char *data = _data;
  if (!len) { return 0; }
  assert (len > 0);
  if (len > c->in_bytes) {
    len = c->in_bytes;
  }
  int x = 0;
  while (len) {
    int y = c->in_head->wptr - c->in_head->rptr;
    if (y > len) {
      memcpy (data, c->in_head->rptr, len);
      c->in_head->rptr += len;
      c->in_bytes -= len;
      return x + len;
    } else {
      memcpy (data, c->in_head->rptr, y);
      c->in_bytes -= y;
      x += y;
      data += y;
      len -= y;
      void *old = c->in_head;
      c->in_head = c->in_head->next;
      if (!c->in_head) {
        c->in_tail = 0;
      }
      delete_connection_buffer (old);
    }
  }
  return x;
}

int tgln_read_in_lookup (struct connection *c, void *_data, int len) {
  unsigned char *data = _data;
  if (!len || !c->in_bytes) { return 0; }
  assert (len > 0);
  if (len > c->in_bytes) {
    len = c->in_bytes;
  }
  int x = 0;
  struct connection_buffer *b = c->in_head;
  while (len) {
    int y = b->wptr - b->rptr;
    if (y >= len) {
      memcpy (data, b->rptr, len);
      return x + len;
    } else {
      memcpy (data, b->rptr, y);
      x += y;
      data += y;
      len -= y;
      b = b->next;
    }
  }
  return x;
}

void tgln_flush_out (struct connection *c UU) {
}

#define MAX_CONNECTIONS 100
static struct connection *Connections[MAX_CONNECTIONS];
static int max_connection_fd;

static void rotate_port (struct connection *c) {
  switch (c->port) {
  case 443:
    c->port = 80;
    break;
  case 80:
    c->port = 25;
    break;
  case 25:
    c->port = 443;
    break;
  }
}

static void try_read (struct connection *c);
static void try_write (struct connection *c);

static void conn_try_read (evutil_socket_t fd, short what, void *arg) {
  struct connection *c = arg;
  struct tgl_state *TLS = c->TLS;
  vlogprintf (E_DEBUG + 1, "Try read. Fd = %d\n", c->fd);
  try_read (c);
}
static void conn_try_write (evutil_socket_t fd, short what, void *arg) {
  struct connection *c = arg;
  struct tgl_state *TLS = c->TLS;
  if (c->state == conn_connecting) {
    c->state = conn_ready;
    c->methods->ready (TLS, c);
  }
  try_write (c);
  if (c->out_bytes) {
    event_add (c->write_ev, 0);
  }
}

struct connection *tgln_create_connection (struct tgl_state *TLS, const char *host, int port, struct tgl_session *session, struct tgl_dc *dc, struct mtproto_methods *methods) {
  struct connection *c = talloc0 (sizeof (*c));
  c->TLS = TLS;
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    vlogprintf (E_ERROR, "Can not create socket: %m\n");
    exit (1);
  }
  assert (fd >= 0 && fd < MAX_CONNECTIONS);
  if (fd > max_connection_fd) {
    max_connection_fd = fd;
  }
  int flags = -1;
  setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof (flags));
  setsockopt (fd, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof (flags));
  setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof (flags));

  struct sockaddr_in addr;
  addr.sin_family = AF_INET; 
  addr.sin_port = htons (port);
  addr.sin_addr.s_addr = inet_addr (host);


  fcntl (fd, F_SETFL, O_NONBLOCK);

  if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == -1) {
    //vlogprintf (E_ERROR, "Can not connect to %s:%d %m\n", host, port);
    if (errno != EINPROGRESS) {
      vlogprintf (E_ERROR, "Can not connect to %s:%d %m\n", host, port);
      close (fd);
      tfree (c, sizeof (*c));
      return 0;
    }
  }

  c->fd = fd;
  c->state = conn_connecting;
  c->last_receive_time = tglt_get_double_time ();
  c->ip = tstrdup (host);
  c->flags = 0;
  c->port = port;
  assert (!Connections[fd]);
  Connections[fd] = c;
 
  c->ping_ev = evtimer_new (TLS->ev_base, ping_alarm, c);
  c->fail_ev = evtimer_new (TLS->ev_base, fail_alarm, c);
  c->write_ev = event_new (TLS->ev_base, c->fd, EV_WRITE, conn_try_write, c);

  struct timeval tv = {5, 0};
  c->read_ev = event_new (TLS->ev_base, c->fd, EV_READ | EV_PERSIST, conn_try_read, c);
  event_add (c->read_ev, &tv);

  start_ping_timer (c);

  c->dc = dc;
  c->session = session;
  c->methods = methods;

  char byte = 0xef;
  assert (tgln_write_out (c, &byte, 1) == 1);
  tgln_flush_out (c);

  return c;
}

static void restart_connection (struct connection *c) {
  struct tgl_state *TLS = c->TLS;
  if (c->last_connect_time == time (0)) {
    start_fail_timer (c);
    return;
  }
  
  c->last_connect_time = time (0);
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    vlogprintf (E_ERROR, "Can not create socket: %m\n");
    exit (1);
  }
  assert (fd >= 0 && fd < MAX_CONNECTIONS);
  if (fd > max_connection_fd) {
    max_connection_fd = fd;
  }
  int flags = -1;
  setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof (flags));
  setsockopt (fd, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof (flags));
  setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof (flags));

  struct sockaddr_in addr;
  addr.sin_family = AF_INET; 
  addr.sin_port = htons (c->port);
  if (strcmp (c->ip, c->dc->ip)) {
    tfree_str (c->ip);
    c->ip = tstrdup (c->dc->ip);
  }
  addr.sin_addr.s_addr = inet_addr (c->ip);


  fcntl (fd, F_SETFL, O_NONBLOCK);

  if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == -1) {
    if (errno != EINPROGRESS) {
      vlogprintf (E_WARNING, "Can not connect to %s:%d %m\n", c->ip, c->port);
      start_fail_timer (c);
      close (fd);
      return;
    }
  }

  c->fd = fd;
  c->state = conn_connecting;
  c->last_receive_time = tglt_get_double_time ();
  start_ping_timer (c);
  Connections[fd] = c;
  
  c->write_ev = event_new (TLS->ev_base, c->fd, EV_WRITE, conn_try_write, c);

  struct timeval tv = {5, 0};
  c->read_ev = event_new (TLS->ev_base, c->fd, EV_READ | EV_PERSIST, conn_try_read, c);
  event_add (c->read_ev, &tv);
  
  char byte = 0xef;
  assert (tgln_write_out (c, &byte, 1) == 1);
  tgln_flush_out (c);
}

static void fail_connection (struct connection *c) {
  struct tgl_state *TLS = c->TLS;
  if (c->state == conn_ready || c->state == conn_connecting) {
    stop_ping_timer (c);
  }
  event_free (c->write_ev);
  event_free (c->read_ev);
  
  rotate_port (c);
  struct connection_buffer *b = c->out_head;
  while (b) {
    struct connection_buffer *d = b;
    b = b->next;
    delete_connection_buffer (d);
  }
  b = c->in_head;
  while (b) {
    struct connection_buffer *d = b;
    b = b->next;
    delete_connection_buffer (d);
  }
  c->out_head = c->out_tail = c->in_head = c->in_tail = 0;
  c->state = conn_failed;
  c->out_bytes = c->in_bytes = 0;
  close (c->fd);
  Connections[c->fd] = 0;
  vlogprintf (E_NOTICE, "Lost connection to server... %s:%d\n", c->ip, c->port);
  restart_connection (c);
}

//extern FILE *log_net_f;
static void try_write (struct connection *c) {
  struct tgl_state *TLS = c->TLS;
  vlogprintf (E_DEBUG, "try write: fd = %d\n", c->fd);
  int x = 0;
  while (c->out_head) {
    int r = write (c->fd, c->out_head->rptr, c->out_head->wptr - c->out_head->rptr);
    if (r >= 0) {
      x += r;
      c->out_head->rptr += r;
      if (c->out_head->rptr != c->out_head->wptr) {
        break;
      }
      struct connection_buffer *b = c->out_head;
      c->out_head = b->next;
      if (!c->out_head) {
        c->out_tail = 0;
      }
      delete_connection_buffer (b);
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        vlogprintf (E_NOTICE, "fail_connection: write_error %m\n");
        fail_connection (c);
        return;
      } else {
        break;
      }
    }
  }
  vlogprintf (E_DEBUG, "Sent %d bytes to %d\n", x, c->fd);
  c->out_bytes -= x;
}

static void try_rpc_read (struct connection *c) {
  assert (c->in_head);
  struct tgl_state *TLS = c->TLS;

  while (1) {
    if (c->in_bytes < 1) { return; }
    unsigned len = 0;
    unsigned t = 0;
    assert (tgln_read_in_lookup (c, &len, 1) == 1);
    if (len >= 1 && len <= 0x7e) {
      if (c->in_bytes < (int)(1 + 4 * len)) { return; }
    } else {
      if (c->in_bytes < 4) { return; }
      assert (tgln_read_in_lookup (c, &len, 4) == 4);
      len = (len >> 8);
      if (c->in_bytes < (int)(4 + 4 * len)) { return; }
      len = 0x7f;
    }

    if (len >= 1 && len <= 0x7e) {
      assert (tgln_read_in (c, &t, 1) == 1);    
      assert (t == len);
      assert (len >= 1);
    } else {
      assert (len == 0x7f);
      assert (tgln_read_in (c, &len, 4) == 4);
      len = (len >> 8);
      assert (len >= 1);
    }
    len *= 4;
    int op;
    assert (tgln_read_in_lookup (c, &op, 4) == 4);
    c->methods->execute (TLS, c, op, len);
  }
}

static void try_read (struct connection *c) {
  struct tgl_state *TLS = c->TLS;
  vlogprintf (E_DEBUG, "try read: fd = %d\n", c->fd);
  if (!c->in_tail) {
    c->in_head = c->in_tail = new_connection_buffer (1 << 20);
  }
  #ifdef EVENT_V1
    struct timeval tv = {5, 0};
    event_add (c->read_ev, &tv);
  #endif
  int x = 0;
  while (1) {
    int r = read (c->fd, c->in_tail->wptr, c->in_tail->end - c->in_tail->wptr);
    if (r > 0) {
      c->last_receive_time = tglt_get_double_time ();
      stop_ping_timer (c);
      start_ping_timer (c);
    }
    if (r >= 0) {
      c->in_tail->wptr += r;
      x += r;
      if (c->in_tail->wptr != c->in_tail->end) {
        break;
      }
      struct connection_buffer *b = new_connection_buffer (1 << 20);
      c->in_tail->next = b;
      c->in_tail = b;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        vlogprintf (E_NOTICE, "fail_connection: read_error %m\n");
        fail_connection (c);
        return;
      } else {
        break;
      }
    }
  }
  vlogprintf (E_DEBUG, "Received %d bytes from %d\n", x, c->fd);
  c->in_bytes += x;
  if (x) {
    try_rpc_read (c);
  }
}
/*
int tgl_connections_make_poll_array (struct pollfd *fds, int max) {
  int _max = max;
  int i;
  for (i = 0; i <= max_connection_fd; i++) {
    if (Connections[i] && Connections[i]->state == conn_failed) {
      restart_connection (Connections[i]);
    }
    if (Connections[i] && Connections[i]->state != conn_failed) {
      assert (max > 0);
      struct connection *c = Connections[i];
      fds[0].fd = c->fd;
      fds[0].events = POLLERR | POLLHUP | POLLRDHUP | POLLIN;
      if (c->out_bytes || c->state == conn_connecting) {
        fds[0].events |= POLLOUT;
      }
      fds ++;
      max --;
    }
  }
  return _max - max;
}

void tgl_connections_poll_result (struct pollfd *fds, int max) {
  int i;
  for (i = 0; i < max; i++) {
    struct connection *c = Connections[fds[i].fd];
    if (fds[i].revents & POLLIN) {
      try_read (c);
    }
    if (fds[i].revents & (POLLHUP | POLLERR | POLLRDHUP)) {
      vlogprintf (E_NOTICE, "fail_connection: events_mask=0x%08x\n", fds[i].revents);
      fail_connection (c);
    } else if (fds[i].revents & POLLOUT) {
      if (c->state == conn_connecting) {
        vlogprintf (E_DEBUG, "connection ready\n");
        c->state = conn_ready;
        c->last_receive_time = tglt_get_double_time ();
      }
      if (c->out_bytes) {
        try_write (c);
      }
    }
  }
}*/

static void incr_out_packet_num (struct connection *c) {
  c->out_packet_num ++;
}

static struct tgl_dc *get_dc (struct connection *c) {
  return c->dc;
}

static struct tgl_session *get_session (struct connection *c) {
  return c->session;
}

static void tgln_free (struct connection *c) {
  if (c->ip) { tfree_str (c->ip); }
  struct connection_buffer *b = c->out_head;
  while (b) {
    struct connection_buffer *d = b;
    b = b->next;
    delete_connection_buffer (d);
  }
  b = c->in_head;
  while (b) {
    struct connection_buffer *d = b;
    b = b->next;
    delete_connection_buffer (d);
  }

  if (c->ping_ev) { event_free (c->ping_ev); }
  if (c->fail_ev) { event_free (c->fail_ev); }
  if (c->read_ev) { event_free (c->read_ev); }
  if (c->write_ev) { event_free (c->write_ev); }

  if (c->fd >= 0) { close (c->fd); }
  tfree (c, sizeof (*c));
}

struct tgl_net_methods tgl_conn_methods = {
  .write_out = tgln_write_out,
  .read_in = tgln_read_in,
  .read_in_lookup = tgln_read_in_lookup,
  .flush_out = tgln_flush_out,
  .incr_out_packet_num = incr_out_packet_num,
  .get_dc = get_dc,
  .get_session = get_session,
  .create_connection = tgln_create_connection,
  .free = tgln_free
};
