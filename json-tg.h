#ifndef __JSON_TG_H__
#define __JSON_TG_H__
#include "config.h"
#ifdef USE_JSON
#include <jansson.h>
#include <tgl/tgl.h>
#include <tgl/tgl-layout.h>
json_t *json_pack_message (struct tgl_message *M);
json_t *json_pack_updates (unsigned flags);
json_t *json_pack_peer (tgl_peer_id_t id);
json_t *json_pack_read (struct tgl_message *M);
json_t *json_pack_user_status (struct tgl_user *U);
#endif
#endif
