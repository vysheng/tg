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

#include <Python.h>
#include <string.h>
#include <tgl/tgl.h>

// Python functions
void py_init (const char *file);
void py_new_msg (struct tgl_message *M);
void py_our_id (int id);
void py_secret_chat_update (struct tgl_secret_chat *U, unsigned flags);
void py_user_update (struct tgl_user *U, unsigned flags);
void py_chat_update (struct tgl_chat *C, unsigned flags);
void py_binlog_end (void);
void py_diff_end (void);
void py_do_all (void);

// Binding functions
PyObject* py_contact_list(PyObject *self, PyObject *args);
PyObject* py_dialog_list(PyObject *self, PyObject *args);
PyObject* py_rename_chat(PyObject *self, PyObject *args);
PyObject* py_send_msg(PyObject *self, PyObject *args);
PyObject* py_send_typing(PyObject *self, PyObject *args);
PyObject* py_send_typing_abort(PyObject *self, PyObject *args);
PyObject* py_send_photo(PyObject *self, PyObject *args);
PyObject* py_send_video(PyObject *self, PyObject *args);
PyObject* py_send_audio(PyObject *self, PyObject *args);
PyObject* py_send_document(PyObject *self, PyObject *args);
PyObject* py_send_file(PyObject *self, PyObject *args);
PyObject* py_send_text(PyObject *self, PyObject *args);
PyObject* py_chat_set_photo(PyObject *self, PyObject *args);
PyObject* py_load_photo(PyObject *self, PyObject *args);
PyObject* py_load_video(PyObject *self, PyObject *args);
PyObject* py_load_video_thumb(PyObject *self, PyObject *args);
PyObject* py_load_audio(PyObject *self, PyObject *args);
PyObject* py_load_document(PyObject *self, PyObject *args);
PyObject* py_load_document_thumb(PyObject *self, PyObject *args);
PyObject* py_fwd(PyObject *self, PyObject *args);
PyObject* py_fwd_media(PyObject *self, PyObject *args);
PyObject* py_chat_info(PyObject *self, PyObject *args);
PyObject* py_user_info(PyObject *self, PyObject *args);
PyObject* py_history(PyObject *self, PyObject *args);
PyObject* py_chat_add_user(PyObject *self, PyObject *args);
PyObject* py_chat_del_user(PyObject *self, PyObject *args);
PyObject* py_add_contact(PyObject *self, PyObject *args);
PyObject* py_del_contact(PyObject *self, PyObject *args);
PyObject* py_rename_contact(PyObject *self, PyObject *args);
PyObject* py_search(PyObject *self, PyObject *args);
PyObject* py_global_search(PyObject *self, PyObject *args);
PyObject* py_mark_read(PyObject *self, PyObject *args);
PyObject* py_set_profile_photo(PyObject *self, PyObject *args);
PyObject* py_set_profile_name(PyObject *self, PyObject *args);
PyObject* py_create_secret_chat(PyObject *self, PyObject *args);
PyObject* py_create_group_chat(PyObject *self, PyObject *args);
PyObject* py_delete_msg(PyObject *self, PyObject *args);
PyObject* py_restore_msg(PyObject *self, PyObject *args);
PyObject* py_accept_secret_chat(PyObject *self, PyObject *args);
PyObject* py_send_contact(PyObject *self, PyObject *args);
PyObject* py_status_online(PyObject *self, PyObject *args);
PyObject* py_status_offline(PyObject *self, PyObject *args);
PyObject* py_send_location(PyObject *self, PyObject *args);
PyObject* py_extf(PyObject *self, PyObject *args);


// Util Functions
void py_add_string_field (PyObject* dict, char *name, const char *value);
void py_add_string_field_arr (PyObject* list, int num, const char *value);
void py_add_num_field (PyObject* dict, const char *name, double value);
#endif
