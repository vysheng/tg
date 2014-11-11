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

#ifndef __UPDATES_H__
#define __UPDATES_H__
struct connection;
void tglu_work_update (struct tgl_state *TLS, struct connection *c, long long msg_id);
void tglu_work_updates_to_long (struct tgl_state *TLS, struct connection *c, long long msg_id);
void tglu_work_update_short_chat_message (struct tgl_state *TLS, struct connection *c, long long msg_id);
void tglu_work_update_short_message (struct tgl_state *TLS, struct connection *c, long long msg_id);
void tglu_work_update_short (struct tgl_state *TLS, struct connection *c, long long msg_id);
void tglu_work_updates (struct tgl_state *TLS, struct connection *c, long long msg_id);

void tglu_fetch_pts (struct tgl_state *TLS);
void tglu_fetch_qts (struct tgl_state *TLS);
void tglu_fetch_seq (struct tgl_state *TLS);
void tglu_fetch_date (struct tgl_state *TLS);

void tgl_insert_status_update (struct tgl_state *TLS, struct tgl_user *U);
void tgl_insert_status_expire (struct tgl_state *TLS, struct tgl_user *U);
void tgl_remove_status_expire (struct tgl_state *TLS, struct tgl_user *U);
#endif
