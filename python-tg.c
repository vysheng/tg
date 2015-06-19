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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef USE_PYTHON
#include "python-tg.h"

#include <string.h>
#include <stdlib.h>
#include <libgen.h>

#include <Python.h>
#include "bytesobject.h"

// Python 2/3 compat macros
#if PY_MAJOR_VERSION >= 3
  #define MOD_ERROR_VAL NULL
  #define MOD_SUCCESS_VAL(val) val
  #define MOD_INIT(name) PyMODINIT_FUNC PyInit_##name(void)
  #define MOD_DEF(ob, name, doc, methods) \
          static struct PyModuleDef moduledef = { \
          PyModuleDef_HEAD_INIT, name, doc, -1, methods, NULL, NULL, NULL, NULL,}; \
          ob = PyModule_Create(&moduledef);
  #define PyInt_FromLong PyLong_FromLong
#else
  #define MOD_ERROR_VAL
  #define MOD_SUCCESS_VAL(val)
  #define MOD_INIT(name) void init##name(void)
  #define MOD_DEF(ob, name, doc, methods) \
          ob = Py_InitModule3(name, methods, doc);
#endif

#define TGL_PYTHON_CALLBACK(name, func) \
      PyObject *set##func(PyObject *dummy, PyObject *args) { \
      PyObject *result = NULL; \
      PyObject *temp; \
      if (PyArg_ParseTuple(args, "O:set_##name", &temp)) { \
        if (!PyCallable_Check(temp)) { \
          PyErr_SetString(PyExc_TypeError, "parameter must be callable");\
          return NULL;\
        }\
        Py_XINCREF(temp);\
        Py_XDECREF(func);\
        func = temp;\
        Py_INCREF(Py_None);\
        result = Py_None;\
        }\
        return result;\
      }


// Python Imports
#include "datetime.h"

// Custom Types
#include "python-types.h"


extern PyTypeObject tgl_PeerType;
extern PyTypeObject tgl_MsgType;

//#include "interface.h"
//#include "auto/constants.h"
#include <tgl/tgl.h>
#include "interface.h"

#include <assert.h>
extern int verbosity;
extern struct tgl_state *TLS;


static int python_loaded;

// TGL Python Exceptions
PyObject *TglError;
PyObject *PeerError;
PyObject *MsgError;


// Python update function callables
PyObject *_py_binlog_end;
PyObject *_py_diff_end;
PyObject *_py_our_id;
PyObject *_py_new_msg;
PyObject *_py_secret_chat_update;
PyObject *_py_user_update;
PyObject *_py_chat_update;
PyObject *_py_on_loop;

PyObject* get_peer (tgl_peer_id_t id, tgl_peer_t *P);

void py_add_string_field (PyObject* dict, char *name, const char *value) {
  assert (PyDict_Check(dict));
  assert (name && strlen (name));
  if (!value || !strlen (value)) { return; }
  PyObject *str = PyUnicode_FromString(value);

  if(PyUnicode_Check(str))
    PyDict_SetItemString (dict, name, str);
}

void py_add_string_field_arr (PyObject* list, int num, const char *value) {
  assert(PyList_Check(list));
  if (!value || !strlen (value)) { return; }
  if(num >= 0)
    PyList_SetItem (list, num, PyUnicode_FromString (value));
  else // Append
    PyList_Append  (list, PyUnicode_FromString (value));
}

void py_add_num_field (PyObject* dict, const char *name, double value) {
  assert (PyDict_Check(dict));
  assert (name && strlen (name));
  PyDict_SetItemString (dict, name, PyFloat_FromDouble(value));
}

PyObject* get_tgl_peer_type (int x) {
  PyObject *type;

  switch (x) {
  case TGL_PEER_USER:
    type = PyUnicode_FromString("user");
    break;
  case TGL_PEER_CHAT:
    type = PyUnicode_FromString("chat");
    break;
  case TGL_PEER_ENCR_CHAT:
    type = PyUnicode_FromString("encr_chat");
    break;
  default:
    assert (0);
  }

  return type;
}

PyObject* get_update_types (unsigned flags) {
  PyObject* types;
  types = PyList_New(0); 
  if(types == NULL)
    assert(0); // TODO handle python exception
  
  if (flags & TGL_UPDATE_CREATED) {
    py_add_string_field_arr(types, -1, "created");
  }  
  if (flags & TGL_UPDATE_DELETED) {
    py_add_string_field_arr(types, -1, "deleted");
  }  
  if (flags & TGL_UPDATE_PHONE) {
    py_add_string_field_arr(types, -1, "phone");
  }
  if (flags & TGL_UPDATE_CONTACT) {
    py_add_string_field_arr(types, -1, "contact");
  }
  if (flags & TGL_UPDATE_PHOTO) {
    py_add_string_field_arr(types, -1, "photo");
  }
  if (flags & TGL_UPDATE_BLOCKED) {
    py_add_string_field_arr(types, -1, "blocked");
  }
  if (flags & TGL_UPDATE_REAL_NAME) {
    py_add_string_field_arr(types, -1, "real_name");
  }
  if (flags & TGL_UPDATE_NAME) {
    py_add_string_field_arr(types, -1, "name");
  }
  if (flags & TGL_UPDATE_REQUESTED) {
    py_add_string_field_arr(types, -1, "requested");
  }
  if (flags & TGL_UPDATE_WORKING) {
    py_add_string_field_arr(types, -1, "working");
  }
  if (flags & TGL_UPDATE_FLAGS) {
    py_add_string_field_arr(types, -1, "flags");
  }
  if (flags & TGL_UPDATE_TITLE) {
    py_add_string_field_arr(types, -1, "title");
  }
  if (flags & TGL_UPDATE_ADMIN) {
    py_add_string_field_arr(types, -1, "admin");
  }
  if (flags & TGL_UPDATE_MEMBERS) {
    py_add_string_field_arr(types, -1, "members");
  }
  if (flags & TGL_UPDATE_ACCESS_HASH) {
    py_add_string_field_arr(types, -1, "access_hash");
  }
  if (flags & TGL_UPDATE_USERNAME) {
    py_add_string_field_arr(types, -1, "username");
  }
  return types;
}

PyObject* get_peer (tgl_peer_id_t id, tgl_peer_t *P) {
  PyObject *peer;

  peer = tgl_Peer_FromTglPeer(P);
  return peer;
}

PyObject* get_message (struct tgl_message *M) {  
  assert (M);
  PyObject *msg;
  
  msg = tgl_Msg_FromTglMsg(M);
  return msg;
}

void py_binlog_end (void) {
  if (!python_loaded) { return; }

  PyObject *arglist, *result;

  if(_py_binlog_end == NULL) {
    logprintf("Callback not set for on_binlog_end");
    return;
  }
 
  arglist = Py_BuildValue("()");
  result = PyEval_CallObject(_py_binlog_end, arglist);
  Py_DECREF(arglist);
  
  if(result == NULL)
    PyErr_Print();
  else if(PyUnicode_Check(result))
    logprintf ("python: %s\n", PyBytes_AsString(PyUnicode_AsASCIIString(result)));

  Py_XDECREF(result);
}

void py_diff_end (void) {
  if (!python_loaded) { return; }

  PyObject *arglist, *result;

  if(_py_diff_end == NULL) {
    logprintf("Callback not set for on_diff_end");
    return;
  }
 
  arglist = Py_BuildValue("()");
  result = PyEval_CallObject(_py_diff_end, arglist);
  Py_DECREF(arglist);
  if(result == NULL)
    PyErr_Print();
  else if(PyUnicode_Check(result))
    logprintf ("python: %s\n", PyBytes_AsString(PyUnicode_AsASCIIString(result)));

  Py_XDECREF(result);
}

void py_our_id (int id) {
  if (!python_loaded) { return; }

  PyObject *arglist, *result;

  if(_py_our_id == NULL) {
    logprintf("Callback not set for on_our_id");
    return;
  }

  arglist = Py_BuildValue("(i)", id);
  result = PyEval_CallObject(_py_our_id, arglist);
  Py_DECREF(arglist);
  if(result == NULL)
    PyErr_Print();
  else if(PyUnicode_Check(result))
    logprintf ("python: %s\n", PyBytes_AsString(PyUnicode_AsASCIIString(result)));

  Py_XDECREF(result);
}

void py_new_msg (struct tgl_message *M) {
  if (!python_loaded) { return; }
  PyObject *msg;
  PyObject *arglist, *result;

  if(_py_new_msg == NULL) {
    logprintf("Callback not set for on_new_msg");
    return;
  }

  msg = get_message (M);

  arglist = Py_BuildValue("(O)", msg);
  result = PyEval_CallObject(_py_new_msg, arglist);
  Py_DECREF(arglist);

  if(result == NULL)  
    PyErr_Print();
  else if(PyUnicode_Check(result))
    logprintf ("python: %s\n", PyBytes_AsString(PyUnicode_AsASCIIString(result)));

  Py_XDECREF(result);
}

void py_secret_chat_update (struct tgl_secret_chat *C, unsigned flags) {
  if (!python_loaded) { return; }
  PyObject *peer, *types;
  PyObject *arglist, *result; 

  if(_py_secret_chat_update == NULL) {
    logprintf("Callback not set for on_secret_chat_update");
    return;
  }

  peer = get_peer (C->id, (void *)C);
  types = get_update_types (flags);

  arglist = Py_BuildValue("(OO)", peer, types);
  result = PyEval_CallObject(_py_secret_chat_update, arglist);
  Py_DECREF(arglist);

  if(result == NULL)
    PyErr_Print();
  else if(PyUnicode_Check(result))
    logprintf ("python: %s\n", PyBytes_AsString(PyUnicode_AsASCIIString(result)));

  Py_XDECREF(result);
}


void py_user_update (struct tgl_user *U, unsigned flags) {
  if (!python_loaded) { return; }
  PyObject *peer, *types;
  PyObject *arglist, *result;

  if(_py_user_update == NULL) {
    logprintf("Callback not set for on_user_update");
    return;
  }

  peer = get_peer (U->id, (void *)U);
  types = get_update_types (flags);

  arglist = Py_BuildValue("(OO)", peer, types);
  result = PyEval_CallObject(_py_user_update, arglist);
  Py_DECREF(arglist);

  if(result == NULL)
    PyErr_Print();
  else if(PyUnicode_Check(result))
    logprintf ("python: %s\n", PyBytes_AsString(PyUnicode_AsASCIIString(result)));

  Py_XDECREF(result);
}

void py_chat_update (struct tgl_chat *C, unsigned flags) {
  if (!python_loaded) { return; }

  PyObject *peer, *types;
  PyObject *arglist, *result;

  if(_py_chat_update == NULL) {
    logprintf("Callback not set for on_chat_update");
    return;
  }

  peer = get_peer (C->id, (void *)C);
  types = get_update_types (flags);

  arglist = Py_BuildValue("(OO)", peer, types);
  result = PyEval_CallObject(_py_chat_update, arglist);
  Py_DECREF(arglist);

  if(result == NULL)
    PyErr_Print();
  else if(PyUnicode_Check(result))
    logprintf ("python: %s\n", PyBytes_AsString(PyUnicode_AsASCIIString(result)));

  Py_XDECREF(result);
}

void py_on_loop () {
  if (!python_loaded) { return; }

  PyObject *result;

  if(_py_on_loop == NULL) {
    logprintf("Callback not set for on_chat_update");
    return;
  }

  result = PyEval_CallObject(_py_on_loop, Py_BuildValue("()"));

  if(result == NULL)
    PyErr_Print();
  else if(PyUnicode_Check(result))
    logprintf ("python: %s\n", PyBytes_AsString(PyUnicode_AsASCIIString(result)));

  Py_XDECREF(result);
}


////extern tgl_peer_t *Peers[];
////extern int peer_num;
//
#define MAX_PY_COMMANDS 1000
void *py_ptr[MAX_PY_COMMANDS];
static int pos;
//
//static inline tgl_peer_t *get_peer (const char *s) { 
//  return tgl_peer_get_by_name (TLS, s);
//}
  
enum py_query_type {
  pq_contact_list,
  pq_dialog_list,
  pq_msg,
  pq_send_typing,
  pq_send_typing_abort,
  pq_rename_chat,
  pq_send_photo,
  pq_chat_set_photo,
  pq_set_profile_photo,
  pq_set_profile_name,
  pq_send_video,
  pq_send_text,
  pq_fwd,
  pq_fwd_media,
  pq_load_photo,
  pq_load_video_thumb,
  pq_load_video,
  pq_chat_info,
  pq_user_info,
  pq_history,
  pq_chat_add_user,
  pq_chat_del_user,
  pq_add_contact,
  pq_del_contact,
  pq_rename_contact,
  pq_search,
  pq_global_search,
  pq_mark_read,
  pq_create_secret_chat,
  pq_create_group_chat,
  pq_send_audio,
  pq_send_document,
  pq_send_file,
  pq_load_audio,
  pq_load_document,
  pq_load_document_thumb,
  pq_delete_msg,
  pq_restore_msg,
  pq_accept_secret_chat,
  pq_send_contact,
  pq_status_online,
  pq_status_offline,
  pq_send_location,
  pq_extf,
  pq_import_chat_link
};

void py_empty_cb (struct tgl_state *TLSR, void *cb_extra, int success) {
  assert (TLSR == TLS);
  PyObject *callable = cb_extra;
  PyObject *arglist = NULL;
  PyObject *result = NULL;

  if(PyCallable_Check(callable)) {
    arglist = Py_BuildValue("(O)", success ? Py_True : Py_False);
    result = PyEval_CallObject(callable, arglist);
    Py_DECREF(arglist);
    
    if(result == NULL)
      PyErr_Print();
    
    Py_XDECREF(result);
  }
  
  Py_XDECREF(callable);
}

void py_contact_list_cb (struct tgl_state *TLSR, void *cb_extra, int success, int num, struct tgl_user **UL) {
  assert (TLSR == TLS);
  PyObject *callable = cb_extra;
  PyObject *arglist = NULL;
  PyObject *peers = NULL; 
  PyObject *result = NULL;
   
  if(PyCallable_Check(callable)) {
    peers = PyList_New(0);
    if (success) {
      int i;
      for (i = 0; i < num; i++) {
        PyList_Append(peers, get_peer (UL[i]->id, (void *)UL[i]));
      }
    }

    arglist = Py_BuildValue("(OO)", success ? Py_True : Py_False, peers);
    result = PyEval_CallObject(callable, arglist);
    Py_DECREF(arglist);
    
    if(result == NULL)
      PyErr_Print();
    
    Py_XDECREF(result);
  }

  Py_XDECREF(callable);
}

void py_dialog_list_cb (struct tgl_state *TLSR, void *cb_extra, int success, int num, tgl_peer_id_t peers[], int msgs[], int unread[]) {
  assert (TLSR == TLS);
  PyObject *callable = cb_extra;
  PyObject *arglist = NULL;
  PyObject *dialog_list = NULL; 
  PyObject *dialog = NULL;
  PyObject *result = NULL;
   
  if(PyCallable_Check(callable)) {
    dialog_list = PyList_New(0);
    if (success) {
      int i;
      for (i = 0; i < num; i++) {
        dialog = PyDict_New();
        PyDict_SetItemString(dialog, "peer", get_peer(peers[i], tgl_peer_get (TLS, peers[i])));
                
        struct tgl_message *M = tgl_message_get (TLS, msgs[i]);
        if (M && (M->flags & TGLMF_CREATED)) {
          PyDict_SetItemString(dialog, "message", get_message(M));
        }
        PyDict_SetItemString(dialog, "unread", unread[i] ? Py_True : Py_False);

        PyList_Append(dialog_list, dialog);
      }
    }

    arglist = Py_BuildValue("(OO)", success ? Py_True : Py_False, dialog_list);
    result = PyEval_CallObject(callable, arglist);
    Py_DECREF(arglist);
    
    if(result == NULL)
      PyErr_Print();
    
    Py_XDECREF(result);
  }

  Py_XDECREF(callable);
}

void py_msg_cb (struct tgl_state *TLSR, void *cb_extra, int success, struct tgl_message *M) {
  assert (TLSR == TLS);
  PyObject *callable = cb_extra;
  PyObject *arglist = NULL;
  PyObject *msg = NULL; 
  PyObject *result = NULL;
   
  if(PyCallable_Check(callable)) {
    if (success && M && (M->flags & TGLMF_CREATED)) {
      msg = get_message(M);
    } else {
      Py_INCREF(Py_None);
      msg = Py_None;
    }

    arglist = Py_BuildValue("(OO)", success ? Py_True : Py_False, msg);
    result = PyEval_CallObject(callable, arglist);
    Py_DECREF(arglist);
    
    if(result == NULL)
      PyErr_Print();
    
    Py_XDECREF(result);
  }

  Py_XDECREF(callable);
}

void py_msg_list_cb (struct tgl_state *TLSR, void *cb_extra, int success, int num, struct tgl_message *M[]) {
  assert (TLSR == TLS);
  PyObject *callable = cb_extra;
  PyObject *arglist = NULL;
  PyObject *msgs = NULL; 
  PyObject *result = NULL;
   
  if(PyCallable_Check(callable)) {
    msgs = PyList_New(0);
    if (success) {
      int i;
      for (i = 0; i < num; i++) {
        PyList_Append(msgs, get_message (M[i]));
      }
    }

    arglist = Py_BuildValue("(OO)", success ? Py_True : Py_False, msgs);
    result = PyEval_CallObject(callable, arglist);
    Py_DECREF(arglist);
    
    if(result == NULL)
      PyErr_Print();
    
    Py_XDECREF(result);
  }

  Py_XDECREF(callable);
}

void py_file_cb (struct tgl_state *TLSR, void *cb_extra, int success, const char *file_name) {
  assert (TLSR == TLS);
  PyObject *callable = cb_extra;
  PyObject *arglist = NULL;
  PyObject *filename = NULL; 
  PyObject *result = NULL;
   
  if(PyCallable_Check(callable)) {
    if(success)
      filename = PyUnicode_FromString(file_name);
    else {
      Py_INCREF(Py_None);
      filename = Py_None;
    }

    arglist = Py_BuildValue("(OO)", success ? Py_True : Py_False, filename);
    result = PyEval_CallObject(callable, arglist);
    Py_DECREF(arglist);
    
    if(result == NULL)
      PyErr_Print();
    
    Py_XDECREF(result);
  }

  Py_XDECREF(callable);
}

void py_chat_cb (struct tgl_state *TLSR, void *cb_extra, int success, struct tgl_chat *C) {
  assert (TLSR == TLS);
  PyObject *callable = cb_extra;
  PyObject *arglist = NULL;
  PyObject *peer = NULL; 
  PyObject *result = NULL;
   
  if(PyCallable_Check(callable)) {
    if (success) {
      peer = get_peer(C->id, (void *)C);
    } else {
      Py_INCREF(Py_None);
      peer = Py_None;
    }

    arglist = Py_BuildValue("(OO)", success ? Py_True : Py_False, peer);
    result = PyEval_CallObject(callable, arglist);
    Py_DECREF(arglist);
    
    if(result == NULL)
      PyErr_Print();
    
    Py_XDECREF(result);
  }

  Py_XDECREF(callable);
}

void py_secret_chat_cb (struct tgl_state *TLSR, void *cb_extra, int success, struct tgl_secret_chat *C) {
  assert (TLSR == TLS);
  PyObject *callable = cb_extra;
  PyObject *arglist = NULL;
  PyObject *peer = NULL; 
  PyObject *result = NULL;
   
  if(PyCallable_Check(callable)) {
    if (success) {
      peer = get_peer(C->id, (void *)C);
    } else {
      Py_INCREF(Py_None);
      peer = Py_None;
    }

    arglist = Py_BuildValue("(OO)", success ? Py_True : Py_False, peer);
    result = PyEval_CallObject(callable, arglist);
    Py_DECREF(arglist);
    
    if(result == NULL)
      PyErr_Print();
    
    Py_XDECREF(result);
  }

  Py_XDECREF(callable);
}

void py_user_cb (struct tgl_state *TLSR, void *cb_extra, int success, struct tgl_user *C) {
  assert (TLSR == TLS);
  PyObject *callable = cb_extra;
  PyObject *arglist = NULL;
  PyObject *peer = NULL; 
  PyObject *result = NULL;
   
  if(PyCallable_Check(callable)) {
    if (success) {
      peer = get_peer(C->id, (void *)C);
    } else {
      Py_INCREF(Py_None);
      peer = Py_None;
    }

    arglist = Py_BuildValue("(OO)", success ? Py_True : Py_False, peer);
    result = PyEval_CallObject(callable, arglist);
    Py_DECREF(arglist);
    
    if(result == NULL)
      PyErr_Print();
    
    Py_XDECREF(result);
  }

  Py_XDECREF(callable);
}

void py_str_cb (struct tgl_state *TLSR, void *cb_extra, int success, const char *data) {
  assert (TLSR == TLS);
  PyObject *callable = cb_extra;
  PyObject *arglist = NULL;
  PyObject *str = NULL; 
  PyObject *result = NULL;
   
  if(PyCallable_Check(callable)) {
    if(success)
      str = PyUnicode_FromString(data);
    else {
      Py_INCREF(Py_None);
      str = Py_None;
    }

    arglist = Py_BuildValue("(OO)", success ? Py_True : Py_False, str);
    result = PyEval_CallObject(callable, arglist);
    Py_DECREF(arglist);
    
    if(result == NULL)
      PyErr_Print();
    
    Py_XDECREF(result);
  }

  Py_XDECREF(callable);
}

#define PY_PEER_ID(x) (tgl_peer_id_t)((tgl_Peer*)x)->peer->id

void py_do_all (void) {
  int p = 0;

  // ping the python thread that we're doing the loop
  py_on_loop();

  while (p < pos) {
    assert (p + 2 <= pos);

    enum py_query_type f = (long)py_ptr[p ++];
    PyObject *args = (PyObject *)py_ptr[p ++];

    const char *str, *str1, *str2, *str3;

    int preview = 0;
    int reply_id = 0;
    unsigned long long flags = 0;

    Py_ssize_t i;
    tgl_user_id_t *ids;

    struct tgl_message *M;

    int len, len1, len2, len3;
    int limit, offset;
    long msg_id = 0;

    PyObject *pyObj1 = NULL;
    PyObject *pyObj2 = NULL;
    PyObject *cb_extra = NULL;

    PyObject *msg = NULL;
    PyObject *peer = NULL;
    PyObject *peer1 = NULL;

    switch (f) {
    case pq_contact_list:
      if(PyArg_ParseTuple(args, "|O", &cb_extra))
        tgl_do_update_contact_list (TLS, py_contact_list_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_dialog_list:
      if(PyArg_ParseTuple(args, "|O", &cb_extra))
        tgl_do_get_dialog_list (TLS, 100, 0, py_dialog_list_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_msg:
      if(PyArg_ParseTuple(args, "O!s#|OO", &tgl_PeerType, &peer, &str, &len, &cb_extra, &pyObj1)) {
        if(pyObj1 && PyArg_ParseTuple(pyObj1, "ii", &preview, &reply_id)) {
          if(preview != -1) {
            if(preview)
              flags |= TGL_SEND_MSG_FLAG_ENABLE_PREVIEW;
            else
              flags |= TGL_SEND_MSG_FLAG_DISABLE_PREVIEW;
          }
          flags |= TGL_SEND_MSG_FLAG_REPLY (reply_id);
        }
        tgl_do_send_message (TLS, PY_PEER_ID(peer), str, len, flags, NULL, py_msg_cb, cb_extra);
      } else
        PyErr_Print();

      Py_XDECREF(pyObj1);
      break;
    case pq_send_typing:
      if(PyArg_ParseTuple(args, "O!|O", &tgl_PeerType, &peer, &cb_extra))
        tgl_do_send_typing (TLS, PY_PEER_ID(peer), tgl_typing_typing, py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_send_typing_abort:
      if(PyArg_ParseTuple(args, "O!|O", &tgl_PeerType, &peer, &cb_extra))
        tgl_do_send_typing (TLS, PY_PEER_ID(peer), tgl_typing_cancel, py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_rename_chat:
      if(PyArg_ParseTuple(args, "O!s#|O", &tgl_PeerType, &peer, &str, &len, &cb_extra))
        tgl_do_rename_chat (TLS, PY_PEER_ID(peer), str, len, py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_send_photo:
      if(PyArg_ParseTuple(args, "O!s|O", &tgl_PeerType, &peer, &str, &cb_extra))
        tgl_do_send_document (TLS, PY_PEER_ID(peer), str, NULL, 0, TGL_SEND_MSG_FLAG_DOCUMENT_PHOTO, py_msg_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_send_video:
      if(PyArg_ParseTuple(args, "O!s|O", &tgl_PeerType, &peer, &str, &cb_extra))
        tgl_do_send_document (TLS, PY_PEER_ID(peer), str, NULL, 0, TGL_SEND_MSG_FLAG_DOCUMENT_VIDEO, py_msg_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_send_audio:
      if(PyArg_ParseTuple(args, "O!s|O", &tgl_PeerType, &peer, &str, &cb_extra))
        tgl_do_send_document (TLS, PY_PEER_ID(peer), str, NULL, 0, TGL_SEND_MSG_FLAG_DOCUMENT_AUDIO, py_msg_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_send_document:
      if(PyArg_ParseTuple(args, "O!s|O", &tgl_PeerType, &peer, &str, &cb_extra))
        tgl_do_send_document (TLS, PY_PEER_ID(peer), str, NULL, 0, 0, py_msg_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_send_file:
      if(PyArg_ParseTuple(args, "O!s|O", &tgl_PeerType, &peer, &str, &cb_extra))
        tgl_do_send_document (TLS, PY_PEER_ID(peer), str, NULL, 0, TGL_SEND_MSG_FLAG_DOCUMENT_AUTO, py_msg_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_send_text:
      if(PyArg_ParseTuple(args, "O!s|O", &tgl_PeerType, &peer, &str, &cb_extra))
        tgl_do_send_text (TLS, PY_PEER_ID(peer), str, 0, py_msg_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_chat_set_photo:
      if(PyArg_ParseTuple(args, "O!s|O", &tgl_PeerType, &peer, &str, &cb_extra))
        tgl_do_set_chat_photo (TLS, PY_PEER_ID(peer), str, py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_load_photo:
    case pq_load_video:
    case pq_load_audio:
    case pq_load_document:
      if(PyArg_ParseTuple(args, "O!O", &tgl_MsgType, &msg, &cb_extra))
      {
        M = ((tgl_Msg*)msg)->msg;
        if (!M || (M->media.type != tgl_message_media_photo && M->media.type != tgl_message_media_document && M->media.type != tgl_message_media_document_encr)) {
          py_file_cb (TLS, cb_extra, 0, 0);
        } else {
          if (M->media.type == tgl_message_media_photo) {
            assert (M->media.photo);
            tgl_do_load_photo (TLS, M->media.photo, py_file_cb, cb_extra);
          } else if (M->media.type == tgl_message_media_document) {
            tgl_do_load_document (TLS, M->media.document, py_file_cb, cb_extra);
          } else {
            tgl_do_load_encr_document (TLS, M->media.encr_document, py_file_cb, cb_extra);
          }
        }
      }
      break;
    case pq_load_video_thumb:
    case pq_load_document_thumb:
      if(PyArg_ParseTuple(args, "O!O", &tgl_MsgType, &msg, &cb_extra))
      {
        M = ((tgl_Msg*)msg)->msg;
        if (!M || (M->media.type != tgl_message_media_document)) {
          py_file_cb (TLS, cb_extra, 0, 0);
        } else {
          tgl_do_load_document_thumb (TLS, M->media.document, py_file_cb, cb_extra);
        }
      }
      break;

    case pq_fwd:
      if(PyArg_ParseTuple(args, "O!l|O", &tgl_PeerType, &peer, &msg_id, &cb_extra))
        tgl_do_forward_message (TLS, PY_PEER_ID(peer), msg_id, 0, py_msg_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_fwd_media:
      if(PyArg_ParseTuple(args, "O!l|O", &tgl_PeerType, &peer, &msg_id, &cb_extra))
        tgl_do_forward_media (TLS, PY_PEER_ID(peer), msg_id, 0, py_msg_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_chat_info:
      if(PyArg_ParseTuple(args, "O!|O", &tgl_PeerType, &peer, &cb_extra))
        tgl_do_get_chat_info (TLS, PY_PEER_ID(peer), 0, py_chat_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_user_info:
      if(PyArg_ParseTuple(args, "O!|O", &tgl_PeerType, &peer, &cb_extra))
        tgl_do_get_user_info (TLS, PY_PEER_ID(peer), 0, py_user_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_history:
      if(PyArg_ParseTuple(args, "O!ii|O", &tgl_PeerType, &peer, &offset, &limit, &cb_extra))
        tgl_do_get_history (TLS, PY_PEER_ID(peer), offset, limit, 0, py_msg_list_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_chat_add_user:
      if(PyArg_ParseTuple(args, "O!O!|O", &tgl_PeerType, &peer, &tgl_PeerType, &peer1, &cb_extra))
        tgl_do_add_user_to_chat (TLS, PY_PEER_ID(peer), PY_PEER_ID(peer1), 100, py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_chat_del_user:
      if(PyArg_ParseTuple(args, "O!O!|O", &tgl_PeerType, &peer, &tgl_PeerType, &peer1, &cb_extra))
        tgl_do_del_user_from_chat (TLS, PY_PEER_ID(peer), PY_PEER_ID(peer1), py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_add_contact:
      if(PyArg_ParseTuple(args, "s#s#s#|O", &str1, &len1, &str2, &len2, &str3, &len3, &cb_extra))
        tgl_do_add_contact (TLS, str1, len1, str2, len2, str3, len3, 0, py_contact_list_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_del_contact:
      if(PyArg_ParseTuple(args, "O!|O", &tgl_PeerType, &peer, &cb_extra))
        tgl_do_del_contact (TLS, PY_PEER_ID(peer), py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_rename_contact:
      if(PyArg_ParseTuple(args, "s#s#s#|O", &str1, &len1, &str2, &len2, &str3, &len3, &cb_extra))
        tgl_do_add_contact (TLS, str1, len1, str2, len2, str3, len3, 1, py_contact_list_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_search:
      if(PyArg_ParseTuple(args, "O!s#|O", &tgl_PeerType, &peer, &str, &len, &cb_extra))
        tgl_do_msg_search (TLS, PY_PEER_ID(peer), 0, 0, 40, 0, str, len, py_msg_list_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_global_search:
      if(PyArg_ParseTuple(args, "s#|O", &str, &len, &cb_extra))
        tgl_do_msg_search (TLS, tgl_set_peer_id (TGL_PEER_UNKNOWN, 0), 0, 0, 40, 0, str, len, py_msg_list_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_mark_read:
      if(PyArg_ParseTuple(args, "O!|O", &tgl_PeerType, &peer, &cb_extra))
        tgl_do_mark_read (TLS, PY_PEER_ID(peer), py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_set_profile_photo:
      if(PyArg_ParseTuple(args, "s|O", &str, &cb_extra))
        tgl_do_set_profile_photo (TLS, str, py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_set_profile_name:
      if(PyArg_ParseTuple(args, "s#s#|O", &str1, &len1, &str2, &len2, &cb_extra))
        tgl_do_set_profile_name (TLS, str1, len1, str2, len2, py_user_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_create_secret_chat:
      if(PyArg_ParseTuple(args, "O!|O", &tgl_PeerType, &peer, &cb_extra))
        tgl_do_create_secret_chat (TLS, PY_PEER_ID(peer), py_secret_chat_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_create_group_chat:
      if(PyArg_ParseTuple(args, "O!s#|O", &PyList_Type, &pyObj1, &str, &len, &cb_extra)) {
        if(PyList_GET_SIZE(pyObj1) > 2) {
          ids = (tgl_user_id_t *)malloc(PyList_GET_SIZE(pyObj1) * sizeof(tgl_user_id_t));
          for(i = 0; i < PyList_GET_SIZE(pyObj1); i++) {
            peer = PyList_GetItem(pyObj1, i);
            *(ids+i) = PY_PEER_ID(peer);
          }
          tgl_do_create_group_chat (TLS, PyList_GET_SIZE(pyObj1), ids, str, len, py_empty_cb, cb_extra);

          tfree(ids, PyList_GET_SIZE(pyObj1) * sizeof(tgl_user_id_t));
        } else {
            logprintf("create_group_chat: Argument 1 must be a list of at least 3 peers");
        }
      }
      Py_XDECREF(pyObj1);
      break;
    case pq_delete_msg:
    case pq_restore_msg:
      if(PyArg_ParseTuple(args, "l|O", &msg_id, &cb_extra))
        tgl_do_delete_msg (TLS, msg_id, py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
/*
    case pq_accept_secret_chat:
      tgl_do_accept_encr_chat_request (TLS, py_ptr[p + 1], py_secret_chat_cb, py_ptr[p]);
      break;
*/
    case pq_send_contact:
      if(PyArg_ParseTuple(args, "O!s#s#s#|O",  &tgl_PeerType, &peer, &str1, &len1, &str2, &len2, 
                                               &str3, &len3, &cb_extra))
        tgl_do_send_contact (TLS, PY_PEER_ID(peer), str1, len1, str2, len2, str3, len3, 0, py_msg_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_status_online:
      if(PyArg_ParseTuple(args, "|O", &cb_extra))
        tgl_do_update_status (TLS, 1, py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_status_offline:
      if(PyArg_ParseTuple(args, "|O", &cb_extra))
        tgl_do_update_status (TLS, 0, py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_extf:
      if(PyArg_ParseTuple(args, "s#|O", &str, &len, &cb_extra))
        tgl_do_send_extf (TLS, str, len, py_str_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_import_chat_link:
      if(PyArg_ParseTuple(args, "s#|O", &str, &len, &cb_extra))
        tgl_do_import_chat_link  (TLS, str, len, py_empty_cb, cb_extra);
      else
        PyErr_Print();
      break;
    case pq_send_location:
      if(PyArg_ParseTuple(args, "O!O!O!|O", &tgl_PeerType, &peer, &PyFloat_Type, &pyObj1, &PyFloat_Type, &pyObj2, &cb_extra)){
        tgl_do_send_location (TLS, PY_PEER_ID(peer), 
                              PyFloat_AsDouble(pyObj1), PyFloat_AsDouble(pyObj2), 0, py_msg_cb, cb_extra);
        Py_XDECREF(pyObj1);
        Py_XDECREF(pyObj2);
      } else
        PyErr_Print();
      break;
    default:
      assert (0);
    }

    // Increment reference on cb_extra as it is passed on to the callback to use
    Py_XINCREF(cb_extra);
    
    // Clean up any arg variables we could have used.
    //Py_XDECREF(args); // TODO: this is going negative ref and causing segfaults
    Py_XDECREF(peer);
    Py_XDECREF(peer1);

  }
  pos = 0;
}

PyObject* push_py_func(enum py_query_type type, PyObject *args) {
  assert(pos + 2 < MAX_PY_COMMANDS);

  py_ptr[pos ++] = (void *)(long)type;
  py_ptr[pos ++] = (void *)args;

  Py_INCREF(args);
  Py_RETURN_TRUE;
}

// Register functions to push commands on the queue
PyObject* py_contact_list(PyObject *self, PyObject *args) { return push_py_func(pq_contact_list, args); }
PyObject* py_dialog_list(PyObject *self, PyObject *args) { return push_py_func(pq_dialog_list, args); }
PyObject* py_rename_chat(PyObject *self, PyObject *args) { return push_py_func(pq_rename_chat, args); }
PyObject* py_send_msg(PyObject *self, PyObject *args) { return push_py_func(pq_msg, args); }
PyObject* py_send_typing(PyObject *self, PyObject *args) { return push_py_func(pq_send_typing, args); }
PyObject* py_send_typing_abort(PyObject *self, PyObject *args) { return push_py_func(pq_send_typing_abort, args); }
PyObject* py_send_photo(PyObject *self, PyObject *args) { return push_py_func(pq_send_photo, args); }
PyObject* py_send_video(PyObject *self, PyObject *args) { return push_py_func(pq_send_video, args); }
PyObject* py_send_audio(PyObject *self, PyObject *args) { return push_py_func(pq_send_audio, args); }
PyObject* py_send_document(PyObject *self, PyObject *args) { return push_py_func(pq_send_document, args); }
PyObject* py_send_file(PyObject *self, PyObject *args) { return push_py_func(pq_send_file, args); }
PyObject* py_send_text(PyObject *self, PyObject *args) { return push_py_func(pq_send_text, args); }
PyObject* py_chat_set_photo(PyObject *self, PyObject *args) { return push_py_func(pq_chat_set_photo, args); }
PyObject* py_load_photo(PyObject *self, PyObject *args) { return push_py_func(pq_load_photo, args); }
PyObject* py_load_video(PyObject *self, PyObject *args) { return push_py_func(pq_load_video, args); }
PyObject* py_load_video_thumb(PyObject *self, PyObject *args) { return push_py_func(pq_load_video_thumb, args); }
PyObject* py_load_audio(PyObject *self, PyObject *args) { return push_py_func(pq_load_audio, args); }
PyObject* py_load_document(PyObject *self, PyObject *args) { return push_py_func(pq_load_document, args); }
PyObject* py_load_document_thumb(PyObject *self, PyObject *args) { return push_py_func(pq_load_document_thumb, args); }
PyObject* py_fwd(PyObject *self, PyObject *args) { return push_py_func(pq_fwd, args); }
PyObject* py_fwd_media(PyObject *self, PyObject *args) { return push_py_func(pq_fwd_media, args); }
PyObject* py_chat_info(PyObject *self, PyObject *args) { return push_py_func(pq_chat_info, args); }
PyObject* py_user_info(PyObject *self, PyObject *args) { return push_py_func(pq_chat_info, args); }
PyObject* py_history(PyObject *self, PyObject *args) { return push_py_func(pq_history, args); }
PyObject* py_chat_add_user(PyObject *self, PyObject *args) { return push_py_func(pq_chat_add_user, args); }
PyObject* py_chat_del_user(PyObject *self, PyObject *args) { return push_py_func(pq_chat_del_user, args); }
PyObject* py_add_contact(PyObject *self, PyObject *args) { return push_py_func(pq_add_contact, args); }
PyObject* py_del_contact(PyObject *self, PyObject *args) { return push_py_func(pq_del_contact, args); }
PyObject* py_rename_contact(PyObject *self, PyObject *args) { return push_py_func(pq_rename_contact, args); }
PyObject* py_search(PyObject *self, PyObject *args) { return push_py_func(pq_search, args); }
PyObject* py_global_search(PyObject *self, PyObject *args) { return push_py_func(pq_global_search, args); }
PyObject* py_mark_read(PyObject *self, PyObject *args) { return push_py_func(pq_mark_read, args); }
PyObject* py_set_profile_photo(PyObject *self, PyObject *args) { return push_py_func(pq_set_profile_photo, args); }
PyObject* py_set_profile_name(PyObject *self, PyObject *args) { return push_py_func(pq_set_profile_name, args); }
PyObject* py_create_secret_chat(PyObject *self, PyObject *args) { return push_py_func(pq_create_secret_chat, args); }
PyObject* py_create_group_chat(PyObject *self, PyObject *args) { return push_py_func(pq_create_group_chat, args); }
PyObject* py_delete_msg(PyObject *self, PyObject *args) { return push_py_func(pq_delete_msg, args); }
PyObject* py_restore_msg(PyObject *self, PyObject *args) { return push_py_func(pq_restore_msg, args); }
PyObject* py_accept_secret_chat(PyObject *self, PyObject *args) { return push_py_func(pq_accept_secret_chat, args); }
PyObject* py_send_contact(PyObject *self, PyObject *args) { return push_py_func(pq_send_contact, args); }
PyObject* py_status_online(PyObject *self, PyObject *args) { return push_py_func(pq_status_online, args); }
PyObject* py_status_offline(PyObject *self, PyObject *args) { return push_py_func(pq_status_offline, args); }
PyObject* py_send_location(PyObject *self, PyObject *args) { return push_py_func(pq_send_location, args); }
PyObject* py_extf(PyObject *self, PyObject *args) { return push_py_func(pq_extf, args); }
PyObject* py_import_chat_link(PyObject *self, PyObject *args) { return push_py_func(pq_import_chat_link, args); }

extern int safe_quit;
extern int exit_code;
PyObject* py_safe_quit(PyObject *self, PyObject *args)
{
  int exit_val = 0;
  if(PyArg_ParseTuple(args, "|i", &exit_val)) {
    safe_quit = 1;
    exit_code = exit_val;
  } else {
    PyErr_Print();
  }

  Py_RETURN_NONE;
}

PyObject* py_set_preview(PyObject *self, PyObject *args)
{
  int preview = 0;
  if(PyArg_ParseTuple(args, "p", &preview)) {
    if(preview)
      TLS->disable_link_preview = 0;
    else
      TLS->disable_link_preview = 1;
  } else {
    PyErr_Print();
  }

  Py_RETURN_NONE;
}

// Store callables for python functions
TGL_PYTHON_CALLBACK("on_binlog_replay_end", _py_binlog_end);
TGL_PYTHON_CALLBACK("on_get_difference_end", _py_diff_end);
TGL_PYTHON_CALLBACK("on_our_id", _py_our_id);
TGL_PYTHON_CALLBACK("on_msg_receive", _py_new_msg);
TGL_PYTHON_CALLBACK("on_secret_chat_update", _py_secret_chat_update);
TGL_PYTHON_CALLBACK("on_user_update", _py_user_update);
TGL_PYTHON_CALLBACK("on_chat_update", _py_chat_update);
TGL_PYTHON_CALLBACK("on_loop", _py_on_loop);

static PyMethodDef py_tgl_methods[] = {
  {"get_contact_list", py_contact_list, METH_VARARGS, "retrieve contact list"},
  {"get_dialog_list", py_dialog_list, METH_VARARGS, ""},
  {"rename_chat", py_rename_chat, METH_VARARGS, ""},
  {"send_msg", py_send_msg, METH_VARARGS, "send message to user or chat"},
  {"send_typing", py_send_typing, METH_VARARGS, ""},
  {"send_typing_abort", py_send_typing_abort, METH_VARARGS, ""},
  {"send_photo", py_send_photo, METH_VARARGS, ""},
  {"send_video", py_send_video, METH_VARARGS, ""},
  {"send_audio", py_send_audio, METH_VARARGS, ""},
  {"send_document", py_send_document, METH_VARARGS, ""},
  {"send_file", py_send_file, METH_VARARGS, ""},
  {"send_text", py_send_text, METH_VARARGS, ""},
  {"chat_set_photo", py_chat_set_photo, METH_VARARGS, ""},
  {"load_photo", py_load_photo, METH_VARARGS, ""},
  {"load_video", py_load_video, METH_VARARGS, ""},
  {"load_video_thumb", py_load_video_thumb, METH_VARARGS, ""},
  {"load_audio", py_load_audio, METH_VARARGS, ""},
  {"load_document", py_load_document, METH_VARARGS, ""},
  {"load_document_thumb", py_load_document_thumb, METH_VARARGS, ""},
  {"fwd_msg", py_fwd, METH_VARARGS, ""},
  {"fwd_media", py_fwd_media, METH_VARARGS, ""},
  {"chat_info", py_chat_info, METH_VARARGS, ""},
  {"user_info", py_user_info, METH_VARARGS, ""},
  {"get_history", py_history, METH_VARARGS, ""},
  {"chat_add_user", py_chat_add_user, METH_VARARGS, ""},
  {"chat_del_user", py_chat_del_user, METH_VARARGS, ""},
  {"add_contact", py_add_contact, METH_VARARGS, ""},
  {"del_contact", py_del_contact, METH_VARARGS, ""},
  {"rename_contact", py_rename_contact, METH_VARARGS, ""},
  {"msg_search", py_search, METH_VARARGS, ""},
  {"msg_global_search", py_global_search, METH_VARARGS, ""},
  {"mark_read", py_mark_read, METH_VARARGS, ""},
  {"set_profile_photo", py_set_profile_photo, METH_VARARGS, ""},
  {"set_profile_name", py_set_profile_name, METH_VARARGS, ""},
  {"create_secret_chat", py_create_secret_chat, METH_VARARGS, ""},
  {"create_group_chat", py_create_group_chat, METH_VARARGS, ""},
  {"delete_msg", py_delete_msg, METH_VARARGS, ""},
  {"restore_msg", py_restore_msg, METH_VARARGS, ""},
  {"accept_secret_chat", py_accept_secret_chat, METH_VARARGS, ""},
  {"send_contact", py_send_contact, METH_VARARGS, ""},
  {"status_online", py_status_online, METH_VARARGS, ""},
  {"status_offline", py_status_offline, METH_VARARGS, ""},
  {"send_location", py_send_location, METH_VARARGS, ""},  
  {"ext_function", py_extf, METH_VARARGS, ""},
  {"import_chat_link", py_import_chat_link, METH_VARARGS, ""},
  {"set_on_binlog_replay_end", set_py_binlog_end, METH_VARARGS, ""},
  {"set_on_get_difference_end", set_py_diff_end, METH_VARARGS, ""},
  {"set_on_our_id", set_py_our_id, METH_VARARGS, ""},
  {"set_on_msg_receive", set_py_new_msg, METH_VARARGS, ""},
  {"set_on_secret_chat_update", set_py_secret_chat_update, METH_VARARGS, ""},
  {"set_on_user_update", set_py_user_update, METH_VARARGS, ""},
  {"set_on_chat_update", set_py_chat_update, METH_VARARGS, ""},
  {"set_on_loop", set_py_on_loop, METH_VARARGS, ""},
  {"set_link_preview", py_set_preview, METH_VARARGS, ""},
  {"safe_quit", py_safe_quit, METH_VARARGS, ""},
  {"safe_exit", py_safe_quit, METH_VARARGS, ""}, // Alias to safe_quit for naming consistancy in python.
  { NULL, NULL, 0, NULL }
};

void py_add_action_enums(PyObject *m)
{
  PyModule_AddIntConstant(m, "ACTION_NONE", tgl_message_action_none);
  PyModule_AddIntConstant(m, "ACTION_GEO_CHAT_CREATE", tgl_message_action_geo_chat_create);
  PyModule_AddIntConstant(m, "ACTION_GEO_CHAT_CHECKIN", tgl_message_action_geo_chat_checkin);
  PyModule_AddIntConstant(m, "ACTION_CHAT_CREATE", tgl_message_action_chat_create);
  PyModule_AddIntConstant(m, "ACTION_CHAT_EDIT_TITLE", tgl_message_action_chat_edit_title);
  PyModule_AddIntConstant(m, "ACTION_CHAT_EDIT_PHOTO", tgl_message_action_chat_edit_photo);
  PyModule_AddIntConstant(m, "ACTION_CHAT_DELETE_PHOTO", tgl_message_action_chat_delete_photo);
  PyModule_AddIntConstant(m, "ACTION_CHAT_ADD_USER", tgl_message_action_chat_add_user);
  PyModule_AddIntConstant(m, "ACTION_CHAT_ADD_USER_BY_LINK", tgl_message_action_chat_add_user_by_link);
  PyModule_AddIntConstant(m, "ACTION_CHAT_DELETE_USER", tgl_message_action_chat_delete_user);
  PyModule_AddIntConstant(m, "ACTION_SET_MESSAGE_TTL", tgl_message_action_set_message_ttl);
  PyModule_AddIntConstant(m, "ACTION_READ_MESSAGES", tgl_message_action_read_messages);
  PyModule_AddIntConstant(m, "ACTION_DELETE_MESSAGES", tgl_message_action_delete_messages);
  PyModule_AddIntConstant(m, "ACTION_SCREENSHOT_MESSAGES", tgl_message_action_screenshot_messages);
  PyModule_AddIntConstant(m, "ACTION_FLUSH_HISTORY", tgl_message_action_flush_history);
  PyModule_AddIntConstant(m, "ACTION_RESEND", tgl_message_action_resend);
  PyModule_AddIntConstant(m, "ACTION_NOTIFY_LAYER", tgl_message_action_notify_layer);
  PyModule_AddIntConstant(m, "ACTION_TYPING", tgl_message_action_typing);
  PyModule_AddIntConstant(m, "ACTION_NOOP", tgl_message_action_noop);
  PyModule_AddIntConstant(m, "ACTION_COMMIT_KEY", tgl_message_action_commit_key);
  PyModule_AddIntConstant(m, "ACTION_ABORT_KEY", tgl_message_action_abort_key);
  PyModule_AddIntConstant(m, "ACTION_REQUEST_KEY", tgl_message_action_request_key);
  PyModule_AddIntConstant(m, "ACTION_ACCEPT_KEY", tgl_message_action_accept_key);
}

void py_add_peer_type_enums(PyObject *m)
{
  PyModule_AddIntConstant(m, "PEER_USER", TGL_PEER_USER);
  PyModule_AddIntConstant(m, "PEER_CHAT", TGL_PEER_CHAT);
  PyModule_AddIntConstant(m, "PEER_ENCR_CHAT", TGL_PEER_ENCR_CHAT);
}


MOD_INIT(tgl)
{
  PyObject *m;

  MOD_DEF(m, "tgl", NULL, py_tgl_methods)

  if (m == NULL)
    return MOD_ERROR_VAL;

  py_add_action_enums(m);
  py_add_peer_type_enums(m);

  if (PyType_Ready(&tgl_PeerType) < 0)
    return MOD_ERROR_VAL;

  Py_INCREF(&tgl_PeerType);
  PyModule_AddObject(m, "Peer", (PyObject *)&tgl_PeerType);

  if (PyType_Ready(&tgl_MsgType) < 0)
    return MOD_ERROR_VAL;
  
  Py_INCREF(&tgl_MsgType);
  PyModule_AddObject(m, "Msg", (PyObject *)&tgl_MsgType);

  TglError = PyErr_NewException("tgl.Error", NULL, NULL);
  Py_INCREF(TglError);
  PyModule_AddObject(m, "TglError", TglError);
  
  PeerError = PyErr_NewException("tgl.PeerError", NULL, NULL);
  Py_INCREF(PeerError);
  PyModule_AddObject(m, "PeerError", PeerError);
  
  MsgError = PyErr_NewException("tgl.MsgError", NULL, NULL);
  Py_INCREF(MsgError);
  PyModule_AddObject(m, "MsgError", MsgError);
  
  return MOD_SUCCESS_VAL(m);  
}


void py_init (const char *file) {
  if (!file) { return; }
  python_loaded = 0;
  
  PyObject *pModule;

  // Get a copy of the filename for dirname/basename, which may modify the string, and break const correctness
  char filename[1024];
  strncpy(filename, file, 1024);


   
#if PY_MAJOR_VERSION >= 3
  PyImport_AppendInittab("tgl", &PyInit_tgl);
  Py_Initialize();
#else
  Py_Initialize();
  inittgl();
#endif


  PyObject* sysPath = PySys_GetObject((char*)"path");
  PyList_Append(sysPath, PyUnicode_FromString(dirname(filename)));
  
  // Recopy the string in, since dirname modified it.
  strncpy(filename, file, 1024);
  
  // remove .py extension from file, if any
  char* dot = strrchr(filename, '.');
  if (dot && strcmp(dot, ".py") == 0) 
    *dot = 0;
  pModule = PyImport_Import(PyUnicode_FromString(basename(filename)));
  
  if(pModule == NULL || PyErr_Occurred()) { // Error loading script
    logprintf("Failed to load python script\n");
    PyErr_Print();
    exit(1);
  }


  python_loaded = 1;
  PyDateTime_IMPORT;
  logprintf("Python Initialized\n");
}

#endif
