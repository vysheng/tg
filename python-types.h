#ifndef __PYTHON_TYPES_H__
#define __PYTHON_TYPES_H__

#include <Python.h>
#include <tgl/tgl.h>

typedef struct {
  PyObject_HEAD
  tgl_peer_t *peer;
} tgl_Peer;

typedef struct {
  PyObject_HEAD
  struct tgl_message *msg;
} tgl_Msg;


PyObject * tgl_Peer_FromTglPeer(tgl_peer_t *peer);
PyObject * tgl_Msg_FromTglMsg(struct tgl_message *peer);

#endif
