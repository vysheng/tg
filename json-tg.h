#ifndef __JSON_TG_H__
#define __JSON_TG_H__
#include "config.h"
#ifdef USE_JSON
#include <jansson.h>
#include <tgl.h>
#include <tgl-layout.h>
json_t *json_pack_message (struct tgl_message *M);
json_t *json_pack_updates (unsigned flags);
json_t *json_pack_peer (tgl_peer_id_t id, tgl_peer_t *P);
json_t *json_pack_read (struct tgl_message *M);
#endif
#endif
