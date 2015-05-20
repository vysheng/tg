
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

  self = (tgl_Peer *)type->tp_alloc(type, 0);
  return (PyObject *)self;
}

static int
tgl_Peer_init(tgl_Peer *self, PyObject *args, PyObject *kwds)
{
  static char *kwlist[] = {"type", "id", NULL};
  tgl_peer_id_t peer_id;
  if(!PyArg_ParseTupleAndKeywords(args, kwds, "|ii", kwlist, 
                                  &peer_id.type,
                                  &peer_id.id))
    return -1;

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

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
      ret = PyLong_FromLong(self->peer->id.id);
      break;
    case TGL_PEER_CHAT:
      PyErr_SetString(PyExc_TypeError, "peer.type == TGL_PEER_CHAT has no user_id");
      Py_RETURN_NONE;
 
      break;
    case TGL_PEER_ENCR_CHAT:
      ret = PyLong_FromLong(self->peer->encr_chat.user_id);
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

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
      ret = PyDict_New();
      PyDict_SetItemString(ret, "online", self->peer->user.status.online? Py_True : Py_False);
      PyDict_SetItemString(ret, "when", PyDateTime_FromTimestamp(Py_BuildValue("(O)",
                                        PyLong_FromLong(self->peer->user.status.when))));

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

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
      ret = PyUnicode_FromString(self->peer->user.phone);
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

  switch(self->peer->id.type) {
    case TGL_PEER_USER:
      ret = PyUnicode_FromString(self->peer->user.username);
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
  {"name", (getter)tgl_Peer_getname, NULL, "", NULL},
  {"user_id", (getter)tgl_Peer_getuser_id, NULL, "", NULL},
  {"user_list", (getter)tgl_Peer_getuser_list, NULL, "", NULL},
  {"user_status", (getter)tgl_Peer_getuser_status, NULL, "", NULL},
  {"phone", (getter)tgl_Peer_getphone, NULL, "", NULL},
  {"username", (getter)tgl_Peer_getusername, NULL, "", NULL},
  {NULL}  /* Sentinel */
};

static PyMemberDef tgl_Peer_members[] = {
  {NULL}  /* Sentinel */
};

static PyObject *
tgl_Peer_type_name(tgl_Peer* self)
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

  self = (tgl_Msg *)type->tp_alloc(type, 0);
  return (PyObject *)self;
}

static int
tgl_Msg_init(tgl_Msg *self, PyObject *args, PyObject *kwds)
{
  return 0;
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
tgl_Msg_getfrom (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  if(tgl_get_peer_type (self->msg->from_id)) {
    ret = tgl_Peer_FromTglPeer(tgl_peer_get (TLS, self->msg->from_id));
  } else {
    Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getto (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  if(tgl_get_peer_type (self->msg->to_id)) {
    ret = tgl_Peer_FromTglPeer(tgl_peer_get (TLS, self->msg->to_id));
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
    ret = PyDateTime_FromTimestamp(Py_BuildValue("(O)",
                                   PyLong_FromLong(self->msg->date)));
  } else {
    Py_RETURN_NONE;
  }

  Py_XINCREF(ret);
  return ret;
}

static PyObject *
tgl_Msg_getfwd_from (tgl_Msg *self, void *closure)
{
  PyObject *ret;

  if(tgl_get_peer_type (self->msg->fwd_from_id)) {
    ret = tgl_Peer_FromTglPeer(tgl_peer_get (TLS, self->msg->fwd_from_id));
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
    ret = PyDateTime_FromTimestamp(Py_BuildValue("(O)",
                                   PyLong_FromLong(self->msg->fwd_date)));
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




static PyGetSetDef tgl_Msg_getseters[] = {
  {"id", (getter)tgl_Msg_getid, NULL, "", NULL},
  {"flags", (getter)tgl_Msg_getflags, NULL, "", NULL},
  {"mention", (getter)tgl_Msg_getmention, NULL, "", NULL},
  {"out", (getter)tgl_Msg_getout, NULL, "", NULL}, 
  {"unread", (getter)tgl_Msg_getunread, NULL, "", NULL},
  {"service", (getter)tgl_Msg_getservice, NULL, "", NULL},
  {"from", (getter)tgl_Msg_getfrom, NULL, "", NULL},
  {"to", (getter)tgl_Msg_getto, NULL, "", NULL},
  {"text", (getter)tgl_Msg_gettext, NULL, "", NULL},
  {"media", (getter)tgl_Msg_getmedia, NULL, "", NULL},
  {"date", (getter)tgl_Msg_getdate, NULL, "", NULL},
  {"fwd_from", (getter)tgl_Msg_getfwd_from, NULL, "", NULL},
  {"fwd_date", (getter)tgl_Msg_getfwd_date, NULL, "", NULL},
  {"reply", (getter)tgl_Msg_getreply, NULL, "", NULL},
  {"reply_id", (getter)tgl_Msg_getreply_id, NULL, "", NULL},
  {NULL}  /* Sentinel */
};

static PyMemberDef tgl_Msg_members[] = {
  {NULL}  /* Sentinel */
};

static PyMethodDef tgl_Msg_methods[] = {
  {NULL}  /* Sentinel */
};


PyTypeObject tgl_MsgType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "tgl.Peer",                   /* tp_name */
    sizeof(tgl_Msg),              /* tp_basicsize */
    0,                            /* tp_itemsize */
    (destructor)tgl_Msg_dealloc,  /* tp_dealloc */
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
