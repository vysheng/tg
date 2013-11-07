/*
    This file is part of telegram-client.

    Telegram-client is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Telegram-client is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this telegram-client.  If not, see <http://www.gnu.org/licenses/>.

    Copyright Vitaly Valtman 2013
*/
#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <openssl/rand.h>
#include <arpa/inet.h>

#include "net.h"
#include "include.h"
#include "mtproto-client.h"
#include "mtproto-common.h"
#include "tree.h"
#include "interface.h"

DEFINE_TREE(int,int,int_cmp,0)

int verbosity;
extern struct connection_methods auth_methods;

void fail_connection (struct connection *c);

#define PING_TIMEOUT 10

void start_ping_timer (struct connection *c);
int ping_alarm (struct connection *c) {
  if (verbosity > 2) {
    logprintf ("ping alarm\n");
  }
  if (get_double_time () - c->last_receive_time > 20 * PING_TIMEOUT) {
    if (verbosity) {
      logprintf ( "fail connection: reason: ping timeout\n");
    }
    c->state = conn_failed;
    fail_connection (c);
  } else if (get_double_time () - c->last_receive_time > 5 * PING_TIMEOUT && c->state == conn_ready) {
    int x[3];
    x[0] = CODE_ping;
    *(long long *)(x + 1) = lrand48 () * (1ll << 32) + lrand48 ();
    encrypt_send_message (c, x, 3, 0);
    start_ping_timer (c);
  } else {
    start_ping_timer (c);
  }
  return 0;
}

void stop_ping_timer (struct connection *c) {
  remove_event_timer (&c->ev);
}

void start_ping_timer (struct connection *c) {
  c->ev.timeout = get_double_time () + PING_TIMEOUT;
  c->ev.alarm = (void *)ping_alarm;
  c->ev.self = c;
  insert_event_timer (&c->ev);
}

void restart_connection (struct connection *c);
int fail_alarm (void *ev) {
  restart_connection (ev);
  return 0;
}
void start_fail_timer (struct connection *c) {
  c->ev.timeout = get_double_time () + 10;
  c->ev.alarm = (void *)fail_alarm;
  c->ev.self = c;
  insert_event_timer (&c->ev);
}

struct connection_buffer *new_connection_buffer (int size) {
  struct connection_buffer *b = malloc (sizeof (*b));
  memset (b, 0, sizeof (*b));
  b->start = malloc (size);
  b->end = b->start + size;
  b->rptr = b->wptr = b->start;
  return b;
}

void delete_connection_buffer (struct connection_buffer *b) {
  free (b->start);
  free (b);
}

int write_out (struct connection *c, const void *data, int len) {
  if (!len) { return 0; }
  assert (len > 0);
  int x = 0;
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

int read_in (struct connection *c, void *data, int len) {
  if (!len) { return 0; }
  assert (len > 0);
  if (len > c->in_bytes) {
    len = c->in_bytes;
  }
  int x = 0;
  while (len) {
    int y = c->in_head->wptr - c->in_head->rptr;
    if (y >= len) {
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

int read_in_lookup (struct connection *c, void *data, int len) {
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
      b = b->next;
    }
  }
  return x;
}

void flush_out (struct connection *c UU) {
}

#define MAX_CONNECTIONS 100
struct connection *Connections[MAX_CONNECTIONS];
int max_connection_fd;

struct connection *create_connection (const char *host, int port, struct session *session, struct connection_methods *methods) {
  struct connection *c = malloc (sizeof (*c));
  memset (c, 0, sizeof (*c));
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    logprintf ("Can not create socket: %m\n");
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
    if (errno != EINPROGRESS) {
      logprintf ( "Can not connect to %s:%d %m\n", host, port);
      close (fd);
      free (c);
      return 0;
    }
  }

  struct pollfd s;
  s.fd = fd;
  s.events = POLLOUT | POLLERR | POLLRDHUP | POLLHUP;
  
  while (poll (&s, 1, 10000) <= 0 || !(s.revents & POLLOUT)) {
    if (errno == EINTR) { continue; }
    if (errno) {
      logprintf ("Problems in poll: %m\n");
      exit (1);
    }
    logprintf ("Connect timeout\n");
    close (fd);
    free (c);
    return 0;
  }

  c->session = session;
  c->fd = fd; 
  c->ip = strdup (host);
  c->flags = 0;
  c->state = conn_ready;
  c->methods = methods;
  c->port = port;
  assert (!Connections[fd]);
  Connections[fd] = c;
  if (verbosity) {
    logprintf ( "connect to %s:%d successful\n", host, port);
  }
  if (c->methods->ready) {
    c->methods->ready (c);
  }
  c->last_receive_time = get_double_time ();
  start_ping_timer (c);
  return c;
}

void restart_connection (struct connection *c) {
  if (c->last_connect_time == time (0)) {
    start_fail_timer (c);
    return;
  }
  
  c->last_connect_time = time (0);
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    logprintf ("Can not create socket: %m\n");
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
  addr.sin_addr.s_addr = inet_addr (c->ip);


  fcntl (fd, F_SETFL, O_NONBLOCK);

  if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == -1) {
    if (errno != EINPROGRESS) {
      logprintf ( "Can not connect to %s:%d %m\n", c->ip, c->port);
      start_fail_timer (c);
      close (fd);
      return;
    }
  }

  c->fd = fd;
  c->state = conn_connecting;
  c->last_receive_time = get_double_time ();
  start_ping_timer (c);
  Connections[fd] = c;
  
  char byte = 0xef;
  assert (write_out (c, &byte, 1) == 1);
  flush_out (c);
}

void fail_connection (struct connection *c) {
  if (c->state == conn_ready || c->state == conn_connecting) {
    stop_ping_timer (c);
  }
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
  logprintf ("Lost connection to server... \n");
  restart_connection (c);
}

void try_write (struct connection *c) {
  if (verbosity) {
    logprintf ( "try write: fd = %d\n", c->fd);
  }
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
        if (verbosity) {
          logprintf ("fail_connection: write_error %m\n");
        }
        fail_connection (c);
        return;
      } else {
        break;
      }
    }
  }
  if (verbosity) {
    logprintf ( "Sent %d bytes to %d\n", x, c->fd);
  }
  c->out_bytes -= x;
}

void hexdump_buf (struct connection_buffer *b) {
  int pos = 0;
  int rem = 8;
  while (b) { 
    unsigned char *c = b->rptr;
    while (c != b->wptr) {
      if (rem == 8) {
        if (pos) { printf ("\n"); }
        printf ("%04d", pos);
      }
      printf (" %02x", (int)*c);
      rem --;
      pos ++;
      if (!rem) {
        rem = 8;
      }
      c ++;
    }
    b = b->next;
  }
  printf ("\n");
    
}

void try_rpc_read (struct connection *c) {
  assert (c->in_head);
  if (verbosity >= 4) {
    hexdump_buf (c->in_head);
  }

  while (1) {
    if (c->in_bytes < 1) { return; }
    unsigned len = 0;
    unsigned t = 0;
    assert (read_in_lookup (c, &len, 1) == 1);
    if (len >= 1 && len <= 0x7e) {
      if (c->in_bytes < (int)(1 + 4 * len)) { return; }
    } else {
      if (c->in_bytes < 4) { return; }
      assert (read_in_lookup (c, &len, 4) == 4);
      len = (len >> 8);
      if (c->in_bytes < (int)(4 + 4 * len)) { return; }
      len = 0x7f;
    }

    if (len >= 1 && len <= 0x7e) {
      assert (read_in (c, &t, 1) == 1);    
      assert (t == len);
      assert (len >= 1);
    } else {
      assert (len == 0x7f);
      assert (read_in (c, &len, 4) == 4);
      len = (len >> 8);
      assert (len >= 1);
    }
    len *= 4;
    int op;
    assert (read_in_lookup (c, &op, 4) == 4);
    c->methods->execute (c, op, len);
  }
}

void try_read (struct connection *c) {
  if (verbosity) {
    logprintf ( "try read: fd = %d\n", c->fd);
  }
  if (!c->in_tail) {
    c->in_head = c->in_tail = new_connection_buffer (1 << 20);
  }
  int x = 0;
  while (1) {
    int r = read (c->fd, c->in_tail->wptr, c->in_tail->end - c->in_tail->wptr);
    if (r > 0) {
      c->last_receive_time = get_double_time ();
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
        if (verbosity) {
          logprintf ("fail_connection: read_error %m\n");
        }
        fail_connection (c);
        return;
      } else {
        break;
      }
    }
  }
  if (verbosity) {
    logprintf ( "Received %d bytes from %d\n", x, c->fd);
  }
  c->in_bytes += x;
  if (x) {
    try_rpc_read (c);
  }
}

int connections_make_poll_array (struct pollfd *fds, int max) {
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
  if (verbosity >= 10) {
    logprintf ( "%d connections in poll\n", _max - max);
  }
  return _max - max;
}

void connections_poll_result (struct pollfd *fds, int max) {
  if (verbosity >= 10) {
    logprintf ( "connections_poll_result: max = %d\n", max);
  }
  int i;
  for (i = 0; i < max; i++) {
    struct connection *c = Connections[fds[i].fd];
    if (fds[i].revents & POLLIN) {
      try_read (c);
    }
    if (fds[i].revents & (POLLHUP | POLLERR | POLLRDHUP)) {
      if (verbosity) {
        logprintf ("fail_connection: events_mask=0x%08x\n", fds[i].revents);
      }
      fail_connection (c);
    } else if (fds[i].revents & POLLOUT) {
      if (c->state == conn_connecting) {
        logprintf ("connection ready\n");
        c->state = conn_ready;
        c->last_receive_time = get_double_time ();
      }
      if (c->out_bytes) {
        try_write (c);
      }
    }
  }
}

int send_all_acks (struct session *S) {
  clear_packet ();
  out_int (CODE_msgs_ack);
  out_int (tree_count_int (S->ack_tree));
  while (S->ack_tree) {
    int x = tree_get_min_int (S->ack_tree); 
    out_int (x);
    S->ack_tree = tree_delete_int (S->ack_tree, x);
  }
  encrypt_send_message (S->c, packet_buffer, packet_ptr - packet_buffer, 0);
  return 0;
}

void insert_seqno (struct session *S, int seqno) {
  if (!S->ack_tree) {
    S->ev.alarm = (void *)send_all_acks;
    S->ev.self = (void *)S;
    S->ev.timeout = get_double_time () + ACK_TIMEOUT;
    insert_event_timer (&S->ev);
  }
  if (!tree_lookup_int (S->ack_tree, seqno)) {
    S->ack_tree = tree_insert_int (S->ack_tree, seqno, lrand48 ());
  }
}

extern struct dc *DC_list[];

struct dc *alloc_dc (int id, char *ip, int port) {
  assert (!DC_list[id]);
  struct dc *DC = malloc (sizeof (*DC));
  memset (DC, 0, sizeof (*DC));
  DC->id = id;
  DC->ip = ip;
  DC->port = port;
  DC_list[id] = DC;
  return DC;
}

void dc_create_session (struct dc *DC) {
  struct session *S = malloc (sizeof (*S));
  memset (S, 0, sizeof (*S));
  assert (RAND_pseudo_bytes ((unsigned char *) &S->session_id, 8) >= 0);
  S->dc = DC;
  S->c = create_connection (DC->ip, DC->port, S, &auth_methods);
  if (!S->c) {
    logprintf ("Can not create connection to DC. Is network down?\n");
    exit (1);
  }
  assert (!DC->sessions[0]);
  DC->sessions[0] = S;
}
