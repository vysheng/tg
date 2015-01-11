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
#ifndef __LOOP_H__
#define __LOOP_H__
#define TELEGRAM_CLI_APP_HASH "36722c72256a24c1225de00eb6a1ca74"
#define TELEGRAM_CLI_APP_ID 2899

int loop (void);
void do_halt (int error);
void write_auth_file (void);
void write_state_file (void);
void write_secret_chat_file (void);
#endif
