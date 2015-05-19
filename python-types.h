#ifndef __PYTHON_TYPES_H__
#define __PYTHON_TYPES_H__

#include <Python.h>
#include <tgl/tgl.h>

typedef struct {
  PyObject_HEAD
  tgl_peer_t peer;
} tgl_Peer;

PyObject * tgl_Peer_FromTglPeer(tgl_peer_t *peer);

#endif
