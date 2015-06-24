
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef USE_PYTHON
#include <Python.h>
#include <tgl/tgl.h>
#include <tgl/tools.h>
#include <tgl/updates.h>
#include <tgl/tgl-structures.h>
#include <stdlib.h>
#include "structmember.h"

// Python Imports
#include "datetime.h"

#include "python-types.h"
#include "python-tg.h"

extern struct tgl_state *TLS;
// TGL Python Exceptions
extern PyObject *TglError;
extern PyObject *PeerError;
extern PyObject *MsgError;

// Forward type declarations
PyTypeObject tgl_PeerType;

// Utility functions
PyObject* get_datetime(long datetime)
{
  return PyDateTime_FromTimestamp(Py_BuildValue("(O)", PyLong_FromLong(datetime)));
}


//
// tgl_peer_t wrapper
//
static void
tgl_Peer_dealloc(tgl_Peer* self)
{
  Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject *
tgl_Peer_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  tgl_Peer *self;

  PyDateTime_IMPORT;
  self = (tgl_Peer *)type->tp_alloc(type, 0);
  return (PyObject *)self;
}

static int
tgl_Peer_init(tgl_Peer *self, PyObject *args, PyObject *kwds)
{
  static char *kwlist[] = {"type", "id", NULL};
  tgl_peer_id_t peer_id;
  if(!PyArg_ParseTupleAndKeywords(args, kwds, "ii", kwlist, 
                                  &peer_id.type,
                                  &peer_id.id))
  {
    PyErr_Format(PeerError, "Peer must specify type and id");
    return -1;
  }
  self->peer = tgl_peer_get(TLS, peer_id);
  if(self->peer == NULL)
    return -1;

  return 0;
}

static PyObject *
tgl_Peer_getname (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
      ret = PyUnicode_FromString(self->peer->user.print_name);
      break;
    case TGL_PEER_CHAT:
      ret = PyUnicode_FromString(self->peer->chat.print_title);
      break;
    case TGL_PEER_ENCR_CHAT:
      ret = PyUnicode_FromString(self->peer->encr_chat.print_name);
      break;
    default:
     PyErr_SetString(PeerError, "peer.type_name not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}


static PyObject *
tgl_Peer_getuser_id (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
      ret = PyLong_FromLong(self->peer->id.id);
      break;
    case TGL_PEER_CHAT:
      PyErr_SetString(PeerError, "peer.type_name == 'chat' has no user_id");
      Py_RETURN_NONE;
 
      break;
    case TGL_PEER_ENCR_CHAT:
      ret = PyLong_FromLong(self->peer->encr_chat.user_id);
      break;
    default:
     PyErr_SetString(PeerError, "peer.type_name not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_getuser_list (tgl_Peer *self, void *closure)
{
  PyObject *ret;
  int i;
  struct tgl_chat_user *user_list;

  switch(self->peer->id.type) {
    case TGL_PEER_CHAT:
      ret = PyList_New(0);
        for(i = 0; i < self->peer->chat.user_list_size; i++) {
        // TODO: Sort tgl_user objects, maybe offline mode is enoug?
        user_list = self->peer->chat.user_list + i;
        PyList_Append(ret, PyLong_FromLong(user_list->user_id));
      }
      break;
    case TGL_PEER_ENCR_CHAT:
    case TGL_PEER_USER:
      PyErr_SetString(PeerError, "Only peer.type_name == 'chat' has user_list");
      Py_RETURN_NONE;
      break;
    default:
     PyErr_SetString(PeerError, "peer.type_name not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_getuser_status(tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
      ret = PyDict_New();
      PyDict_SetItemString(ret, "online", self->peer->user.status.online? Py_True : Py_False);
      PyDict_SetItemString(ret, "when", get_datetime(self->peer->user.status.when));

      break;
    case TGL_PEER_CHAT:
    case TGL_PEER_ENCR_CHAT:
      PyErr_SetString(PeerError, "Only peer.type_name == 'user' has user_status");
      Py_RETURN_NONE;
      break;
    default:
     PyErr_SetString(PeerError, "peer.type_name not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_getphone (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
      if(self->peer->user.phone)
        ret = PyUnicode_FromString(self->peer->user.phone);
      else
        Py_RETURN_NONE;
      break;
    case TGL_PEER_CHAT:
    case TGL_PEER_ENCR_CHAT:
      PyErr_SetString(PeerError, "Only peer.type_name == 'user' has phone");
      Py_RETURN_NONE;
      break;
    default:
     PyErr_SetString(PeerError, "peer.type_name not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_getusername (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
      if(self->peer->user.username)
        ret = PyUnicode_FromString(self->peer->user.username);
      else
        Py_RETURN_NONE;
      break;
    case TGL_PEER_CHAT:
    case TGL_PEER_ENCR_CHAT:
      PyErr_SetString(PeerError, "Only peer.type_name == 'user' has username");
      Py_RETURN_NONE;
      break;
    default:
     PyErr_SetString(PeerError, "peer.type_name not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_getfirst_name (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
      if(self->peer->user.first_name)
        ret = PyUnicode_FromString(self->peer->user.first_name);
      else
        Py_RETURN_NONE;
      break;
    case TGL_PEER_CHAT:
    case TGL_PEER_ENCR_CHAT:
      PyErr_SetString(PeerError, "Only peer.type_name == 'user' has first_name");
      Py_RETURN_NONE;
      break;
    default:
     PyErr_SetString(PeerError, "peer.type_name not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_getlast_name (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
      if(self->peer->user.last_name)
        ret = PyUnicode_FromString(self->peer->user.last_name);
      else
        Py_RETURN_NONE;
      break;
    case TGL_PEER_CHAT:
    case TGL_PEER_ENCR_CHAT:
      PyErr_SetString(PeerError, "Only peer.type_name == 'user' has last_name");
      Py_RETURN_NONE;
      break;
    default:
     PyErr_SetString(PeerError, "peer.type_name not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_getuser (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer->id.type) {
    case TGL_PEER_ENCR_CHAT:
      ret = tgl_Peer_FromTglPeer(tgl_peer_get(TLS, TGL_MK_USER (self->peer->encr_chat.user_id)));
      break;
    case TGL_PEER_USER:
      ret = (PyObject*)self;
      break;
    case TGL_PEER_CHAT:
      PyErr_SetString(PeerError, "Only peer.type_name == 'chat' does not have user");
      Py_RETURN_NONE;
      break;
    default:
     PyErr_SetString(PeerError, "peer.type_name not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_gettype_name(tgl_Peer* self)
{
  PyObject *name;

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
      name = PyUnicode_FromString("user");
      break;
    case TGL_PEER_CHAT:
      name = PyUnicode_FromString("chat");
      break;
    case TGL_PEER_ENCR_CHAT:
      name = PyUnicode_FromString("secret_chat");
      break;
    default:
      name = PyUnicode_FromString("unknown");
    }
  return name;
}

static PyObject *
tgl_Peer_getid (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  ret = PyLong_FromLong(self->peer->id.id);

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_gettype (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  ret = PyLong_FromLong(self->peer->id.type); 

  Py_XINCREF(ret);
  return ret;
}

static PyGetSetDef tgl_Peer_getseters[] = {
  {"id", (getter)tgl_Peer_getid, NULL, "", NULL},
  {"type", (getter)tgl_Peer_gettype, NULL, "", NULL},
  {"type_name", (getter)tgl_Peer_gettype_name, NULL, "", NULL},
  {"name", (getter)tgl_Peer_getname, NULL, "", NULL},
  {"user_id", (getter)tgl_Peer_getuser_id, NULL, "", NULL},
  {"user", (getter)tgl_Peer_getuser, NULL, "", NULL},
  {"user_list", (getter)tgl_Peer_getuser_list, NULL, "", NULL},
  {"user_status", (getter)tgl_Peer_getuser_status, NULL, "", NULL},
  {"phone", (getter)tgl_Peer_getphone, NULL, "", NULL},
  {"username", (getter)tgl_Peer_getusername, NULL, "", NULL},
  {"first_name", (getter)tgl_Peer_getfirst_name, NULL, "", NULL},
  {"last_name", (getter)tgl_Peer_getlast_name, NULL, "", NULL},
  {NULL}  /* Sentinel */
};

static PyMemberDef tgl_Peer_members[] = {
  {NULL}  /* Sentinel */
};

static PyObject *
tgl_Peer_send_msg (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"message", "callback", "preview", "reply", NULL};

  char *message;
  int preview = -1;
  int reply = 0;
  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "s|Opi", kwlist, &message, &callback, &preview, &reply)) {
    PyObject *api_call;
    PyObject *flags;

    flags = Py_BuildValue("(ii)", preview, reply);


    if(callback)
      api_call = Py_BuildValue("OsOO", (PyObject*) self, message, callback, flags);
    else
      api_call = Py_BuildValue("OsOO", (PyObject*) self, message, Py_None, flags);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);
    Py_XINCREF(flags);

    return py_send_msg(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_fwd_msg (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"callback", NULL};

  int fwd_id = 0;
  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "i|O", kwlist, &fwd_id, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OiO", (PyObject*) self, fwd_id, callback);
    else
      api_call = Py_BuildValue("Oi", (PyObject*) self, fwd_id);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_fwd(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_send_typing (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"callback", NULL};

  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "|O", kwlist, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OO", (PyObject*) self, callback);
    else
      api_call = Py_BuildValue("O", (PyObject*) self);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_send_typing(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_send_typing_abort (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"callback", NULL};

  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "|O", kwlist, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OO", (PyObject*) self, callback);
    else
      api_call = Py_BuildValue("O", (PyObject*) self);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_send_typing_abort(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_rename_chat (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"title", "callback", NULL};

  char * title;
  PyObject *callback = NULL;

  if(self->peer->id.type != TGL_PEER_CHAT) {
    PyErr_SetString(PeerError, "Only a chat peer can be renamed");
    Py_XINCREF(Py_False);
    return Py_False;
  }

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "s|O", kwlist, &title, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OsO", (PyObject*) self, title, callback);
    else
      api_call = Py_BuildValue("Os", (PyObject*) self, title);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_rename_chat(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_send_photo (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"filename", "callback", NULL};

  char *filename;
  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "s|O", kwlist, &filename, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OsO", (PyObject*) self, filename, callback);
    else
      api_call = Py_BuildValue("Os", (PyObject*) self, filename);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_send_photo(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_send_video (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"filename", "callback", NULL};

  char *filename;
  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "s|O", kwlist, &filename, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OsO", (PyObject*) self, filename, callback);
    else
      api_call = Py_BuildValue("Os", (PyObject*) self, filename);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_send_video(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_send_audio (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"filename", "callback", NULL};

  char *filename;
  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "s|O", kwlist, &filename, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OsO", (PyObject*) self, filename, callback);
    else
      api_call = Py_BuildValue("Os", (PyObject*) self, filename);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_send_audio(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_send_document (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"filename", "callback", NULL};

  char *filename;
  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "s|O", kwlist, &filename, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OsO", (PyObject*) self, filename, callback);
    else
      api_call = Py_BuildValue("Os", (PyObject*) self, filename);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_send_document(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_send_file (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"filename", "callback", NULL};

  char *filename;
  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "s|O", kwlist, &filename, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OsO", (PyObject*) self, filename, callback);
    else
      api_call = Py_BuildValue("Os", (PyObject*) self, filename);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_send_file(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_send_text (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"filename", "callback", NULL};

  char *filename;
  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "s|O", kwlist, &filename, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OsO", (PyObject*) self, filename, callback);
    else
      api_call = Py_BuildValue("Os", (PyObject*) self, filename);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_send_text(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_chat_set_photo (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"filename", "callback", NULL};

  char * filename;
  PyObject *callback = NULL;

  if(self->peer->id.type != TGL_PEER_CHAT) {
    PyErr_SetString(PeerError, "Only a chat peer can have a chat photo set.");
    Py_XINCREF(Py_False);
    return Py_False;
  }

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "s|O", kwlist, &filename, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OsO", (PyObject*) self, filename, callback);
    else
      api_call = Py_BuildValue("Os", (PyObject*) self, filename);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_chat_set_photo(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_chat_add_user (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"peer", "callback", NULL};

  PyObject *peer;
  PyObject *callback = NULL;

  if(self->peer->id.type != TGL_PEER_CHAT) {
    PyErr_SetString(PeerError, "Only a chat peer can have a user added.");
    Py_XINCREF(Py_False);
    return Py_False;
  }

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "O!|O", kwlist, &tgl_PeerType, &peer, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OOO", (PyObject*) self, peer, callback);
    else
      api_call = Py_BuildValue("OO", (PyObject*) self, peer);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_chat_add_user(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_chat_del_user (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"peer", "callback", NULL};

  PyObject *peer;
  PyObject *callback = NULL;

  if(self->peer->id.type != TGL_PEER_CHAT) {
    PyErr_SetString(PeerError, "Only a chat peer can have a user deleted.");
    Py_XINCREF(Py_False);
    return Py_False;
  }

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "O!|O", kwlist, &tgl_PeerType, &peer, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OOO", (PyObject*) self, peer, callback);
    else
      api_call = Py_BuildValue("OO", (PyObject*) self, peer);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_chat_del_user(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_history (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"offset", "limit", "callback", NULL};

  int offset, limit;
  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "ii|O", kwlist, &offset, &limit, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OiiO", (PyObject*) self, offset, limit, callback);
    else
      api_call = Py_BuildValue("Oii", (PyObject*) self, offset, limit);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_history(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_info (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"callback", NULL};

  PyObject *callback = NULL;

  if(self->peer->id.type == TGL_PEER_ENCR_CHAT) {
    PyErr_SetString(PeerError, "Secret chats currently have no info.");
    Py_XINCREF(Py_False);
    return Py_False;
  }

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "O", kwlist, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OO", (PyObject*) self, callback);
    else
      api_call = Py_None;

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);
    if(self->peer->id.type == TGL_PEER_USER)
      return py_user_info(Py_None, api_call);
    else
      return py_chat_info(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_send_contact (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"phone", "first_name", "last_name", "callback", NULL};

  char *phone, *first_name, *last_name;
  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "sss|O", kwlist, &phone, &first_name, &last_name, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("Osss", (PyObject*) self, phone, first_name, last_name, callback);
    else
      api_call = Py_BuildValue("Os", (PyObject*) self, phone, first_name, last_name);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_send_contact(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}


static PyObject *
tgl_Peer_send_location (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"latitude", "longitude", "callback", NULL};

  PyObject *latitude, *longitude;
  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "O!O!|O", kwlist, &PyFloat_Type,
        &latitude, &PyFloat_Type, &longitude, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OOOO", (PyObject*) self, latitude, longitude, callback);
    else
      api_call = Py_BuildValue("OOO", (PyObject*) self, latitude, longitude);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_send_location(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Peer_mark_read (tgl_Peer *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"callback", NULL};

  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "|O", kwlist, &callback)) {
    PyObject *api_call;

    if(callback)
      api_call = Py_BuildValue("OO", (PyObject*) self, callback);
    else
      api_call = Py_BuildValue("O", (PyObject*) self);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_mark_read(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyMethodDef tgl_Peer_methods[] = {
  {"send_msg",          (PyCFunction)tgl_Peer_send_msg, METH_VARARGS | METH_KEYWORDS,
    "Send a message to peer object"},
  {"send_typing",       (PyCFunction)tgl_Peer_send_typing, METH_VARARGS | METH_KEYWORDS, ""},
  {"send_typing_abort", (PyCFunction)tgl_Peer_send_typing_abort, METH_VARARGS | METH_KEYWORDS, ""},
  {"rename_chat",       (PyCFunction)tgl_Peer_rename_chat, METH_VARARGS | METH_KEYWORDS, ""},
  {"send_photo",        (PyCFunction)tgl_Peer_send_photo, METH_VARARGS | METH_KEYWORDS, ""},
  {"send_video",        (PyCFunction)tgl_Peer_send_video, METH_VARARGS | METH_KEYWORDS, ""},
  {"send_audio",        (PyCFunction)tgl_Peer_send_audio, METH_VARARGS | METH_KEYWORDS, ""},
  {"send_file",         (PyCFunction)tgl_Peer_send_file, METH_VARARGS | METH_KEYWORDS, ""},
  {"send_document",     (PyCFunction)tgl_Peer_send_document, METH_VARARGS | METH_KEYWORDS, ""},
  {"send_text",         (PyCFunction)tgl_Peer_send_text, METH_VARARGS | METH_KEYWORDS, ""},
  {"chat_set_photo",    (PyCFunction)tgl_Peer_chat_set_photo, METH_VARARGS | METH_KEYWORDS, ""},
  {"info",              (PyCFunction)tgl_Peer_info, METH_VARARGS | METH_KEYWORDS, ""},
  {"history",           (PyCFunction)tgl_Peer_history, METH_VARARGS | METH_KEYWORDS, ""},
  {"chat_add_user",     (PyCFunction)tgl_Peer_chat_add_user, METH_VARARGS | METH_KEYWORDS, ""},
  {"chat_del_user",     (PyCFunction)tgl_Peer_chat_del_user, METH_VARARGS | METH_KEYWORDS, ""},
  {"send_contact",      (PyCFunction)tgl_Peer_send_contact, METH_VARARGS | METH_KEYWORDS, ""},
  {"send_location",     (PyCFunction)tgl_Peer_send_location, METH_VARARGS | METH_KEYWORDS, ""},
  {"mark_read",         (PyCFunction)tgl_Peer_mark_read, METH_VARARGS | METH_KEYWORDS, ""},
  {"fwd_msg",           (PyCFunction)tgl_Peer_fwd_msg, METH_VARARGS | METH_KEYWORDS, ""},
  {NULL}  /* Sentinel */
};


static PyObject *
tgl_Peer_repr(tgl_Peer *self)
{
  PyObject *ret;

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
#if PY_VERSION_HEX < 0x02070900
       ret = PyUnicode_FromFormat("<tgl.Peer: id=%ld>", self->peer->id.id);
#else
       ret = PyUnicode_FromFormat("<tgl.Peer: type=user, id=%ld, username=%R, name=%R, first_name=%R, last_name=%R, phone=%R>",
                                  self->peer->id.id,
                                  PyObject_GetAttrString((PyObject*)self, "username"),
                                  PyObject_GetAttrString((PyObject*)self, "name"),
                                  PyObject_GetAttrString((PyObject*)self, "first_name"),
                                  PyObject_GetAttrString((PyObject*)self, "last_name"),
                                  PyObject_GetAttrString((PyObject*)self, "phone")
            );
#endif
      break;
    case TGL_PEER_CHAT:
      ret = PyUnicode_FromFormat("<tgl.Peer: type=chat, id=%ld, name=%s>",
                                  self->peer->id.id,  self->peer->chat.print_title);
      break;
    case TGL_PEER_ENCR_CHAT:
      ret = PyUnicode_FromFormat("<tgl.Peer: type=secret_chat, id=%ld, name=%s, user=%R>",
                                  self->peer->id.id,  self->peer->encr_chat.print_name,
                                  PyObject_GetAttrString((PyObject*)self, "user"));
      break;
    default:
      ret = PyUnicode_FromFormat("<tgl.Peer: Type Unknown>");
    }

  return ret;
}

int
tgl_Peer_hash(PyObject *self)
{
  return PyObject_Hash(PyObject_GetAttrString(self, "id"));
}

PyObject *
tgl_Peer_RichCompare(PyObject *self, PyObject *other, int cmp)
{
  PyObject *result = NULL;

  if(!PyObject_TypeCheck(other, &tgl_PeerType)) {
    result = Py_False;
  } else {
    if(((tgl_Peer*)self)->peer == NULL ||
       ((tgl_Peer*)other)->peer == NULL) {
      result = Py_False; // If either object is not properly instantiated, compare is false
    } else {
      switch (cmp) {
      case Py_EQ:
        result = ((tgl_Peer*)self)->peer->id.id == ((tgl_Peer*)other)->peer->id.id ? Py_True : Py_False;
        break;
      case Py_NE:
        result = ((tgl_Peer*)self)->peer->id.id == ((tgl_Peer*)other)->peer->id.id ? Py_False : Py_True;
        break;
      case Py_LE:
      case Py_GE:
      case Py_GT:
      case Py_LT:
      default:
        return Py_INCREF(Py_NotImplemented), Py_NotImplemented;
      }
    }
  }
  Py_XINCREF(result);
  return result;
}


PyTypeObject tgl_PeerType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "tgl.Peer",                   /* tp_name */
    sizeof(tgl_Peer),             /* tp_basicsize */
    0,                            /* tp_itemsize */
    (destructor)tgl_Peer_dealloc, /* tp_dealloc */
    0,                            /* tp_print */
    0,                            /* tp_getattr */
    0,                            /* tp_setattr */
    0,                            /* tp_reserved */
    (reprfunc)tgl_Peer_repr,      /* tp_repr */
    0,                            /* tp_as_number */
    0,                            /* tp_as_sequence */
    0,                            /* tp_as_mapping */
    (hashfunc)tgl_Peer_hash,      /* tp_hash  */
    0,                            /* tp_call */
    0,                            /* tp_str */
    0,                            /* tp_getattro */
    0,                            /* tp_setattro */
    0,                            /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,           /* tp_flags */
    "tgl Peer",                   /* tp_doc */
    0,                            /* tp_traverse */
    0,                            /* tp_clear */
    (richcmpfunc)tgl_Peer_RichCompare, /* tp_richcompare */
    0,                            /* tp_weaklistoffset */
    0,                            /* tp_iter */
    0,                            /* tp_iternext */
    tgl_Peer_methods,             /* tp_methods */
    tgl_Peer_members,             /* tp_members */
    tgl_Peer_getseters,           /* tp_getset */
    0,                            /* tp_base */
    0,                            /* tp_dict */
    0,                            /* tp_descr_get */
    0,                            /* tp_descr_set */
    0,                            /* tp_dictoffset */
    (initproc)tgl_Peer_init,      /* tp_init */
    0,                            /* tp_alloc */
    tgl_Peer_new,                 /* tp_new */
};


PyObject *
tgl_Peer_FromTglPeer(tgl_peer_t *peer) {
  tgl_Peer *self = (tgl_Peer *) tgl_Peer_new((PyTypeObject *)&tgl_PeerType, Py_None, Py_None);

  self->peer = peer;
  return (PyObject *) self;
}

//
// struct tgl_message wrapper
//

static void
tgl_Msg_dealloc(tgl_Msg* self)
{
  Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject *
tgl_Msg_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  tgl_Msg *self;

  PyDateTime_IMPORT;
  self = (tgl_Msg *)type->tp_alloc(type, 0);
  return (PyObject *)self;
}

static int
tgl_Msg_init(tgl_Msg *self, PyObject *args, PyObject *kwds)
{
  PyErr_SetString(MsgError, "You cannot instantiate a tgl.Msg object, only the API can send them.");
  return -1;
}

static PyObject *
tgl_Msg_getid (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  ret = PyLong_FromLong(self->msg->id);

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getflags (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  ret = PyLong_FromLong(self->msg->flags);

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getmention (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  ret = ((self->msg->flags & TGLMF_MENTION) ? Py_True : Py_False);

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getout (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  ret = ((self->msg->flags & TGLMF_OUT) ? Py_True : Py_False);

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getunread (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  ret = ((self->msg->flags & TGLMF_UNREAD) ? Py_True : Py_False);

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getservice (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  ret = ((self->msg->flags & TGLMF_SERVICE) ? Py_True : Py_False);

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getaction (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  ret = PyLong_FromLong(self->msg->action.type);

  Py_XINCREF(ret);
  return ret;
}


static PyObject *
tgl_Msg_getsrc (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  if(tgl_get_peer_type (self->msg->from_id)) {
    tgl_peer_t *peer = tgl_peer_get (TLS, self->msg->from_id);
    if(peer)
      ret = tgl_Peer_FromTglPeer(peer);
    else {
      PyErr_SetString(PeerError, "Cannot Retrieve Peer. Internal tgl error");
      Py_RETURN_NONE;
    }

  } else {
    Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getdest (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  if(tgl_get_peer_type (self->msg->to_id)) {
    tgl_peer_t *peer = tgl_peer_get (TLS, self->msg->to_id);
    if(peer)
      ret = tgl_Peer_FromTglPeer(peer);
    else {
      PyErr_SetString(PeerError, "Cannot Retrieve Peer. Internal tgl error");
      Py_RETURN_NONE;
    }
  } else {
    Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_gettext (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  if(self->msg->message_len && self->msg->message && !(self->msg->flags & TGLMF_SERVICE)) {
    ret = PyUnicode_FromStringAndSize(self->msg->message, self->msg->message_len);
  } else {
    Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}



static PyObject *
tgl_Msg_getmedia (tgl_Msg *self, void *closure)
{
  PyObject *ret;
  
  // TODO probably want a custom class for media, but it's not too important right now.
  if(self->msg->media.type && self->msg->media.type != tgl_message_media_none && !(self->msg->flags & TGLMF_SERVICE)) {
    
    ret = PyDict_New();
    switch (self->msg->media.type) {
    case tgl_message_media_photo:
      py_add_string_field (ret, "type", "photo");
      py_add_string_field (ret, "caption", self->msg->media.caption);
      break;
    case tgl_message_media_document:
    case tgl_message_media_document_encr:
      py_add_string_field (ret, "type", "document");
      break;
    case tgl_message_media_unsupported:
      py_add_string_field (ret, "type", "unsupported");
      break;
    case tgl_message_media_geo:
      py_add_string_field (ret, "type", "geo");
      py_add_num_field (ret, "longitude", self->msg->media.geo.longitude);
      py_add_num_field (ret, "latitude", self->msg->media.geo.latitude);
      break;
    case tgl_message_media_contact:
      py_add_string_field (ret, "type", "contact");
      py_add_string_field (ret, "phone", self->msg->media.phone);
      py_add_string_field (ret, "first_name", self->msg->media.first_name);
      py_add_string_field (ret, "last_name", self->msg->media.last_name);
      py_add_num_field (ret, "user_id", self->msg->media.user_id);
      break;
    case tgl_message_media_webpage:
      py_add_string_field (ret, "type", "webpage");
      py_add_string_field (ret, "type", "webpage");
      py_add_string_field (ret, "url", self->msg->media.webpage->url);
      py_add_string_field (ret, "title", self->msg->media.webpage->title);
      py_add_string_field (ret, "description", self->msg->media.webpage->description);
      py_add_string_field (ret, "author", self->msg->media.webpage->author);
      break;
    case tgl_message_media_venue:
      py_add_string_field (ret, "type", "venue");
      py_add_num_field (ret, "longitude", self->msg->media.venue.geo.longitude);
      py_add_num_field (ret, "latitude", self->msg->media.venue.geo.latitude);
      py_add_string_field (ret, "title", self->msg->media.venue.title);
      py_add_string_field (ret, "address", self->msg->media.venue.address);
      py_add_string_field (ret, "provider", self->msg->media.venue.provider);
      py_add_string_field (ret, "venue_id", self->msg->media.venue.venue_id);
      break;
    default:
      py_add_string_field (ret, "type", "unknown");
    }
  } else {
    Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getdate (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  if(self->msg->date) {
    ret = get_datetime(self->msg->date);
  } else {
    Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getfwd_src (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  if(tgl_get_peer_type (self->msg->fwd_from_id)) {
    tgl_peer_t *peer = tgl_peer_get (TLS, self->msg->fwd_from_id);
    if(peer)
      ret = tgl_Peer_FromTglPeer(peer);
    else {
      PyErr_SetString(PeerError, "Cannot Retrieve Peer. Internal tgl error");
      Py_RETURN_NONE;
    }
  } else {
    Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getfwd_date (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  if(tgl_get_peer_type (self->msg->fwd_from_id)) {
    ret = get_datetime(self->msg->fwd_date);
  } else {
    Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getreply (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  if(self->msg->reply_id) {
    struct tgl_message *MR = tgl_message_get (TLS, self->msg->reply_id);
    if(MR) {
      ret = tgl_Msg_FromTglMsg(MR);
    } else {
      Py_RETURN_NONE;
    }
  } else {
    Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getreply_id (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  if(self->msg->reply_id) {
    ret = PyLong_FromLong(self->msg->reply_id);
  } else {
    Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

// All load methods are implemented the same, just alias load_document
static PyObject *
tgl_Msg_load_document (tgl_Msg *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"callback", NULL};

  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "O", kwlist, &callback)) {
    PyObject *api_call;

    api_call = Py_BuildValue("OO", (PyObject*) self, callback);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_load_document(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Msg_load_document_thumb (tgl_Msg *self, PyObject *args, PyObject *kwargs)
{
  static char *kwlist[] = {"callback", NULL};

  PyObject *callback = NULL;

  if(PyArg_ParseTupleAndKeywords(args, kwargs, "O", kwlist, &callback)) {
    PyObject *api_call;

    api_call = Py_BuildValue("OO", (PyObject*) self, callback);

    Py_INCREF(Py_None);
    Py_XINCREF(api_call);

    return py_load_document_thumb(Py_None, api_call);
  } else {
    PyErr_Print();
    Py_XINCREF(Py_False);
    return Py_False;
  }

}

static PyObject *
tgl_Msg_repr(tgl_Msg *self)
{
  PyObject *ret;
#if PY_VERSION_HEX < 0x02070900
  ret = PyUnicode_FromFormat("<tgl.Msg id=%ld>", self->msg->id);
#else
  ret = PyUnicode_FromFormat("<tgl.Msg id=%ld, flags=%d, mention=%R, out=%R, unread=%R, service=%R, src=%R, "
                             "dest=%R, text=%R, media=%R, date=%R, fwd_src=%R, fwd_date=%R, reply_id=%R, reply=%R>",
                             self->msg->id, self->msg->flags,
                             PyObject_GetAttrString((PyObject*)self, "mention"),
                             PyObject_GetAttrString((PyObject*)self, "out"),
                             PyObject_GetAttrString((PyObject*)self, "unread"),
                             PyObject_GetAttrString((PyObject*)self, "service"),
                             PyObject_GetAttrString((PyObject*)self, "src"),
                             PyObject_GetAttrString((PyObject*)self, "dest"),
                             PyObject_GetAttrString((PyObject*)self, "text"),
                             PyObject_GetAttrString((PyObject*)self, "media"),
                             PyObject_GetAttrString((PyObject*)self, "date"),
                             PyObject_GetAttrString((PyObject*)self, "fwd_src"),
                             PyObject_GetAttrString((PyObject*)self, "fwd_date"),
                             PyObject_GetAttrString((PyObject*)self, "reply_id"),
                             PyObject_GetAttrString((PyObject*)self, "reply")
        );
#endif
  return ret;
}


static PyGetSetDef tgl_Msg_getseters[] = {
  {"id", (getter)tgl_Msg_getid, NULL, "", NULL},
  {"flags", (getter)tgl_Msg_getflags, NULL, "", NULL},
  {"mention", (getter)tgl_Msg_getmention, NULL, "", NULL},
  {"out", (getter)tgl_Msg_getout, NULL, "", NULL}, 
  {"unread", (getter)tgl_Msg_getunread, NULL, "", NULL},
  {"service", (getter)tgl_Msg_getservice, NULL, "", NULL},
  {"src", (getter)tgl_Msg_getsrc, NULL, "", NULL},
  {"dest", (getter)tgl_Msg_getdest, NULL, "", NULL},
  {"text", (getter)tgl_Msg_gettext, NULL, "", NULL},
  {"media", (getter)tgl_Msg_getmedia, NULL, "", NULL},
  {"date", (getter)tgl_Msg_getdate, NULL, "", NULL},
  {"fwd_src", (getter)tgl_Msg_getfwd_src, NULL, "", NULL},
  {"fwd_date", (getter)tgl_Msg_getfwd_date, NULL, "", NULL},
  {"reply", (getter)tgl_Msg_getreply, NULL, "", NULL},
  {"reply_id", (getter)tgl_Msg_getreply_id, NULL, "", NULL},
  {"action", (getter)tgl_Msg_getaction, NULL, "", NULL},
  {NULL}  /* Sentinel */
};

static PyMemberDef tgl_Msg_members[] = {
  {NULL}  /* Sentinel */
};



static PyMethodDef tgl_Msg_methods[] = {
  {"load_document", (PyCFunction)tgl_Msg_load_document, METH_VARARGS | METH_KEYWORDS, ""},
  {"load_photo", (PyCFunction)tgl_Msg_load_document, METH_VARARGS | METH_KEYWORDS, ""},
  {"load_audio", (PyCFunction)tgl_Msg_load_document, METH_VARARGS | METH_KEYWORDS, ""},
  {"load_video", (PyCFunction)tgl_Msg_load_document, METH_VARARGS | METH_KEYWORDS, ""},
  {"load_document_thumb", (PyCFunction)tgl_Msg_load_document_thumb, METH_VARARGS | METH_KEYWORDS, ""},
  {"load_video_thumb", (PyCFunction)tgl_Msg_load_document_thumb, METH_VARARGS | METH_KEYWORDS, ""},
  {NULL}  /* Sentinel */
};


PyTypeObject tgl_MsgType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "tgl.Msg",                   /* tp_name */
    sizeof(tgl_Msg),              /* tp_basicsize */
    0,                            /* tp_itemsize */
    (destructor)tgl_Msg_dealloc,  /* tp_dealloc */
    0,                            /* tp_print */
    0,                            /* tp_getattr */
    0,                            /* tp_setattr */
    0,                            /* tp_reserved */
    (reprfunc)tgl_Msg_repr,      /* tp_repr */
    0,                            /* tp_as_number */
    0,                            /* tp_as_sequence */
    0,                            /* tp_as_mapping */
    0,                            /* tp_hash  */
    0,                            /* tp_call */
    0,                            /* tp_str */
    0,                            /* tp_getattro */
    0,                            /* tp_setattro */
    0,                            /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,           /* tp_flags */
    "tgl Message",                /* tp_doc */
    0,                            /* tp_traverse */
    0,                            /* tp_clear */
    0,                            /* tp_richcompare */
    0,                            /* tp_weaklistoffset */
    0,                            /* tp_iter */
    0,                            /* tp_iternext */
    tgl_Msg_methods,              /* tp_methods */
    tgl_Msg_members,              /* tp_members */
    tgl_Msg_getseters,            /* tp_getset */
    0,                            /* tp_base */
    0,                            /* tp_dict */
    0,                            /* tp_descr_get */
    0,                            /* tp_descr_set */
    0,                            /* tp_dictoffset */
    (initproc)tgl_Msg_init,       /* tp_init */
    0,                            /* tp_alloc */
    tgl_Msg_new,                  /* tp_new */
};


PyObject *
tgl_Msg_FromTglMsg(struct tgl_message *msg) {
  tgl_Msg *self = (tgl_Msg *) tgl_Msg_new((PyTypeObject *)&tgl_MsgType, Py_None, Py_None);

  self->msg = msg;
  return (PyObject *) self;
}

#endif
