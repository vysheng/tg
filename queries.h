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

struct event;

#define QUERY_ACK_RECEIVED 1

struct query;
struct query_methods {
  int (*on_answer)(struct query *q);
  int (*on_error)(struct query *q, int error_code, int len, char *error);
  int (*on_timeout)(struct query *q);
  struct paramed_type *type;
};

struct query {
  long long msg_id;
  int data_len;
  int flags;
  int seq_no;
  void *data;
  struct query_methods *methods;
  struct event *ev;
  struct tgl_dc *DC;
  struct tgl_session *session;
  void *extra;
  void *callback;
  void *callback_extra;
};


struct query *tglq_send_query (struct tgl_dc *DC, int len, void *data, struct query_methods *methods, void *extra, void *callback, void *callback_extra);
void tglq_query_ack (long long id);
void tglq_query_error (long long id);
void tglq_query_result (long long id);
void tglq_query_restart (long long id);

double next_timer_in (void);
void work_timers (void);

//extern struct query_methods help_get_config_methods;

double get_double_time (void);


// For binlog

//int get_dh_config_on_answer (struct query *q);
//void fetch_dc_option (void);
#endif
