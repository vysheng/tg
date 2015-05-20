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
    Copyright Vincent Castellano 2015
*/
#ifndef __PYTHON_TG_H__
#define __PYTHON_TG_H__

#include <string.h>
#include <Python.h>
#include <tgl/tgl.h>

void py_init (const char *file);
void py_new_msg (struct tgl_message *M);
void py_our_id (int id);
void py_secret_chat_update (struct tgl_secret_chat *U, unsigned flags);
void py_user_update (struct tgl_user *U, unsigned flags);
void py_chat_update (struct tgl_chat *C, unsigned flags);
void py_binlog_end (void);
void py_diff_end (void);
void py_do_all (void);


void py_add_string_field (PyObject* dict, char *name, const char *value);
void py_add_string_field_arr (PyObject* list, int num, const char *value);
void py_add_num_field (PyObject* dict, const char *name, double value);
#endif
