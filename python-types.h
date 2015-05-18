#ifndef __PYTHON_TYPES_H__
#define __PYTHON_TYPES_H__

#include <Python.h>
#include <tgl/tgl.h>
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
      if (self->peer.first_name) tfree_str(self->peer.first_name);
      if (self->peer.last_name) tfree_str(self->peer.last_name);
      if (self->peer.print_name) tfree_str(self->peer.print_name);
      if (self->peer.phone) tfree_str(self->peer.phone);
      if (self->peer.real_first_name) tfree_str(self->peer.real_first_name);
      if (self->peer.real_last_name) tfree_str(self->peer.real_last_name);
      if (self->peer.status.ev) { tgl_remove_status_expire (TLS, &self->peer); }
      tgls_free_photo (TLS, self->peer.photo);
      break;
    case TGL_PEER_CHAT:
      if (self->peer.title) tfree_str(self->peer.title);
      if (self->peer.print_title) tfree_str(self->peer.print_title);
      if (self->peer.user_list) tfree(self->peer.user_list, self->peer.user_list_size * sizeof(tgl_chat_user));
      tgls_free_photo (TLS, self->peer.photo);
      break;
    case TGL_PEER_ENCR_CHAT:
      if (self->peer.print_name) tfree_str(self->peer.print_name);
      if (self->peer.g_key) tfree (self->peer.g_key, 256);
      break;
    default:
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
tgl_Peer_get_name (tgl_Peer *self, void *closure)
{
  PyObject *ret;
  if(self->peer.id.type == TGL_PEER_CHAT)
    ret = PyUnicode_FromString(self->peer.print_title);
  else
    ret = PyUnicode_FromString(self->peer.print_name);

  Py_XINCREF(ret);
  return ret;
}


static PyObject *
tgl_Peer_getuser_id (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  if(self->peer.id.type == TGL_PEER_CHAT)
  {
    PyErr_SetString(PyExc_TypeError,
                    "peer.type == TGL_PEER_CHAT has no user_id");
    Py_RETURN_NONE;
  } else {
    ret = PyLong_FromLong(self->peer.user_id);
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_getuser_list (tgl_Peer *self, void *closure)
{
  PyObject *ret, *peer;
  tgl_chat_user *user_list;

  if(self->peer.id.type == TGL_PEER_CHAT)
  {
    ret = PyList_New();
    for(int i = 0; i < self->peer.user_list_size; i++) {
       user_list = self->peer.userlist + i;
       
    }
  } else {
    PyErr_SetString(PyExc_TypeError,
                    "Only peer.type == TGL_PEER_CHAT has user_list");
    Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_get (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_get (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_get (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_get (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_get (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_get (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Peer_get (tgl_Peer *self, void *closure)
{
  PyObject *ret;

  Py_XINCREF(ret);
  return ret;
}



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
      name = PyUnicode_FromString("secret_chat");
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
    0,                            /* tp_getset */
    0,                            /* tp_base */
    0,                            /* tp_dict */
    0,                            /* tp_descr_get */
    0,                            /* tp_descr_set */
    0,                            /* tp_dictoffset */
    (initproc)tgl_Peer_init,      /* tp_init */
    0,                            /* tp_alloc */
    tgl_Peer_new,                 /* tp_new */
};

#endif
