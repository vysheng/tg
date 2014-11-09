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
#ifndef __NET_H__
#define __NET_H__

struct connection_buffer {
  unsigned char *start;
  unsigned char *end;
  unsigned char *rptr;
  unsigned char *wptr;
  struct connection_buffer *next;
};

enum conn_state {
  conn_none,
  conn_connecting,
  conn_ready,
  conn_failed,
  conn_stopped
};

struct connection {
  int fd;
  char *ip;
  int port;
  int flags;
  enum conn_state state;
  int ipv6[4];
  struct connection_buffer *in_head;
  struct connection_buffer *in_tail;
  struct connection_buffer *out_head;
  struct connection_buffer *out_tail;
  int in_bytes;
  int out_bytes;
  int packet_num;
  int out_packet_num;
  int last_connect_time;
  int in_fail_timer;
  struct mtproto_methods *methods;
  struct tgl_state *TLS;
  struct tgl_session *session;
  struct tgl_dc *dc;
  void *extra;
  struct event *ping_ev;
  struct event *fail_ev;
  struct event *read_ev;
  struct event *write_ev;
  double last_receive_time;
};

//extern struct connection *Connections[];

int tgln_write_out (struct connection *c, const void *data, int len);
void tgln_flush_out (struct connection *c);
int tgln_read_in (struct connection *c, void *data, int len);
int tgln_read_in_lookup (struct connection *c, void *data, int len);

//void tgln_insert_msg_id (struct tgl_session *S, long long id);

extern struct tgl_net_methods tgl_conn_methods;

//void create_all_outbound_connections (void);

//struct connection *create_connection (const char *host, int port, struct tgl_session *session, struct connection_methods *methods);
//struct tgl_dc *tgln_alloc_dc (int id, char *ip, int port);
//void tgln_dc_create_session (struct tgl_dc *DC, struct mtproto_methods *methods);
struct connection *tgln_create_connection (struct tgl_state *TLS, const char *host, int port, struct tgl_session *session, struct tgl_dc *dc, struct mtproto_methods *methods);

#define GET_DC(c) (c->session->dc)
#endif
