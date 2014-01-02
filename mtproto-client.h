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
#include "net.h"
#include <openssl/bn.h>
void on_start (void);
long long encrypt_send_message (struct connection *c, int *msg, int msg_ints, int useful);
void dc_authorize (struct dc *DC);
void work_update (struct connection *c, long long msg_id);
void work_update_binlog (void);
int check_g (unsigned char p[256], BIGNUM *g);
int check_g_bn (BIGNUM *p, BIGNUM *g);
int check_DH_params (BIGNUM *p, int g);
void secure_random (void *s, int l);
#endif
