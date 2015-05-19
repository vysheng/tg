#ifndef __PYTHON_TYPES_H__
#define __PYTHON_TYPES_H__

#include <Python.h>
#include <tgl/tgl.h>
#include <tgl/tools.h>
#include <tgl/updates.h>
#include <tgl/tgl-structures.h>
#include <stdlib.h>
#include <Python.h>
#include "structmember.h"

typedef struct {
  PyObject_HEAD
  tgl_peer_t peer;
} tgl_Peer;

extern struct tgl_state *TLS;

// modeled after tgls_free_peer 
static void
tgl_Peer_dealloc(tgl_Peer* self)
{
  switch(self->peer.id.type) {
    case TGL_PEER_USER:
      if (self->peer.user.first_name) tfree_str(self->peer.user.first_name);
      if (self->peer.user.last_name) tfree_str(self->peer.user.last_name);
      if (self->peer.user.print_name) tfree_str(self->peer.user.print_name);
      if (self->peer.user.phone) tfree_str(self->peer.user.phone);
      if (self->peer.user.real_first_name) tfree_str(self->peer.user.real_first_name);
      if (self->peer.user.real_last_name) tfree_str(self->peer.user.real_last_name);
      if (self->peer.user.status.ev) { tgl_remove_status_expire (TLS, &self->peer.user); }
      tgls_free_photo (TLS, self->peer.user.photo);
      break;
    case TGL_PEER_CHAT:
      if (self->peer.chat.title) tfree_str(self->peer.chat.title);
      if (self->peer.chat.print_title) tfree_str(self->peer.chat.print_title);
      if (self->peer.chat.user_list) 
        tfree(self->peer.chat.user_list, self->peer.chat.user_list_size * sizeof(struct tgl_chat_user));
      tgls_free_photo (TLS, self->peer.chat.photo);
      break;
    case TGL_PEER_ENCR_CHAT:
      if (self->peer.encr_chat.print_name) tfree_str(self->peer.encr_chat.print_name);
      if (self->peer.encr_chat.g_key) tfree (self->peer.encr_chat.g_key, 256);
      break;
    default:
     PyErr_SetString(PyExc_TypeError, "peer.type not supported!");
  }
 
  Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject *
tgl_Peer_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  tgl_Peer *self;

  self = (tgl_Peer *)type->tp_alloc(type, 0);
  if (self != NULL) {
    self->peer.id.type = 0;
    self->peer.id.id = 0;
  }

  return (PyObject *)self;
}



static int
tgl_Peer_init(tgl_Peer *self, PyObject *args, PyObject *kwds)
{
  static char *kwlist[] = {"type", "id", NULL};

  if(!PyArg_ParseTupleAndKeywords(args, kwds, "|ii", kwlist, 
                                  &self->peer.id.type,
                                  &self->peer.id.id))
    return -1;

  return 0;
}

static PyObject *
tgl_Peer_getname (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer.id.type) {
    case TGL_PEER_USER:
      ret = PyUnicode_FromString(self->peer.user.print_name);
      break;
    case TGL_PEER_CHAT:
      ret = PyUnicode_FromString(self->peer.chat.print_title);
      break;
    case TGL_PEER_ENCR_CHAT:
      ret = PyUnicode_FromString(self->peer.encr_chat.print_name);
      break;
    default:
     PyErr_SetString(PyExc_TypeError, "peer.type not supported!");
     Py_RETURN_NONE;
  }


  Py_XINCREF(ret);
  return ret;
}


static PyObject *
tgl_Peer_getuser_id (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer.id.type) {
    case TGL_PEER_USER:
      ret = PyLong_FromLong(self->peer.id.id);
      break;
    case TGL_PEER_CHAT:
      PyErr_SetString(PyExc_TypeError, "peer.type == TGL_PEER_CHAT has no user_id");
      Py_RETURN_NONE;
 
      break;
    case TGL_PEER_ENCR_CHAT:
      ret = PyLong_FromLong(self->peer.encr_chat.user_id);
      break;
    default:
     PyErr_SetString(PyExc_TypeError, "peer.type not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_getuser_list (tgl_Peer *self, void *closure)
{
  PyObject *ret, *peer;
  int i;
  struct tgl_chat_user *user_list;
  struct tgl_user *user;

  switch(self->peer.id.type) {
    case TGL_PEER_CHAT:
      ret = PyList_New(0);
        for(i = 0; i < self->peer.chat.user_list_size; i++) {
        // TODO: Sort tgl_user objects, maybe offline mode is enoug?
        user_list = self->peer.chat.user_list + i;
        PyList_Append(ret, PyLong_FromLong(user_list->user_id));
      }
      break;
    case TGL_PEER_ENCR_CHAT:
    case TGL_PEER_USER:
      PyErr_SetString(PyExc_TypeError, "Only peer.type == TGL_PEER_CHAT has user_list");
      Py_RETURN_NONE;
      break;
    default:
     PyErr_SetString(PyExc_TypeError, "peer.type not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_getuser_status(tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer.id.type) {
    case TGL_PEER_USER:
      ret = PyDict_New();
      PyDict_SetItemString(ret, "online", self->peer.user.status.online? Py_True : Py_False);
      PyDict_SetItemString(ret, "when", PyDateTime_FromTimestamp(Py_BuildValue("(O)",
                                        PyLong_FromLong(self->peer.user.status.when))));

      break;
    case TGL_PEER_CHAT:
    case TGL_PEER_ENCR_CHAT:
      PyErr_SetString(PyExc_TypeError, "Only peer.type == TGL_PEER_USER has user_status");
      Py_RETURN_NONE;
      break;
    default:
     PyErr_SetString(PyExc_TypeError, "peer.type not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_getphone (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer.id.type) {
    case TGL_PEER_USER:
      ret = PyUnicode_FromString(self->peer.user.phone);
      break;
    case TGL_PEER_CHAT:
    case TGL_PEER_ENCR_CHAT:
      PyErr_SetString(PyExc_TypeError, "Only peer.type == TGL_PEER_USER has phone");
      Py_RETURN_NONE;
      break;
    default:
     PyErr_SetString(PyExc_TypeError, "peer.type not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_getusername (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  switch(self->peer.id.type) {
    case TGL_PEER_USER:
      ret = PyUnicode_FromString(self->peer.user.username);
      break;
    case TGL_PEER_CHAT:
    case TGL_PEER_ENCR_CHAT:
      PyErr_SetString(PyExc_TypeError, "Only peer.type == TGL_PEER_USER has username");
      Py_RETURN_NONE;
      break;
    default:
     PyErr_SetString(PyExc_TypeError, "peer.type not supported!");
     Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyGetSetDef tgl_Peer_getseters[] = {
  {"name", (getter)tgl_Peer_getname, NULL, "", NULL},
  {"user_id", (getter)tgl_Peer_getuser_id, NULL, "", NULL},
  {"user_list", (getter)tgl_Peer_getuser_list, NULL, "", NULL},
  {"user_status", (getter)tgl_Peer_getuser_status, NULL, "", NULL},
  {"phone", (getter)tgl_Peer_getphone, NULL, "", NULL},
  {"username", (getter)tgl_Peer_getusername, NULL, "", NULL},
  {NULL}  /* Sentinel */
};

static PyMemberDef tgl_Peer_members[] = {
  {"type", T_INT, offsetof(tgl_Peer, peer.id.type), 0, "Peer Type"},
  {"id", T_INT, offsetof(tgl_Peer, peer.id.id),     0, "Peer ID"  },
  {NULL}  /* Sentinel */
};

static PyObject *
tgl_Peer_type_name(tgl_Peer* self)
{
  PyObject *name;

  switch(self->peer.id.type) {
    case TGL_PEER_USER:
      name = PyUnicode_FromString("user");
      break;
    case TGL_PEER_CHAT:
      name = PyUnicode_FromString("chat");
      break;
    case TGL_PEER_ENCR_CHAT:
      name = PyUnicode_FromString("encr_chat");
      break;
    default:
      name = PyUnicode_FromString("unknown");
    }
  return name;
}

static PyMethodDef tgl_Peer_methods[] = {
  {"type_name", (PyCFunction)tgl_Peer_type_name, METH_NOARGS,
    "Return the string representation of the peer type."
  },
  {NULL}  /* Sentinel */
};


static PyTypeObject tgl_PeerType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "tgl.Peer",                   /* tp_name */
    sizeof(tgl_Peer),             /* tp_basicsize */
    0,                            /* tp_itemsize */
    (destructor)tgl_Peer_dealloc, /* tp_dealloc */
    0,                            /* tp_print */
    0,                            /* tp_getattr */
    0,                            /* tp_setattr */
    0,                            /* tp_reserved */
    0,                            /* tp_repr */
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
    "tgl Peer",                   /* tp_doc */
    0,                            /* tp_traverse */
    0,                            /* tp_clear */
    0,                            /* tp_richcompare */
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


static PyObject *
tgl_Peer_FromTglPeer(tgl_peer_t *peer) {
  tgl_Peer *self = (tgl_Peer *) tgl_Peer_new((PyTypeObject *)&tgl_PeerType, Py_None, Py_None);

  memcpy(&self->peer, peer, sizeof(tgl_peer_t));

  switch(self->peer.id.type) {
  case TGL_PEER_USER:
    // print_name
    if(peer->user.print_name) {
      self->peer.user.print_name = (char*)malloc(strlen(peer->user.print_name));
      memcpy(self->peer.user.print_name, peer->user.print_name, strlen(peer->user.print_name));
    }
    // phone
    if(peer->user.phone) {
      self->peer.user.phone = (char*)malloc(strlen(peer->user.phone));
      memcpy(self->peer.user.phone, peer->user.phone, strlen(peer->user.phone));
    }
    // username
    if(peer->user.username) {
      self->peer.user.username = (char*)malloc(strlen(peer->user.username));
      memcpy(self->peer.user.username, peer->user.username, strlen(peer->user.username));
    }
    break;
  case TGL_PEER_CHAT:
    // print_title
    if(peer->chat.print_title) {
      self->peer.chat.print_title = (char*)malloc(strlen(peer->chat.print_title));
      memcpy(self->peer.chat.print_title, peer->chat.print_title, strlen(peer->chat.print_title));
    }
    // user_list
    if(peer->chat.user_list_size > 0) {
      self->peer.chat.user_list = (struct tgl_chat_user*)malloc(self->peer.chat.user_list_size * 
                                                                sizeof(struct tgl_chat_user));
      memcpy(self->peer.chat.user_list, peer->chat.user_list, 
             peer->chat.user_list_size * sizeof(struct tgl_chat_user));
    }
    break;
  case TGL_PEER_ENCR_CHAT:
    // print_name
    if(peer->encr_chat.print_name) {
      self->peer.encr_chat.print_name = (char*)malloc(strlen(peer->encr_chat.print_name));
      memcpy(self->peer.encr_chat.print_name, peer->encr_chat.print_name, strlen(peer->encr_chat.print_name));
    }
    // g_key
    if(peer->encr_chat.g_key) {
      self->peer.encr_chat.g_key = (unsigned char*)malloc(256);
      memcpy(self->peer.encr_chat.g_key, peer->encr_chat.g_key, 256);
    }
    break;
  default:
    assert(0);
  }

  return (PyObject *) self;
}

#endif
