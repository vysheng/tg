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

    Copyright Vitaly Valtman 2013-2014
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef USE_LUA
#include "lua-tg.h"

#include "include.h"
#include <string.h>
#include <stdlib.h>


#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#ifdef EVENT_V2
#include <event2/event.h>
#else
#include <event.h>
#include "event-old.h"
#endif
lua_State *luaState;

//#include "interface.h"
//#include "auto/constants.h"
#include "tgl.h"
#include "interface.h"

#include <assert.h>
extern int verbosity;
extern struct tgl_state *TLS;

static int have_file;

#define my_lua_checkstack(L,x) assert (lua_checkstack (L, x))
void push_user (tgl_peer_t *P UU);
void push_peer (tgl_peer_id_t id, tgl_peer_t *P);

void lua_add_string_field (const char *name, const char *value) {
  assert (name && strlen (name));
  if (!value || !strlen (value)) { return; }
  my_lua_checkstack (luaState, 3);
  lua_pushstring (luaState, name);
  lua_pushstring (luaState, value);
  lua_settable (luaState, -3);
}

void lua_add_string_field_arr (int num, const char *value) {
  if (!value || !strlen (value)) { return; }
  my_lua_checkstack (luaState, 3);
  lua_pushnumber (luaState, num);
  lua_pushstring (luaState, value);
  lua_settable (luaState, -3);
}

void lua_add_num_field (const char *name, double value) {
  assert (name && strlen (name));
  my_lua_checkstack (luaState, 3);
  lua_pushstring (luaState, name);
  lua_pushnumber (luaState, value);
  lua_settable (luaState, -3);
}

void push_tgl_peer_type (int x) {
  switch (x) {
  case TGL_PEER_USER:
    lua_pushstring (luaState, "user");
    break;
  case TGL_PEER_CHAT:
    lua_pushstring (luaState, "chat");
    break;
  case TGL_PEER_ENCR_CHAT:
    lua_pushstring (luaState, "encr_chat");
    break;
  default:
    assert (0);
  }
}

void push_user (tgl_peer_t *P UU) {
  my_lua_checkstack (luaState, 4);
  lua_add_string_field ("first_name", P->user.first_name);
  lua_add_string_field ("last_name", P->user.last_name);
  lua_add_string_field ("real_first_name", P->user.real_first_name);
  lua_add_string_field ("real_last_name", P->user.real_last_name);
  lua_add_string_field ("phone", P->user.phone);
  if (P->user.access_hash) {
    lua_add_num_field ("access_hash", 1);
  }
}

void push_chat (tgl_peer_t *P) {
  my_lua_checkstack (luaState, 4);
  assert (P->chat.title);
  lua_add_string_field ("title", P->chat.title);
  lua_add_num_field ("members_num", P->chat.users_num);
  if (P->chat.user_list) {
    lua_pushstring (luaState, "members");
    lua_newtable (luaState);
    int i;
    for (i = 0; i < P->chat.users_num; i++) {
      lua_pushnumber (luaState, i);
      tgl_peer_id_t id = TGL_MK_USER (P->chat.user_list[i].user_id);
      push_peer (id, tgl_peer_get (TLS, id));
      lua_settable (luaState, -3);
    }
    lua_settable (luaState, -3);
  }
}

void push_encr_chat (tgl_peer_t *P) {
  my_lua_checkstack (luaState, 4);
  lua_pushstring (luaState, "user");
  push_peer (TGL_MK_USER (P->encr_chat.user_id), tgl_peer_get (TLS, TGL_MK_USER (P->encr_chat.user_id)));
  lua_settable (luaState, -3);
}

void push_update_types (unsigned flags) {
  my_lua_checkstack (luaState, 4);
  lua_newtable (luaState);
  int cc = 0;
  
  
  if (flags & TGL_UPDATE_CREATED) {
    lua_add_string_field_arr (cc++, "created");
  }  
  if (flags & TGL_UPDATE_DELETED) {
    lua_add_string_field_arr (cc++, "deleted");
  }  
  if (flags & TGL_UPDATE_PHONE) {
    lua_add_string_field_arr (cc++, "phone");
  }
  if (flags & TGL_UPDATE_CONTACT) {
    lua_add_string_field_arr (cc++, "contact");
  }
  if (flags & TGL_UPDATE_PHOTO) {
    lua_add_string_field_arr (cc++, "photo");
  }
  if (flags & TGL_UPDATE_BLOCKED) {
    lua_add_string_field_arr (cc++, "blocked");
  }
  if (flags & TGL_UPDATE_REAL_NAME) {
    lua_add_string_field_arr (cc++, "real_name");
  }
  if (flags & TGL_UPDATE_NAME) {
    lua_add_string_field_arr (cc++, "name");
  }
  if (flags & TGL_UPDATE_REQUESTED) {
    lua_add_string_field_arr (cc++, "requested");
  }
  if (flags & TGL_UPDATE_WORKING) {
    lua_add_string_field_arr (cc++, "working");
  }
  if (flags & TGL_UPDATE_FLAGS) {
    lua_add_string_field_arr (cc++, "flags");
  }
  if (flags & TGL_UPDATE_TITLE) {
    lua_add_string_field_arr (cc++, "title");
  }
  if (flags & TGL_UPDATE_ADMIN) {
    lua_add_string_field_arr (cc++, "admin");
  }
  if (flags & TGL_UPDATE_MEMBERS) {
    lua_add_string_field_arr (cc++, "members");
  }
  if (flags & TGL_UPDATE_ACCESS_HASH) {
    lua_add_string_field_arr (cc++, "access_hash");
  }
  if (flags & TGL_UPDATE_USERNAME) {
    lua_add_string_field_arr (cc++, "username");
  }

}

void push_peer (tgl_peer_id_t id, tgl_peer_t *P) {
  lua_newtable (luaState);
 
  lua_add_num_field ("id", tgl_get_peer_id (id));
  lua_pushstring (luaState, "type");
  push_tgl_peer_type (tgl_get_peer_type (id));
  lua_settable (luaState, -3);


  if (!P || !(P->flags & FLAG_CREATED)) {
    lua_pushstring (luaState, "print_name"); 
    static char s[100];
    switch (tgl_get_peer_type (id)) {
    case TGL_PEER_USER:
      sprintf (s, "user#%d", tgl_get_peer_id (id));
      break;
    case TGL_PEER_CHAT:
      sprintf (s, "chat#%d", tgl_get_peer_id (id));
      break;
    case TGL_PEER_ENCR_CHAT:
      sprintf (s, "encr_chat#%d", tgl_get_peer_id (id));
      break;
    default:
      assert (0);
    }
    lua_pushstring (luaState, s); 
    lua_settable (luaState, -3); // flags
  
    return;
  }
  
  lua_add_string_field ("print_name", P->print_name);
  lua_add_num_field ("flags", P->flags);
  
  switch (tgl_get_peer_type (id)) {
  case TGL_PEER_USER:
    push_user (P);
    break;
  case TGL_PEER_CHAT:
    push_chat (P);
    break;
  case TGL_PEER_ENCR_CHAT:
    push_encr_chat (P);
    break;
  default:
    assert (0);
  }
}

void push_media (struct tgl_message_media *M) {
  my_lua_checkstack (luaState, 4);

  switch (M->type) {
  case tgl_message_media_photo:
  case tgl_message_media_photo_encr:
    lua_newtable (luaState);
    lua_add_string_field ("type", "photo");
    break;
  case tgl_message_media_video:
  case tgl_message_media_video_encr:
    lua_newtable (luaState);
    lua_add_string_field ("type", "video");
    break;
  case tgl_message_media_audio:
  case tgl_message_media_audio_encr:
    lua_newtable (luaState);
    lua_add_string_field ("type", "audio");
    break;
  case tgl_message_media_document:
  case tgl_message_media_document_encr:
    lua_newtable (luaState);
    lua_add_string_field ("type", "document");
    break;
  case tgl_message_media_unsupported:
    lua_newtable (luaState);
    lua_add_string_field ("type", "unsupported");
    break;
  case tgl_message_media_geo:
    lua_newtable (luaState);
    lua_add_string_field ("type", "geo");
    lua_add_num_field ("longitude", M->geo.longitude);
    lua_add_num_field ("latitude", M->geo.latitude);
    break;
  case tgl_message_media_contact:
    lua_newtable (luaState);
    lua_add_string_field ("type", "contact");
    lua_add_string_field ("phone", M->phone);
    lua_add_string_field ("first_name", M->first_name);
    lua_add_string_field ("last_name", M->last_name);
    lua_add_num_field ("user_id", M->user_id);
    break;
  default:
    lua_pushstring (luaState, "???");
  }
}

void push_message (struct tgl_message *M) {
  assert (M);
  my_lua_checkstack (luaState, 10);
  lua_newtable (luaState);

  static char s[30];
  snprintf (s, 30, "%lld", M->id);
  lua_add_string_field ("id", s);
  lua_add_num_field ("flags", M->flags);
 
  if (tgl_get_peer_type (M->fwd_from_id)) {
    lua_pushstring (luaState, "fwd_from");
    push_peer (M->fwd_from_id, tgl_peer_get (TLS, M->fwd_from_id));
    lua_settable (luaState, -3); // fwd_from

    lua_add_num_field ("fwd_date", M->fwd_date);
  }
 
  lua_pushstring (luaState, "from");
  push_peer (M->from_id, tgl_peer_get (TLS, M->from_id));
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "to");
  push_peer (M->to_id, tgl_peer_get (TLS, M->to_id));
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "out");
  lua_pushboolean (luaState, M->out);
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "unread");
  lua_pushboolean (luaState, M->unread);
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "date");
  lua_pushnumber (luaState, M->date);
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "service");
  lua_pushboolean (luaState, M->service);
  lua_settable (luaState, -3); 

  if (!M->service) {  
    if (M->message_len && M->message) {
      lua_pushstring (luaState, "text");
      lua_pushlstring (luaState, M->message, M->message_len);
      lua_settable (luaState, -3); 
    }
    if (M->media.type && M->media.type != tgl_message_media_none) {
      lua_pushstring (luaState, "media");
      push_media (&M->media);
      lua_settable (luaState, -3); 
    }
  }
}

void lua_binlog_end (void) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_binlog_replay_end");
  assert (lua_gettop (luaState) == 1);

  int r = lua_pcall (luaState, 0, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

void lua_diff_end (void) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_get_difference_end");
  assert (lua_gettop (luaState) == 1);

  int r = lua_pcall (luaState, 0, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

void lua_our_id (int id) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_our_id");
  lua_pushnumber (luaState, id);
  assert (lua_gettop (luaState) == 2);

  int r = lua_pcall (luaState, 1, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

void lua_new_msg (struct tgl_message *M UU) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_msg_receive");
  push_message (M);
  assert (lua_gettop (luaState) == 2);

  int r = lua_pcall (luaState, 1, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

void lua_secret_chat_update (struct tgl_secret_chat *C, unsigned flags) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_secret_chat_update");
  push_peer (C->id, (void *)C);
  push_update_types (flags);
  assert (lua_gettop (luaState) == 3);

  int r = lua_pcall (luaState, 2, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

void lua_user_update (struct tgl_user *U, unsigned flags) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_user_update");
  push_peer (U->id, (void *)U);
  push_update_types (flags);
  assert (lua_gettop (luaState) == 3);

  int r = lua_pcall (luaState, 2, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

void lua_chat_update (struct tgl_chat *C, unsigned flags) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_chat_update");
  push_peer (C->id, (void *)C);
  push_update_types (flags);
  assert (lua_gettop (luaState) == 3);

  int r = lua_pcall (luaState, 2, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

//extern tgl_peer_t *Peers[];
//extern int peer_num;

#define MAX_LUA_COMMANDS 1000
void *lua_ptr[MAX_LUA_COMMANDS];
static int pos;

static inline tgl_peer_t *get_peer (const char *s) { 
  return tgl_peer_get_by_name (TLS, s);
}
  
enum lua_query_type {
  lq_contact_list,
  lq_dialog_list,
  lq_msg,
  lq_rename_chat,
  lq_send_photo,
  lq_chat_set_photo,
  lq_set_profile_photo,
  lq_set_profile_name,
  lq_send_video,
  lq_send_text,
  lq_fwd,
  lq_fwd_media,
  lq_load_photo,
  lq_load_video_thumb,
  lq_load_video,
  lq_chat_info,
  lq_user_info,
  lq_history,
  lq_chat_add_user,
  lq_chat_del_user,
  lq_add_contact,
  lq_del_contact,
  lq_rename_contact,
  lq_search,
  lq_global_search,
  lq_mark_read,
  lq_create_secret_chat,
  lq_create_group_chat,
  lq_send_audio,
  lq_send_document,
  lq_load_audio,
  lq_load_document,
  lq_load_document_thumb,
  lq_delete_msg,
  lq_restore_msg,
  lq_accept_secret_chat,
  lq_send_contact,
  lq_status_online,
  lq_status_offline,
  lq_send_location,
  lq_extf
};

struct lua_query_extra {
  int func;
  int param;
};

void lua_empty_cb (struct tgl_state *TLSR, void *cb_extra, int success) {
  assert (TLSR == TLS);
  struct lua_query_extra *cb = cb_extra;
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->param);

  lua_pushnumber (luaState, success);

  assert (lua_gettop (luaState) == 3);

  int r = lua_pcall (luaState, 2, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_contact_list_cb (struct tgl_state *TLSR, void *cb_extra, int success, int num, struct tgl_user **UL) {
  assert (TLSR == TLS);
  struct lua_query_extra *cb = cb_extra;
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->param);

  lua_pushnumber (luaState, success);

  if (success) {
    lua_newtable (luaState);
    int i;
    for (i = 0; i < num; i++) {
      lua_pushnumber (luaState, i);
      push_peer (UL[i]->id, (void *)UL[i]);
      lua_settable (luaState, -3);
    }
  } else {
    lua_pushboolean (luaState, 0);
  }

  assert (lua_gettop (luaState) == 4);

  int r = lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_dialog_list_cb (struct tgl_state *TLSR, void *cb_extra, int success, int num, tgl_peer_id_t peers[], int msgs[], int unread[]) {
  assert (TLSR == TLS);
  struct lua_query_extra *cb = cb_extra;
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->param);

  lua_pushnumber (luaState, success);
  if (success) {
    lua_newtable (luaState);
    int i;
    for (i = 0; i < num; i++) {
      lua_pushnumber (luaState, i);

      lua_newtable (luaState);

      lua_pushstring (luaState, "peer");
      push_peer (peers[i], tgl_peer_get (TLS, peers[i]));
      lua_settable (luaState, -3);

      struct tgl_message *M = tgl_message_get (TLS, msgs[i]);
      if (M && (M->flags & FLAG_CREATED)) {
        lua_pushstring (luaState, "message");
        push_message (M);
        lua_settable (luaState, -3);
      }

      lua_pushstring (luaState, "unread");
      lua_pushnumber (luaState, unread[i]);
      lua_settable (luaState, -3);

      lua_settable (luaState, -3);
    }
  } else {
    lua_pushboolean (luaState, 0);
  }
  assert (lua_gettop (luaState) == 4);


  int r = lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_msg_cb (struct tgl_state *TLSR, void *cb_extra, int success, struct tgl_message *M) {
  assert (TLSR == TLS);
  struct lua_query_extra *cb = cb_extra;
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->param);

  lua_pushnumber (luaState, success);

  if (success) {
    push_message (M);
  } else {
    lua_pushboolean (luaState, 0);
  }

  assert (lua_gettop (luaState) == 4);

  int r = lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_msg_list_cb (struct tgl_state *TLSR, void *cb_extra, int success, int num, struct tgl_message *M[]) {
  assert (TLSR == TLS);
  struct lua_query_extra *cb = cb_extra;
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->param);

  lua_pushnumber (luaState, success);

  if (success) {
    lua_newtable (luaState);
    int i;
    for (i = 0; i < num; i++) {
      lua_pushnumber (luaState, i);
      push_message (M[i]);
      lua_settable (luaState, -3);
    }
  } else {
    lua_pushboolean (luaState, 0);
  }

  assert (lua_gettop (luaState) == 4);

  int r = lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_file_cb (struct tgl_state *TLSR, void *cb_extra, int success, char *file_name) {
  assert (TLSR == TLS);
  struct lua_query_extra *cb = cb_extra;
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->param);

  lua_pushnumber (luaState, success);

  if (success) {
    lua_pushstring (luaState, file_name);
  } else {
    lua_pushboolean (luaState, 0);
  }

  assert (lua_gettop (luaState) == 4);

  int r = lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_chat_cb (struct tgl_state *TLSR, void *cb_extra, int success, struct tgl_chat *C) {
  assert (TLSR == TLS);
  struct lua_query_extra *cb = cb_extra;
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->param);

  lua_pushnumber (luaState, success);

  if (success) {
    push_peer (C->id, (void *)C);
  } else {
    lua_pushboolean (luaState, 0);
  }

  assert (lua_gettop (luaState) == 4);

  int r = lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_secret_chat_cb (struct tgl_state *TLSR, void *cb_extra, int success, struct tgl_secret_chat *C) {
  assert (TLSR == TLS);
  struct lua_query_extra *cb = cb_extra;
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->param);

  lua_pushnumber (luaState, success);

  if (success) {
    push_peer (C->id, (void *)C);
  } else {
    lua_pushboolean (luaState, 0);
  }

  assert (lua_gettop (luaState) == 4);

  int r = lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_user_cb (struct tgl_state *TLSR, void *cb_extra, int success, struct tgl_user *C) {
  assert (TLSR == TLS);
  struct lua_query_extra *cb = cb_extra;
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->param);

  lua_pushnumber (luaState, success);

  if (success) {
    push_peer (C->id, (void *)C);
  } else {
    lua_pushboolean (luaState, 0);
  }

  assert (lua_gettop (luaState) == 4);

  int r = lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_str_cb (struct tgl_state *TLSR, void *cb_extra, int success, char *data) {
  assert (TLSR == TLS);
  struct lua_query_extra *cb = cb_extra;
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->param);

  lua_pushnumber (luaState, success);

  if (success) {
    lua_pushstring (luaState, data);
  } else {
    lua_pushboolean (luaState, 0);
  }

  assert (lua_gettop (luaState) == 4);

  int r = lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_do_all (void) {
  int p = 0;
  while (p < pos) {
    int l = (long)lua_ptr[p ++];
    assert (p + l + 1 <= pos);
    enum lua_query_type f = (long)lua_ptr[p ++];
    struct tgl_message *M;
    char *s, *s1, *s2, *s3;
    switch (f) {
    case lq_contact_list:
      tgl_do_update_contact_list (TLS, lua_contact_list_cb, lua_ptr[p ++]);
      break;
    case lq_dialog_list:
      tgl_do_get_dialog_list (TLS, lua_dialog_list_cb, lua_ptr[p ++]);
      break;
    case lq_msg:
      tgl_do_send_message (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, lua_ptr[p + 2], strlen (lua_ptr[p + 2]), lua_msg_cb, lua_ptr[p]);
      free (lua_ptr[p + 2]);
      p += 3;
      break;
    case lq_rename_chat:
      tgl_do_rename_chat (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, lua_ptr[p + 2], lua_msg_cb, lua_ptr[p]);
      free (lua_ptr[p + 2]);
      p += 3;
      break;
    case lq_send_photo:
      tgl_do_send_photo (TLS, tgl_message_media_photo, ((tgl_peer_t *)lua_ptr[p + 1])->id, lua_ptr[p + 2], lua_msg_cb, lua_ptr[p]);
      free (lua_ptr[p + 2]);
      p += 3;
      break;
    case lq_send_video:
      tgl_do_send_photo (TLS, tgl_message_media_video, ((tgl_peer_t *)lua_ptr[p + 1])->id, lua_ptr[p + 2], lua_msg_cb, lua_ptr[p]);
      free (lua_ptr[p + 2]);
      p += 3;
      break;
    case lq_send_audio:
      tgl_do_send_photo (TLS, tgl_message_media_audio, ((tgl_peer_t *)lua_ptr[p + 1])->id, lua_ptr[p + 2], lua_msg_cb, lua_ptr[p]);
      free (lua_ptr[p + 2]);
      p += 3;
      break;
    case lq_send_document:
      tgl_do_send_photo (TLS, tgl_message_media_document, ((tgl_peer_t *)lua_ptr[p + 1])->id, lua_ptr[p + 2], lua_msg_cb, lua_ptr[p]);
      free (lua_ptr[p + 2]);
      p += 3;
      break;
    case lq_send_text:
      tgl_do_send_text (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, lua_ptr[p + 2], lua_msg_cb, lua_ptr[p]);
      free (lua_ptr[p + 2]);
      p += 3;
      break;
    case lq_chat_set_photo:
      tgl_do_set_chat_photo (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, lua_ptr[p + 2], lua_msg_cb, lua_ptr[p]);
      free (lua_ptr[p + 2]);
      p += 3;
      break;
    case lq_load_photo:
      M = lua_ptr[p + 1];
      if (!M || (M->media.type != tgl_message_media_photo && M->media.type != tgl_message_media_photo_encr)) {
        lua_file_cb (TLS, lua_ptr[p], 0, 0);
      } else {
        if (M->media.type == tgl_message_media_photo) {
          tgl_do_load_photo (TLS, &M->media.photo, lua_file_cb, lua_ptr[p]);
        } else {
          tgl_do_load_encr_video (TLS, &M->media.encr_video, lua_file_cb, lua_ptr[p]);
        }
      }
      p += 2;
      break;
    case lq_load_video:
      M = lua_ptr[p + 1];
      if (!M || (M->media.type != tgl_message_media_video && M->media.type != tgl_message_media_video_encr)) {
        lua_file_cb (TLS, lua_ptr[p], 0, 0);
      } else {
        if (M->media.type == tgl_message_media_video) {
          tgl_do_load_video (TLS, &M->media.video, lua_file_cb, lua_ptr[p]);
        } else {
          tgl_do_load_encr_video (TLS, &M->media.encr_video, lua_file_cb, lua_ptr[p]);
        }
      }
      p += 2;
      break;
    case lq_load_video_thumb:
      M = lua_ptr[p + 1];
      if (!M || (M->media.type != tgl_message_media_video)) {
        lua_file_cb (TLS, lua_ptr[p], 0, 0);
      } else {
        tgl_do_load_video_thumb (TLS, &M->media.video, lua_file_cb, lua_ptr[p]);
      }
      p += 2;
      break;
    case lq_load_audio:
      M = lua_ptr[p + 1];
      if (!M || (M->media.type != tgl_message_media_audio && M->media.type != tgl_message_media_audio_encr)) {
        lua_file_cb (TLS, lua_ptr[p], 0, 0);
      } else {
        if (M->media.type == tgl_message_media_audio) {
          tgl_do_load_audio (TLS, &M->media.audio, lua_file_cb, lua_ptr[p]);
        } else {
          tgl_do_load_encr_video (TLS, &M->media.encr_video, lua_file_cb, lua_ptr[p]);
        }
      }
      p += 2;
      break;
    case lq_load_document:
      M = lua_ptr[p + 1];
      if (!M || (M->media.type != tgl_message_media_document && M->media.type != tgl_message_media_document_encr)) {
        lua_file_cb (TLS, lua_ptr[p], 0, 0);
      } else {
        if (M->media.type == tgl_message_media_document) {
          tgl_do_load_document (TLS, &M->media.document, lua_file_cb, lua_ptr[p]);
        } else {
          tgl_do_load_encr_video (TLS, &M->media.encr_video, lua_file_cb, lua_ptr[p]);
        }
      }
      p += 2;
      break;
    case lq_load_document_thumb:
      M = lua_ptr[p + 1];
      if (!M || (M->media.type != tgl_message_media_document)) {
        lua_file_cb (TLS, lua_ptr[p], 0, 0);
      } else {
        tgl_do_load_document_thumb (TLS, &M->media.document, lua_file_cb, lua_ptr[p]);
      }
      p += 2;
      break;
    case lq_fwd:
      tgl_do_forward_message (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, ((struct tgl_message *)lua_ptr[p + 2])->id, lua_msg_cb, lua_ptr[p]);
      p += 3;
      break;
    case lq_fwd_media:
      tgl_do_forward_media (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, ((struct tgl_message *)lua_ptr[p + 2])->id, lua_msg_cb, lua_ptr[p]);
      p += 3;
      break;
    case lq_chat_info:
      tgl_do_get_chat_info (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, 0, lua_chat_cb, lua_ptr[p]);
      p += 2;
      break;
    case lq_user_info:
      tgl_do_get_user_info (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, 0, lua_user_cb, lua_ptr[p]);
      p += 2;
      break;
    case lq_history:
      tgl_do_get_history (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, (long)lua_ptr[p + 2], 0, lua_msg_list_cb, lua_ptr[p]);
      p += 3;
      break;
    case lq_chat_add_user:
      tgl_do_add_user_to_chat (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, ((tgl_peer_t *)lua_ptr[p + 2])->id, 10, lua_msg_cb, lua_ptr[p]);
      p += 3;
      break;
    case lq_chat_del_user:
      tgl_do_del_user_from_chat (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, ((tgl_peer_t *)lua_ptr[p + 2])->id, lua_msg_cb, lua_ptr[p]);
      p += 3;
      break;
    case lq_add_contact:
      s1 = lua_ptr[p + 1];
      s2 = lua_ptr[p + 2];
      s3 = lua_ptr[p + 3];
      tgl_do_add_contact (TLS, s1, strlen (s1), s2, strlen (s2), s3, strlen (s3), 0, lua_contact_list_cb, lua_ptr[p]);
      free (s1);
      free (s2);
      free (s3);
      p += 4;
      break;
    case lq_del_contact:
      tgl_do_del_contact (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, lua_empty_cb, lua_ptr[p]);
      p += 2;
      break;
    case lq_rename_contact:
      s1 = lua_ptr[p + 1];
      s2 = lua_ptr[p + 2];
      s3 = lua_ptr[p + 3];
      tgl_do_add_contact (TLS, s1, strlen (s1), s2, strlen (s2), s3, strlen (s3), 1, lua_contact_list_cb, lua_ptr[p]);
      free (s1);
      free (s2);
      free (s3);
      p += 4;
      break;
    case lq_search:
      s = lua_ptr[p + 2];
      tgl_do_msg_search (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, 0, 0, 40, 0, s, lua_msg_list_cb, lua_ptr[p]);
      free (s);
      p += 3;
      break;
    case lq_global_search:
      s = lua_ptr[p + 1];
      tgl_do_msg_search (TLS, tgl_set_peer_id (TGL_PEER_UNKNOWN, 0), 0, 0, 40, 0, s, lua_msg_list_cb, lua_ptr[p]);
      free (s);
      p += 2;
      break;
    case lq_mark_read:
      tgl_do_mark_read (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, lua_empty_cb, lua_ptr[p]);
      p += 2;
      break;
    case lq_set_profile_photo:
      s = lua_ptr[p + 1];
      tgl_do_set_profile_photo (TLS, s, lua_empty_cb, lua_ptr[p]);
      free (s);
      p += 2;
      break;
    case lq_set_profile_name:
      s1 = lua_ptr[p + 1];
      s2 = lua_ptr[p + 1];
      tgl_do_set_profile_name (TLS, s1, s2, lua_user_cb, lua_ptr[p]);
      free (s1);
      free (s2);
      p += 3;
      break;
    case lq_create_secret_chat:
      tgl_do_create_secret_chat (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, lua_secret_chat_cb, lua_ptr[p]);
      p += 2;
      break;
    case lq_create_group_chat:
      s = lua_ptr[p + 2];
      tgl_do_create_group_chat (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, s, lua_msg_cb, lua_ptr[p]);
      free (s);
      p += 3;
      break;
    case lq_delete_msg:
      tgl_do_delete_msg (TLS, ((struct tgl_message *)lua_ptr[p + 1])->id, lua_empty_cb, lua_ptr[p]);
      p += 2;
      break;
    case lq_restore_msg:
      tgl_do_delete_msg (TLS, (long)lua_ptr[p + 1], lua_empty_cb, lua_ptr[p]);
      p += 2;
      break;
    case lq_accept_secret_chat:
      tgl_do_accept_encr_chat_request (TLS, lua_ptr[p + 1], lua_secret_chat_cb, lua_ptr[p]);
      p += 2;
      break;
    case lq_send_contact:
      s1 = lua_ptr[p + 2];
      s2 = lua_ptr[p + 3];
      s3 = lua_ptr[p + 4];
      tgl_do_send_contact (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id, s1, strlen (s1), s2, strlen (s2), s3, strlen (s3), lua_msg_cb, lua_ptr[p]);
      free (s1);
      free (s2);
      free (s3);
      p += 5;
      break;
    case lq_status_online:
      tgl_do_update_status (TLS, 1, lua_empty_cb, lua_ptr[p]);
      p ++;
      break;
    case lq_status_offline:
      tgl_do_update_status (TLS, 0, lua_empty_cb, lua_ptr[p]);
      p ++;
      break;
    case lq_extf:
      s = lua_ptr[p + 1];
      tgl_do_send_extf (TLS, s, strlen (s), lua_str_cb, lua_ptr[p]);
      free (s);
      p += 2;
      break;
    case lq_send_location:
      if (sizeof (void *) == 4) {
        tgl_do_send_location (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id , *(float *)(lua_ptr + p + 2), *(float *)(lua_ptr + p + 3), lua_msg_cb, lua_ptr[p]);
      } else {
        tgl_do_send_location (TLS, ((tgl_peer_t *)lua_ptr[p + 1])->id , *(double *)(lua_ptr + p + 2), *(double *)(lua_ptr + p + 3), lua_msg_cb, lua_ptr[p]);
      }
      p += 4;
      break;
  /*
  lq_delete_msg,
  lq_restore_msg,
    case 0:
      tgl_do_send_message (((tgl_peer_t *)lua_ptr[p])->id, lua_ptr[p + 1], strlen (lua_ptr[p + 1]), 0, 0);
      free (lua_ptr[p + 1]);
      p += 2;
      break;
    case 1:
      tgl_do_forward_message (((tgl_peer_t *)lua_ptr[p])->id, (long)lua_ptr[p + 1], 0, 0);
      p += 2;
      break;
    case 2:
      tgl_do_mark_read (((tgl_peer_t *)lua_ptr[p])->id, 0, 0);
      p += 1;
      break;*/
    default:
      assert (0);
    }
  }
  pos = 0;
}


enum lua_function_param {
  lfp_none,
  lfp_peer,
  lfp_chat,
  lfp_user,
  lfp_secret_chat,
  lfp_string,
  lfp_number,
  lfp_positive_number,
  lfp_nonnegative_number,
  lfp_msg,
  lfp_double
};

struct lua_function {
  char *name;
  enum lua_query_type type;
  enum lua_function_param params[10];
};

struct lua_function functions[] = {
  {"get_contact_list", lq_contact_list, { lfp_none }},
  {"get_dialog_list", lq_dialog_list, { lfp_none }},
  {"rename_chat", lq_rename_chat, { lfp_chat, lfp_string, lfp_none }},
  {"send_msg", lq_msg, { lfp_peer, lfp_string, lfp_none }},
  {"send_photo", lq_send_photo, { lfp_peer, lfp_string, lfp_none }},
  {"send_video", lq_send_video, { lfp_peer, lfp_string, lfp_none }},
  {"send_audio", lq_send_audio, { lfp_peer, lfp_string, lfp_none }},
  {"send_document", lq_send_document, { lfp_peer, lfp_string, lfp_none }},
  {"send_text", lq_send_text, { lfp_peer, lfp_string, lfp_none }},
  {"chat_set_photo", lq_chat_set_photo, { lfp_chat, lfp_string, lfp_none }},
  {"load_photo", lq_load_photo, { lfp_msg, lfp_none }},
  {"load_video", lq_load_video, { lfp_msg, lfp_none }},
  {"load_video_thumb", lq_load_video_thumb, { lfp_msg, lfp_none }},
  {"load_audio", lq_load_audio, { lfp_msg, lfp_none }},
  {"load_document", lq_load_document, { lfp_msg, lfp_none }},
  {"load_document_thumb", lq_load_document_thumb, { lfp_msg, lfp_none }},
  {"fwd_msg", lq_fwd, { lfp_peer, lfp_msg, lfp_none }},
  {"fwd_media", lq_fwd_media, { lfp_peer, lfp_msg, lfp_none }},
  {"chat_info", lq_chat_info, { lfp_chat, lfp_none }},
  {"user_info", lq_user_info, { lfp_user, lfp_none }},
  {"get_history", lq_history, { lfp_peer, lfp_nonnegative_number, lfp_none }},
  {"chat_add_user", lq_chat_add_user, { lfp_chat, lfp_user, lfp_none }},
  {"chat_del_user", lq_chat_del_user, { lfp_chat, lfp_user, lfp_none }},
  {"add_contact", lq_add_contact, { lfp_string, lfp_string, lfp_string, lfp_none }},
  {"del_contact", lq_del_contact, { lfp_user, lfp_none }},
  {"rename_contact", lq_rename_contact, { lfp_string, lfp_string, lfp_string, lfp_none }},
  {"msg_search", lq_search, { lfp_peer, lfp_string, lfp_none }},
  {"msg_global_search", lq_global_search, { lfp_string, lfp_none }},
  {"mark_read", lq_mark_read, { lfp_peer, lfp_none }},
  {"set_profile_photo", lq_set_profile_photo, { lfp_string, lfp_none }},
  {"set_profile_name", lq_set_profile_name, { lfp_string, lfp_none }},
  {"create_secret_chat", lq_create_secret_chat, { lfp_user, lfp_none }},
  {"create_group_chat", lq_create_group_chat, { lfp_user, lfp_string, lfp_none }},
  {"delete_msg", lq_delete_msg, { lfp_msg, lfp_none }},
  {"restore_msg", lq_restore_msg, { lfp_positive_number, lfp_none }},
  {"accept_secret_chat", lq_accept_secret_chat, { lfp_secret_chat, lfp_none }},
  {"send_contact", lq_send_contact, { lfp_peer, lfp_string, lfp_string, lfp_string, lfp_none }},
  {"status_online", lq_status_online, { lfp_none }},
  {"status_offline", lq_status_offline, { lfp_none }},
  {"send_location", lq_send_location, { lfp_peer, lfp_double, lfp_double, lfp_none }},
  {"ext_function", lq_extf, { lfp_string, lfp_none }},
  { 0, 0, { lfp_none}}
};

static int parse_lua_function (lua_State *L, struct lua_function *F) {
  int p = 0;
  while (F->params[p] != lfp_none) { p ++; }
  if (lua_gettop (L) != p + 2) {
    lua_pushboolean (L, 0);
    return 1;
  }
  
  int a1 = luaL_ref (L, LUA_REGISTRYINDEX);
  int a2 = luaL_ref (L, LUA_REGISTRYINDEX);
  
  struct lua_query_extra *e = malloc (sizeof (*e));
  assert (e);
  e->func = a2;
  e->param = a1;

  assert (pos + 3 + p < MAX_LUA_COMMANDS);

  lua_ptr[pos ++] = (void *)(long)(p + 1);
  lua_ptr[pos ++] = (void *)(long)F->type;
  lua_ptr[pos ++] = e;

  int sp = p;
  int ok = 1;
  int cc = 0;
  while (p > 0) {
    p --;
    cc ++;
    const char *s;
    tgl_peer_t *P;
    long long num;
    double dval;
    struct tgl_message *M;
    switch (F->params[p]) {
    case lfp_none:
      assert (0);
      break;
    case lfp_peer:
    case lfp_user:
    case lfp_chat:
    case lfp_secret_chat:
      s = lua_tostring (L, -cc);
      if (!s) {
        ok = 0;
        break;
      }
      if (sscanf (s, "user#id%lld", &num) == 1 && num > 0) {
        tgl_insert_empty_user (TLS, num);
        P = tgl_peer_get (TLS, TGL_MK_USER (num));
      } else if (sscanf (s, "chat#id%lld", &num) == 1 && num > 0) {
        tgl_insert_empty_chat (TLS, num);
        P = tgl_peer_get (TLS, TGL_MK_CHAT (num));
      } else {
        P = get_peer (s);
      }
      if (!P/* || !(P->flags & FLAG_CREATED)*/) {
        ok = 0;
        break;
      }
      if (F->params[p] != lfp_peer) {
        if ((tgl_get_peer_type (P->id) == TGL_PEER_USER && F->params[p] != lfp_user) || 
            (tgl_get_peer_type (P->id) == TGL_PEER_CHAT && F->params[p] != lfp_chat) || 
            (tgl_get_peer_type (P->id) == TGL_PEER_ENCR_CHAT && F->params[p] != lfp_secret_chat)) {
          ok = 0;
          break;
        }
      }
      lua_ptr[pos + p] = P;
      break;

    case lfp_string:
      s = lua_tostring (L, -cc);
      if (!s) {
        ok = 0;
        break;
      }
      lua_ptr[pos + p] = (void *)s;
      break;

    case lfp_number:
      num = lua_tonumber (L, -cc);
      
      lua_ptr[pos + p] = (void *)(long)num;
      break;
    
    case lfp_double:
      dval  = lua_tonumber (L, -cc);
    
      if (sizeof (void *) == 4) {
        *(float *)(lua_ptr + pos + p) = dval;
      } else {
        assert (sizeof (void *) >= 8);
        *(double *)(lua_ptr + pos + p) = dval;
      }
      break;
    
    case lfp_positive_number:
      num = lua_tonumber (L, -cc);
      if (num <= 0) {
        ok = 0;
        break;
      }
      
      lua_ptr[pos + p] = (void *)(long)num;
      break;
    
    case lfp_nonnegative_number:
      num = lua_tonumber (L, -cc);
      if (num < 0) {
        ok = 0;
        break;
      }
      
      lua_ptr[pos + p] = (void *)(long)num;
      break;

    case lfp_msg:
      s = lua_tostring (L, -cc);
      if (!s || !strlen (s)) {
        ok = 0;
        break;
      }

      num = atoll (s);

      M = tgl_message_get (TLS, num);

      if (!M || !(M->flags & FLAG_CREATED)) {
        ok = 0;
        break;
      }
      
      lua_ptr[pos + p] = M;
      break;
    
    default:
      assert (0);
    }
  }
  if (!ok) {
    luaL_unref (luaState, LUA_REGISTRYINDEX, a1);
    luaL_unref (luaState, LUA_REGISTRYINDEX, a2);
    free (e);
    pos -= 3;
    lua_pushboolean (L, 0);
    return 1;
  }
  
  for (p = 0; p < sp; p++) {
    if (F->params[p] == lfp_string) {
      lua_ptr[pos + p] = strdup (lua_ptr[pos + p]);
    }
  }
  pos += p;

  lua_pushboolean (L, 1);
  return 1;
}


static void lua_postpone_alarm (evutil_socket_t fd, short what, void *arg) {
  int *t = arg;
  
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, t[1]);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, t[0]);
  assert (lua_gettop (luaState) == 2);
  
  int r = lua_pcall (luaState, 1, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, t[0]);
  luaL_unref (luaState, LUA_REGISTRYINDEX, t[1]);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

}

static int postpone_from_lua (lua_State *L) {
  int n = lua_gettop (L);
  if (n != 3) {
    lua_pushboolean (L, 0);
    return 1;
  }

  double timeout = lua_tonumber (L, -1);
  if (timeout < 0) {
    lua_pushboolean (L, 0);
    return 1;
  }

  lua_pop (L, 1);
  int a1 = luaL_ref (L, LUA_REGISTRYINDEX);
  int a2 = luaL_ref (L, LUA_REGISTRYINDEX);


  int *t = malloc (16);
  assert (t);
  struct event *ev = evtimer_new (TLS->ev_base, lua_postpone_alarm, t);
  t[0] = a1;
  t[1] = a2;
  *(void **)(t + 2) = ev;
  
  struct timeval ts= { timeout, 0};
  event_add (ev, &ts);
  
  lua_pushboolean (L, 1);
  return 1;
}

static int universal_from_lua (lua_State *L) {
  const char *s = lua_tostring(L, lua_upvalueindex(1));
  if (!s) {
    lua_pushboolean (L, 0);
    return 1;
  }
  int i = 0;
  while (functions[i].name) {
    if (!strcmp (functions[i].name, s)) {
      return parse_lua_function (L, &functions[i]);
    }
    i ++;
  }
  lua_pushboolean (L, 0);
  return 1;
}


static void my_lua_register (lua_State *L, const char *name, lua_CFunction f) {
  lua_pushstring(L, name);
  lua_pushcclosure(L, f, 1);
  lua_setglobal(L, name);
}


void lua_init (const char *file) {
  if (!file) { return; }
  have_file = 1;
  luaState = luaL_newstate ();
  luaL_openlibs (luaState);

  int i = 0;
  while (functions[i].name) {
    my_lua_register (luaState, functions[i].name, universal_from_lua);
    i ++;
  }
  //lua_register (luaState, "fwd_msg", fwd_msg_from_lua);
  //lua_register (luaState, "mark_read", mark_read_from_lua);
  lua_register (luaState, "postpone", postpone_from_lua);
  //lua_register (luaState, "get_contact_list", get_contacts_from_lua);
  /*lua_register (luaState, "get_dialog_list", get_dialog_list_from_lua);
  lua_register (luaState, "send_msg", send_msg_from_lua);
  lua_register (luaState, "rename_chat", rename_chat_from_lua);
  lua_register (luaState, "send_photo", send_photo_from_lua);
  lua_register (luaState, "send_video", send_video_from_lua);
  lua_register (luaState, "send_audio", send_audio_from_lua);
  lua_register (luaState, "send_document", send_document_from_lua);
  lua_register (luaState, "send_text", send_text_from_lua);
  lua_register (luaState, "chat_set_photo", chat_set_photo_from_lua);
  lua_register (luaState, "load_photo", load_photo_from_lua);
  lua_register (luaState, "load_video", load_video_from_lua);
  lua_register (luaState, "load_video_thumb", load_video_thumb_from_lua);
  lua_register (luaState, "load_audio", load_audio_from_lua);
  lua_register (luaState, "load_document", load_document_from_lua);
  lua_register (luaState, "load_document_thumb", load_document_thumb_from_lua);
  lua_register (luaState, "fwd_msg", message_forward_from_lua);
  lua_register (luaState, "chat_info", chat_info_from_lua);
  lua_register (luaState, "user_info", user_info_from_lua);
  lua_register (luaState, "get_history", get_history_from_lua);
  lua_register (luaState, "chat_add_user", chat_add_user_from_lua);
  lua_register (luaState, "chat_del_user", chat_del_user_from_lua);
  lua_register (luaState, "add_contact", add_contact_from_lua);
  lua_register (luaState, "rename_contact", rename_contact_from_lua);*/

  int ret = luaL_dofile (luaState, file);
  if (ret) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
    exit (1);
  }
}

#endif
