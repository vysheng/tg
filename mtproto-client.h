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

    Copyright Nikolay Durov, Andrey Lopatin 2012-2013
    Copyright Vitaly Valtman 2013
*/
#ifndef __MTPROTO_CLIENT_H__
#define __MTPROTO_CLIENT_H__
//#include "net.h"
#include <openssl/bn.h>
//void on_start (void);
//..long long encrypt_send_message (struct connection *c, int *msg, int msg_ints, int useful);
//void dc_authorize (struct dc *DC);
//void work_update (struct connection *c, long long msg_id);
//void work_update_binlog (void);
//int check_g (unsigned char p[256], BIGNUM *g);
//int check_g_bn (BIGNUM *p, BIGNUM *g);
//int check_DH_params (BIGNUM *p, int g);
//void secure_random (void *s, int l);

struct connection;
struct dc;
//#include "queries.h"
#define TG_SERVER "173.240.5.1"
#define TG_SERVER_TEST "173.240.5.253"
#define TG_APP_HASH "36722c72256a24c1225de00eb6a1ca74"
#define TG_APP_ID 2899

#define ACK_TIMEOUT 1
#define MAX_DC_ID 10

struct connection;

enum dc_state {
  st_init,
  st_reqpq_sent,
  st_reqdh_sent,
  st_client_dh_sent,
  st_authorized,
  st_error
};

#define MAX_DC_SESSIONS 3

struct session {
  struct dc *dc;
  long long session_id;
  int seq_no;
  struct connection *c;
  struct tree_long *ack_tree;
  struct event *ev;
  //struct event_timer ev;
};

struct dc {
  int id;
  int port;
  int flags;
  enum dc_state state;
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

long long tglmp_encrypt_send_message (struct connection *c, int *msg, int msg_ints, int useful);
void tglmp_dc_create_session (struct dc *DC);
int tglmp_check_g (unsigned char p[256], BIGNUM *g);
int tglmp_check_DH_params (BIGNUM *p, int g);
struct dc *tglmp_alloc_dc (int id, char *ip, int port);

void tgln_insert_msg_id (struct session *S, long long id);
void tglmp_on_start (const char *key);
void tgl_dc_authorize (struct dc *DC);
#endif
