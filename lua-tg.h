/*
    This file is part of telegram-cli.

    Telegram-cli is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Telegram-cli is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this telegram-cli.  If not, see <http://www.gnu.org/licenses/>.

    Copyright Vitaly Valtman 2013-2015
*/
#ifndef __LUA_TG_H__
#define __LUA_TG_H__

#include <string.h>
#include <tgl/tgl.h>

void lua_init (const char *file);
void lua_new_msg (struct tgl_message *M);
void lua_our_id (tgl_peer_id_t id);
void lua_secret_chat_update (struct tgl_secret_chat *U, unsigned flags);
void lua_user_update (struct tgl_user *U, unsigned flags);
void lua_chat_update (struct tgl_chat *C, unsigned flags);
void lua_binlog_end (void);
void lua_diff_end (void);
void lua_do_all (void);
#endif
