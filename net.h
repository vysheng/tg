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
#ifndef __NET_H__
#define __NET_H__

#include <poll.h>
struct dc;
#include "queries.h"
#define TG_SERVER "173.240.5.1"
#define TG_SERVER_TEST "173.240.5.253"
#define TG_APP_HASH "36722c72256a24c1225de00eb6a1ca74"
#define TG_APP_ID 2899
#define TG_BUILD "203"

#define TG_VERSION "0.01-beta"

#define ACK_TIMEOUT 60
#define MAX_DC_ID 10

enum dc_state {
  st_init,
  st_reqpq_sent,
  st_reqdh_sent,
  st_client_dh_sent,
  st_authorized,
  st_error
} ;

struct connection;
struct connection_methods {
  int (*ready) (struct connection *c);
  int (*close) (struct connection *c);
  int (*execute) (struct connection *c, int op, int len);
};


#define MAX_DC_SESSIONS 3

struct session {
  struct dc *dc;
  long long session_id;
  int seq_no;
  struct connection *c;
  struct tree_int *ack_tree;
  struct event_timer ev;
};

struct dc {
  int id;
  int port;
  int flags;
  char *ip;
  char *user;
  struct session *sessions[MAX_DC_SESSIONS];
  char auth_key[256];
  long long auth_key_id;
  long long server_salt;

  int server_time_delta;
  double server_time_udelta;
  int has_auth;
};

#define DC_SERIALIZED_MAGIC 0x64582faa
#define DC_SERIALIZED_MAGIC_V2 0x94032abb
#define STATE_FILE_MAGIC 0x84217a0d
#define SECRET_CHAT_FILE_MAGIC 0xa9840add

struct dc_serialized {
  int magic;
  int port;
  char ip[64];
  char user[64];
  char auth_key[256];
  long long auth_key_id, server_salt;
  int authorized;
};

struct connection_buffer {
  void *start;
  void *end;
  void *rptr;
  void *wptr;
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
  struct connection_methods *methods;
  struct session *session;
  void *extra;
  struct event_timer ev;
  double last_receive_time;
};

extern struct connection *Connections[];

int write_out (struct connection *c, const void *data, int len);
void flush_out (struct connection *c);
int read_in (struct connection *c, void *data, int len);

void create_all_outbound_connections (void);

struct connection *create_connection (const char *host, int port, struct session *session, struct connection_methods *methods);
int connections_make_poll_array (struct pollfd *fds, int max);
void connections_poll_result (struct pollfd *fds, int max);
void dc_create_session (struct dc *DC);
void insert_seqno (struct session *S, int seqno);
struct dc *alloc_dc (int id, char *ip, int port);

#define GET_DC(c) (c->session->dc)
#endif
