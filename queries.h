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
//#include "net.h"
#ifndef __QUERIES_H__
#define __QUERIES_H__
#include "structures.h"
#include "auto.h"
#include "tgl-layout.h"

#define QUERY_ACK_RECEIVED 1
#define QUERY_FORCE_SEND 2

struct query;
struct query_methods {
  int (*on_answer)(struct tgl_state *TLS, struct query *q);
  int (*on_error)(struct tgl_state *TLS, struct query *q, int error_code, int len, char *error);
  int (*on_timeout)(struct tgl_state *TLS, struct query *q);
  struct paramed_type *type;
};

struct query {
  long long msg_id;
  int data_len;
  int flags;
  int seq_no;
  long long session_id;
  void *data;
  struct query_methods *methods;
  struct tgl_timer *ev;
  struct tgl_dc *DC;
  struct tgl_session *session;
  struct paramed_type *type;
  void *extra;
  void *callback;
  void *callback_extra;
};


struct query *tglq_send_query (struct tgl_state *TLS, struct tgl_dc *DC, int len, void *data, struct query_methods *methods, void *extra, void *callback, void *callback_extra);
void tglq_query_ack (struct tgl_state *TLS, long long id);
void tglq_query_error (struct tgl_state *TLS, long long id);
void tglq_query_result (struct tgl_state *TLS, long long id);
void tglq_query_restart (struct tgl_state *TLS, long long id);

//double next_timer_in (void);
//void work_timers (void);

//extern struct query_methods help_get_config_methods;

double get_double_time (void);

void tgl_do_send_bind_temp_key (struct tgl_state *TLS, struct tgl_dc *D, long long nonce, int expires_at, void *data, int len, long long msg_id);

// For binlog

//int get_dh_config_on_answer (struct query *q);
//void fetch_dc_option (void);
#endif
