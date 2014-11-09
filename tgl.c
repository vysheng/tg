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

    Copyright Vitaly Valtman 2014
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "tgl.h"
#include "tools.h"
#include "mtproto-client.h"
#include "structures.h"
//#include "net.h"

#include <assert.h>

struct tgl_state tgl_state;

    
void tgl_set_binlog_mode (struct tgl_state *TLS, int mode) {
  TLS->binlog_enabled = mode;
}

void tgl_set_binlog_path (struct tgl_state *TLS, const char *path) {
  TLS->binlog_name = tstrdup (path);
}
    
void tgl_set_auth_file_path (struct tgl_state *TLS, const char *path) {
  TLS->auth_file = tstrdup (path);
}

void tgl_set_download_directory (struct tgl_state *TLS, const char *path) {
  TLS->downloads_directory = tstrdup (path);
}

void tgl_set_callback (struct tgl_state *TLS, struct tgl_update_callback *cb) {
  TLS->callback = *cb;
}

void tgl_set_rsa_key (struct tgl_state *TLS, const char *key) {
  assert (TLS->rsa_key_num < TGL_MAX_RSA_KEYS_NUM);
  TLS->rsa_key_list[TLS->rsa_key_num ++] = tstrdup (key);
}

void tgl_init (struct tgl_state *TLS) {
  assert (TLS->timer_methods);
  assert (TLS->net_methods);
  if (!TLS->callback.create_print_name) {
    TLS->callback.create_print_name = tgls_default_create_print_name;
  }
  if (!TLS->temp_key_expire_time) {
    TLS->temp_key_expire_time = 100000;
  }

  TLS->message_list.next_use = &TLS->message_list;
  TLS->message_list.prev_use = &TLS->message_list;

  tglmp_on_start (TLS);
}

int tgl_authorized_dc (struct tgl_state *TLS, struct tgl_dc *DC) {
  assert (DC);
  return (DC->flags & 4) != 0;//DC->auth_key_id;
}

int tgl_signed_dc (struct tgl_state *TLS, struct tgl_dc *DC) {
  assert (DC);
  return DC->has_auth;
}

