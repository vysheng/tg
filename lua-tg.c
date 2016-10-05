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
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef USE_LUA
#include "lua-tg.h"

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
#include <tgl/tgl.h>
#include <tgl/tgl-queries.h>
#include "interface.h"

#include <assert.h>
extern int verbosity;
extern struct tgl_state *TLS;

static int have_file;

void print_start (void);
void print_end (void);

int ps_lua_pcall (lua_State *l, int a, int b, int c) {
  print_start ();
  int r = lua_pcall (l, a, b, c);
  print_end ();
  return r;
}

#define my_lua_checkstack(L,x) assert (lua_checkstack (L, x))
void push_user (tgl_peer_t *P);
void push_peer (tgl_peer_id_t id, tgl_peer_t *P);

void lua_add_string_field (const char *name, const char *value) {
  assert (name && strlen (name));
  if (!value || !strlen (value)) { return; }
  my_lua_checkstack (luaState, 3);
  lua_pushstring (luaState, name);
  lua_pushstring (luaState, value);
  lua_settable (luaState, -3);
}

void lua_add_lstring_field (const char *name, const char *value, int len) {
  assert (name && strlen (name));
  if (!value || !len) { return; }
  my_lua_checkstack (luaState, 3);
  lua_pushstring (luaState, name);
  lua_pushlstring (luaState, value, len);
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
  case TGL_PEER_CHANNEL:
    lua_pushstring (luaState, "channel");
    break;
  default:
    assert (0);
  }
}

void push_user (tgl_peer_t *P) {
  my_lua_checkstack (luaState, 4);
  lua_add_string_field ("first_name", P->user.first_name);
  lua_add_string_field ("last_name", P->user.last_name);
  lua_add_string_field ("real_first_name", P->user.real_first_name);
  lua_add_string_field ("real_last_name", P->user.real_last_name);
  lua_add_string_field ("phone", P->user.phone);
  lua_add_string_field ("username", P->user.username);
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

void push_channel (tgl_peer_t *P) {
  my_lua_checkstack (luaState, 4);
  lua_add_string_field ("title", P->channel.title);
  lua_add_string_field ("about", P->channel.about);
  lua_add_num_field ("participants_count", P->channel.participants_count);
  lua_add_num_field ("admins_count", P->channel.admins_count);
  lua_add_num_field ("kicked_count", P->channel.kicked_count);
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
  
  lua_add_string_field ("id", print_permanent_peer_id (P ? P->id : id));
  lua_pushstring (luaState, "peer_type");
  push_tgl_peer_type (tgl_get_peer_type (id));
  lua_settable (luaState, -3);
  lua_add_num_field ("peer_id", tgl_get_peer_id (id));

  if (!P || !(P->flags & TGLPF_CREATED)) {
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
    case TGL_PEER_CHANNEL:
      sprintf (s, "channel#%d", tgl_get_peer_id (id));
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
  case TGL_PEER_CHANNEL:
    push_channel (P);
    break;
  default:
    assert (0);
  }
}

void push_media (struct tgl_message_media *M) {
  my_lua_checkstack (luaState, 4);

  switch (M->type) {
  case tgl_message_media_photo:
    lua_newtable (luaState);
    lua_add_string_field ("type", "photo");
    lua_add_string_field ("caption", M->caption);
    break;
  case tgl_message_media_document:
  case tgl_message_media_audio:
  case tgl_message_media_video:
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
  case tgl_message_media_webpage:
    lua_newtable (luaState);
    lua_add_string_field ("type", "webpage");
    lua_add_string_field ("url", M->webpage->url);
    lua_add_string_field ("title", M->webpage->title);
    lua_add_string_field ("description", M->webpage->description);
    lua_add_string_field ("author", M->webpage->author);
    break;
  case tgl_message_media_venue:
    lua_newtable (luaState);
    lua_add_string_field ("type", "venue");
    lua_add_num_field ("longitude", M->venue.geo.longitude);
    lua_add_num_field ("latitude", M->venue.geo.latitude);
    lua_add_string_field ("title", M->venue.title);
    lua_add_string_field ("address", M->venue.address);
    lua_add_string_field ("provider", M->venue.provider);
    lua_add_string_field ("venue_id", M->venue.venue_id);
    break;
  default:
    lua_pushstring (luaState, "???");
  }
}

void push_service (struct tgl_message *M) {
  my_lua_checkstack (luaState, 4);
  switch (M->action.type) {
  case tgl_message_action_geo_chat_create:
    lua_newtable (luaState);
    lua_add_string_field ("type", "geo_created");
    break;
  case tgl_message_action_geo_chat_checkin:
    lua_newtable (luaState);
    lua_add_string_field ("type", "geo_checkin");
    break;
  case tgl_message_action_chat_create:
    lua_newtable (luaState);
    lua_add_string_field ("type", "chat_created");
    lua_add_string_field ("title", M->action.title);
    break;
  case tgl_message_action_chat_edit_title:
    lua_newtable (luaState);
    lua_add_string_field ("type", "chat_rename");
    lua_add_string_field ("title", M->action.title);
    break;
  case tgl_message_action_chat_edit_photo:
    lua_newtable (luaState);
    lua_add_string_field ("type", "chat_change_photo");
    break;
  case tgl_message_action_chat_delete_photo:
    lua_newtable (luaState);
    lua_add_string_field ("type", "chat_delete_photo");
    break;
  case tgl_message_action_chat_add_users:
    lua_newtable (luaState);
    lua_add_string_field ("type", "chat_add_user");
    
    lua_pushstring (luaState, "user");
    push_peer (tgl_set_peer_id (TGL_PEER_USER, M->action.users[0]), tgl_peer_get (TLS, tgl_set_peer_id (TGL_PEER_USER, M->action.users[0])));
    lua_settable (luaState, -3);
    break;
  case tgl_message_action_chat_add_user_by_link:
    lua_newtable (luaState);
    lua_add_string_field ("type", "chat_add_user_link");
    
    lua_pushstring (luaState, "link_issuer");
    push_peer (tgl_set_peer_id (TGL_PEER_USER, M->action.user), tgl_peer_get (TLS, tgl_set_peer_id (TGL_PEER_USER, M->action.user)));
    lua_settable (luaState, -3);
    break;
  case tgl_message_action_chat_delete_user:
    lua_newtable (luaState);
    lua_add_string_field ("type", "chat_del_user");
    
    lua_pushstring (luaState, "user");
    push_peer (tgl_set_peer_id (TGL_PEER_USER, M->action.user), tgl_peer_get (TLS, tgl_set_peer_id (TGL_PEER_USER, M->action.user)));
    lua_settable (luaState, -3);
    break;
  case tgl_message_action_set_message_ttl:
    lua_newtable (luaState);
    lua_add_string_field ("type", "set_ttl");
    lua_add_num_field ("ttl", M->action.ttl);
    break;
  case tgl_message_action_read_messages:
    lua_newtable (luaState);
    lua_add_string_field ("type", "read");
    lua_add_num_field ("count", M->action.read_cnt);
    break;
  case tgl_message_action_delete_messages:
    lua_newtable (luaState);
    lua_add_string_field ("type", "delete");
    lua_add_num_field ("count", M->action.delete_cnt);
    break;
  case tgl_message_action_screenshot_messages:
    lua_newtable (luaState);
    lua_add_string_field ("type", "screenshot");
    lua_add_num_field ("count", M->action.screenshot_cnt);
    break;
  case tgl_message_action_flush_history:
    lua_newtable (luaState);
    lua_add_string_field ("type", "flush");
    break;
  case tgl_message_action_resend:
    lua_newtable (luaState);
    lua_add_string_field ("type", "resend");
    break;
  case tgl_message_action_notify_layer:
    lua_newtable (luaState);
    lua_add_string_field ("type", "set_layer");
    lua_add_num_field ("layer", M->action.layer);
    break;
  case tgl_message_action_typing:    
    lua_newtable (luaState);
    lua_add_string_field ("type", "typing");
    break;
  case tgl_message_action_noop:
    lua_newtable (luaState);
    lua_add_string_field ("type", "nop");
    break;
  case tgl_message_action_request_key:
    lua_newtable (luaState);
    lua_add_string_field ("type", "request_rekey");
    break;
  case tgl_message_action_accept_key:
    lua_newtable (luaState);
    lua_add_string_field ("type", "accept_rekey");
    break;
  case tgl_message_action_commit_key:
    lua_newtable (luaState);
    lua_add_string_field ("type", "commit_rekey");
    break;
  case tgl_message_action_abort_key:
    lua_newtable (luaState);
    lua_add_string_field ("type", "abort_rekey");
    break;
  case tgl_message_action_channel_create:
    lua_newtable (luaState);
    lua_add_string_field ("type", "channel_created");
    lua_add_string_field ("title", M->action.title);
    break;
  case tgl_message_action_migrated_to:
    lua_newtable (luaState);
    lua_add_string_field ("type", "migrated_to");
    break;
  case tgl_message_action_migrated_from:
    lua_newtable (luaState);
    lua_add_string_field ("type", "migrated_from");
    break;
  default:
    lua_pushstring (luaState, "???");
    break;
  }
}

void push_message (struct tgl_message *M) {  
  assert (M);
  my_lua_checkstack (luaState, 10);
  lua_newtable (luaState);

  lua_add_string_field ("id", print_permanent_msg_id (M->permanent_id));
  if (!(M->flags & TGLMF_CREATED)) { return; }
  lua_add_num_field ("flags", M->flags);
 
  if (tgl_get_peer_type (M->fwd_from_id)) {
    lua_pushstring (luaState, "fwd_from");
    push_peer (M->fwd_from_id, tgl_peer_get (TLS, M->fwd_from_id));
    lua_settable (luaState, -3); // fwd_from

    lua_add_num_field ("fwd_date", M->fwd_date);
  }

  if (M->reply_id) {
    tgl_message_id_t msg_id = M->permanent_id;
    msg_id.id = M->reply_id;
    
    lua_add_string_field ("reply_id", print_permanent_msg_id (msg_id));
  }

  if (M->flags & TGLMF_MENTION) {
    lua_pushstring (luaState, "mention");
    lua_pushboolean (luaState, 1);
    lua_settable (luaState, -3); 
  }
 
  lua_pushstring (luaState, "from");
  push_peer (M->from_id, tgl_peer_get (TLS, M->from_id));
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "to");
  push_peer (M->to_id, tgl_peer_get (TLS, M->to_id));
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "out");
  lua_pushboolean (luaState, (M->flags & TGLMF_OUT) != 0);
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "unread");
  lua_pushboolean (luaState, (M->flags & TGLMF_UNREAD) != 0);
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "date");
  lua_pushnumber (luaState, M->date);
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "service");
  lua_pushboolean (luaState, (M->flags & TGLMF_SERVICE) != 0);
  lua_settable (luaState, -3); 

  if (!(M->flags & TGLMF_SERVICE)) {  
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
  } else {
    lua_pushstring (luaState, "action");
    push_service (M);
    lua_settable (luaState, -3); 
  }
}

void lua_binlog_end (void) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_binlog_replay_end");
  assert (lua_gettop (luaState) == 1);

  int r = ps_lua_pcall (luaState, 0, 0, 0);
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

  int r = ps_lua_pcall (luaState, 0, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

void lua_our_id (tgl_peer_id_t id) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_our_id");
  lua_pushnumber (luaState, tgl_get_peer_id (id));
  assert (lua_gettop (luaState) == 2);

  int r = ps_lua_pcall (luaState, 1, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

void lua_new_msg (struct tgl_message *M) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_msg_receive");
  push_message (M);
  assert (lua_gettop (luaState) == 2);

  int r = ps_lua_pcall (luaState, 1, 0, 0);
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

  int r = ps_lua_pcall (luaState, 2, 0, 0);
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

  int r = ps_lua_pcall (luaState, 2, 0, 0);
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

  int r = ps_lua_pcall (luaState, 2, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

//extern tgl_peer_t *Peers[];
//extern int peer_num;

#define MAX_LUA_COMMANDS 1000

struct lua_arg {
  int flags;
  union {
    tgl_message_id_t msg_id;
    tgl_peer_id_t peer_id;
    char *str;
    long long num;
    double dnum;
    void *ptr;
  };
};
struct lua_arg lua_ptr[MAX_LUA_COMMANDS];
static int pos;

enum lua_query_type {
  lq_contact_list,
  lq_dialog_list,
  lq_msg,
  lq_msg_channel,
  lq_send_typing,
  lq_send_typing_abort,
  lq_rename_chat,
  lq_send_photo,
  lq_chat_set_photo,
  lq_set_profile_photo,
  lq_set_profile_name,
  lq_send_video,
  lq_send_text,
  lq_reply,
  lq_fwd,
  lq_fwd_media,
  lq_load_photo,
  lq_load_video_thumb,
  lq_load_video,
  lq_chat_info,
  lq_channel_info,
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
  lq_send_file,
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
  lq_extf,
  lq_import_chat_link,
  lq_export_chat_link,
  lq_channel_invite_user,
  lq_channel_kick_user,
  lq_channel_get_admins,
  lq_channel_get_users
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

  int r = ps_lua_pcall (luaState, 2, 0, 0);

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

  int r = ps_lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_dialog_list_cb (struct tgl_state *TLSR, void *cb_extra, int success, int num, tgl_peer_id_t peers[], tgl_message_id_t *msgs[], int unread[]) {
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
      if (M && (M->flags & TGLMF_CREATED)) {
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


  int r = ps_lua_pcall (luaState, 3, 0, 0);

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

  if (success && M && (M->flags & TGLMF_CREATED)) {
    push_message (M);
  } else {
    lua_pushboolean (luaState, 0);
  }

  assert (lua_gettop (luaState) == 4);

  int r = ps_lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_one_msg_cb (struct tgl_state *TLSR, void *cb_extra, int success, int size, struct tgl_message *M[]) {
  assert (TLSR == TLS);
  struct lua_query_extra *cb = cb_extra;
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->param);

  lua_pushnumber (luaState, success);

  if (success && size > 0 && M[0] && (M[0]->flags & TGLMF_CREATED)) {
    push_message (M[0]);
  } else {
    lua_pushboolean (luaState, 0);
  }

  assert (lua_gettop (luaState) == 4);

  int r = ps_lua_pcall (luaState, 3, 0, 0);

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

  int r = ps_lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_file_cb (struct tgl_state *TLSR, void *cb_extra, int success, const char *file_name) {
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

  int r = ps_lua_pcall (luaState, 3, 0, 0);

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

  int r = ps_lua_pcall (luaState, 3, 0, 0);

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

  int r = ps_lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_channel_cb (struct tgl_state *TLSR, void *cb_extra, int success, struct tgl_channel *C) {
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

  int r = ps_lua_pcall (luaState, 3, 0, 0);

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

  int r = ps_lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_str_cb (struct tgl_state *TLSR, void *cb_extra, int success, const char *data) {
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

  int r = ps_lua_pcall (luaState, 3, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

#define LUA_STR_ARG(n) lua_ptr[n].str, strlen (lua_ptr[n].str)

void lua_do_all (void) {
  int p = 0;
  while (p < pos) {
    int l = lua_ptr[p ++].num;
    assert (p + l + 1 <= pos);
    enum lua_query_type f = lua_ptr[p ++].num;
    struct tgl_message *M;
    int q = p;
    tgl_message_id_t *tmp_msg_id;
    switch (f) {
    case lq_contact_list:
      tgl_do_update_contact_list (TLS, lua_contact_list_cb, lua_ptr[p ++].ptr);
      break;
    case lq_dialog_list:
      tgl_do_get_dialog_list (TLS, 100, 0, lua_dialog_list_cb, lua_ptr[p ++].ptr);
      break;
    case lq_msg:
      tgl_do_send_message (TLS, lua_ptr[p + 1].peer_id, LUA_STR_ARG (p + 2), 0, NULL, lua_msg_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_msg_channel:
      tgl_do_send_message (TLS, lua_ptr[p + 1].peer_id, LUA_STR_ARG (p + 2), 256, NULL, lua_msg_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_send_typing:
      tgl_do_send_typing (TLS, lua_ptr[p + 1].peer_id, tgl_typing_typing, lua_empty_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_send_typing_abort:
      tgl_do_send_typing (TLS, lua_ptr[p + 1].peer_id, tgl_typing_cancel, lua_empty_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_rename_chat:
      tgl_do_rename_chat (TLS, lua_ptr[p + 1].peer_id, LUA_STR_ARG (p + 2), lua_empty_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_send_photo:
      tgl_do_send_document (TLS, lua_ptr[p + 1].peer_id, lua_ptr[p + 2].str, NULL, 0, TGL_SEND_MSG_FLAG_DOCUMENT_PHOTO, lua_msg_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_send_video:
      tgl_do_send_document (TLS, lua_ptr[p + 1].peer_id, lua_ptr[p + 2].str, NULL, 0, TGL_SEND_MSG_FLAG_DOCUMENT_VIDEO, lua_msg_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_send_audio:
      tgl_do_send_document (TLS, lua_ptr[p + 1].peer_id, lua_ptr[p + 2].str, NULL, 0, TGL_SEND_MSG_FLAG_DOCUMENT_AUDIO, lua_msg_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_send_document:
      tgl_do_send_document (TLS, lua_ptr[p + 1].peer_id, lua_ptr[p + 2].str, NULL, 0, 0, lua_msg_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_send_file:
      tgl_do_send_document (TLS, lua_ptr[p + 1].peer_id, lua_ptr[p + 2].str, NULL, 0, TGL_SEND_MSG_FLAG_DOCUMENT_AUTO, lua_msg_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_send_text:
      tgl_do_send_text (TLS, lua_ptr[p + 1].peer_id, lua_ptr[p + 2].str, 0, lua_msg_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_chat_set_photo:
      tgl_do_set_chat_photo (TLS, lua_ptr[p + 1].peer_id, lua_ptr[p + 2].str, lua_empty_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_load_photo:
    case lq_load_video:
    case lq_load_audio:
    case lq_load_document:
      M = tgl_message_get (TLS, &lua_ptr[p + 1].msg_id);
      if (!M || (M->media.type != tgl_message_media_photo && M->media.type != tgl_message_media_document && M->media.type != tgl_message_media_document_encr)) {
        lua_file_cb (TLS, lua_ptr[p].ptr, 0, 0);
      } else {
        if (M->media.type == tgl_message_media_photo) {
          assert (M->media.photo);
          tgl_do_load_photo (TLS, M->media.photo, lua_file_cb, lua_ptr[p].ptr);
        } else if (M->media.type == tgl_message_media_document) {
          assert (M->media.document);
          tgl_do_load_document (TLS, M->media.document, lua_file_cb, lua_ptr[p].ptr);
        } else {
          tgl_do_load_encr_document (TLS, M->media.encr_document, lua_file_cb, lua_ptr[p].ptr);
        }
      }
      p += 2;
      break;
    case lq_load_video_thumb:
    case lq_load_document_thumb:
      M = tgl_message_get (TLS, &lua_ptr[p + 1].msg_id);
      if (!M || (M->media.type != tgl_message_media_document)) {
        lua_file_cb (TLS, lua_ptr[p].ptr, 0, 0);
      } else {
        tgl_do_load_document_thumb (TLS, M->media.document, lua_file_cb, lua_ptr[p].ptr);
      }
      p += 2;
      break;
    case lq_reply:
      tgl_do_reply_message (TLS, &lua_ptr[p + 1].msg_id, LUA_STR_ARG (p + 2), 0, lua_msg_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_fwd:
      tmp_msg_id = &lua_ptr[p + 2].msg_id;
      tgl_do_forward_messages (TLS, lua_ptr[p + 1].peer_id, 1, (void *)&tmp_msg_id, 0, lua_one_msg_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_fwd_media:
      tgl_do_forward_media (TLS, lua_ptr[p + 1].peer_id, &lua_ptr[p + 2].msg_id, 0, lua_msg_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_chat_info:
      tgl_do_get_chat_info (TLS, lua_ptr[p + 1].peer_id, 0, lua_chat_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_channel_info:
      tgl_do_get_channel_info (TLS, lua_ptr[p + 1].peer_id, 0, lua_channel_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_user_info:
      tgl_do_get_user_info (TLS, lua_ptr[p + 1].peer_id, 0, lua_user_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_history:
      tgl_do_get_history (TLS, lua_ptr[p + 1].peer_id, 0, lua_ptr[p + 2].num, 0, lua_msg_list_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_chat_add_user:
      tgl_do_add_user_to_chat (TLS, lua_ptr[p + 1].peer_id, lua_ptr[p + 2].peer_id, 10, lua_empty_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_chat_del_user:
      tgl_do_del_user_from_chat (TLS, lua_ptr[p + 1].peer_id, lua_ptr[p + 2].peer_id, lua_empty_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_add_contact:
      tgl_do_add_contact (TLS, LUA_STR_ARG (p + 1), LUA_STR_ARG (p + 2), LUA_STR_ARG (p + 3), 0, lua_contact_list_cb, lua_ptr[p].ptr);
      p += 4;
      break;
    case lq_del_contact:
      tgl_do_del_contact (TLS, lua_ptr[p + 1].peer_id, lua_empty_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_rename_contact:
      tgl_do_add_contact (TLS, LUA_STR_ARG (p + 1), LUA_STR_ARG (p + 2), LUA_STR_ARG (p + 3), 1, lua_contact_list_cb, lua_ptr[p].ptr);
      p += 4;
      break;
    case lq_search:
      tgl_do_msg_search (TLS, lua_ptr[p + 1].peer_id, 0, 0, 40, 0, LUA_STR_ARG (p + 2), lua_msg_list_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_global_search:
      tgl_do_msg_search (TLS, tgl_set_peer_id (TGL_PEER_UNKNOWN, 0), 0, 0, 40, 0, LUA_STR_ARG (p + 1), lua_msg_list_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_mark_read:
      tgl_do_mark_read (TLS, lua_ptr[p + 1].peer_id, lua_empty_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_set_profile_photo:
      tgl_do_set_profile_photo (TLS, lua_ptr[p + 1].str, lua_empty_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_set_profile_name:
      tgl_do_set_profile_name (TLS, LUA_STR_ARG (p + 1), LUA_STR_ARG (p + 2), lua_user_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_create_secret_chat:
      tgl_do_create_secret_chat (TLS, lua_ptr[p + 1].peer_id, lua_secret_chat_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_create_group_chat:
      tgl_do_create_group_chat (TLS, 1, &lua_ptr[p + 1].peer_id, LUA_STR_ARG (p + 2), lua_empty_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_delete_msg:
      tgl_do_delete_msg (TLS, &lua_ptr[p + 1].msg_id, lua_empty_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_accept_secret_chat:
      tgl_do_accept_encr_chat_request (TLS, (void *)tgl_peer_get (TLS, lua_ptr[p + 1].peer_id), lua_secret_chat_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_send_contact:
      tgl_do_send_contact (TLS, lua_ptr[p + 1].peer_id, LUA_STR_ARG (p + 2), LUA_STR_ARG (p + 3), LUA_STR_ARG (p + 4), 0, lua_msg_cb, lua_ptr[p].ptr);
      p += 5;
      break;
    case lq_status_online:
      tgl_do_update_status (TLS, 1, lua_empty_cb, lua_ptr[p].ptr);
      p ++;
      break;
    case lq_status_offline:
      tgl_do_update_status (TLS, 0, lua_empty_cb, lua_ptr[p].ptr);
      p ++;
      break;
    case lq_extf:
      tgl_do_send_extf (TLS, LUA_STR_ARG (p + 1), lua_str_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_import_chat_link:
      tgl_do_import_chat_link (TLS, LUA_STR_ARG (p + 1), lua_empty_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_export_chat_link:
      tgl_do_export_chat_link (TLS, lua_ptr[p + 1].peer_id, lua_str_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_send_location:
      tgl_do_send_location (TLS, lua_ptr[p + 1].peer_id, lua_ptr[p + 2].dnum, lua_ptr[p + 3].dnum, 0, lua_msg_cb, lua_ptr[p].ptr);
      p += 4;
      break;
    case lq_channel_invite_user:
      tgl_do_channel_invite_user (TLS, lua_ptr[p + 1].peer_id, lua_ptr[p + 2].peer_id, lua_empty_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_channel_kick_user:
      tgl_do_channel_kick_user (TLS, lua_ptr[p + 1].peer_id, lua_ptr[p + 2].peer_id, lua_empty_cb, lua_ptr[p].ptr);
      p += 3;
      break;
    case lq_channel_get_admins:
      tgl_do_channel_get_members (TLS, lua_ptr[p + 1].peer_id, 100, 0, 1, lua_contact_list_cb, lua_ptr[p].ptr);
      p += 2;
      break;
    case lq_channel_get_users:
      tgl_do_channel_get_members (TLS, lua_ptr[p + 1].peer_id, 100, 0, 0, lua_contact_list_cb, lua_ptr[p].ptr);
      p += 2;
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
    while (q < p) {
      if (lua_ptr[q].flags & 1) {
        tfree_str (lua_ptr[q].str);
      }
      q ++;
    }
  }
  pos = 0;
}


enum lua_function_param {
  lfp_none,
  lfp_peer,
  lfp_chat,
  lfp_channel,
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
  {"post_msg", lq_msg_channel, { lfp_channel, lfp_string, lfp_none }},
  {"send_typing", lq_send_typing, { lfp_peer, lfp_none }},
  {"send_typing_abort", lq_send_typing_abort, { lfp_peer, lfp_none }},
  {"send_photo", lq_send_photo, { lfp_peer, lfp_string, lfp_none }},
  {"send_video", lq_send_video, { lfp_peer, lfp_string, lfp_none }},
  {"send_audio", lq_send_audio, { lfp_peer, lfp_string, lfp_none }},
  {"send_document", lq_send_document, { lfp_peer, lfp_string, lfp_none }},
  {"send_file", lq_send_file, { lfp_peer, lfp_string, lfp_none }},
  {"send_text", lq_send_text, { lfp_peer, lfp_string, lfp_none }},
  {"chat_set_photo", lq_chat_set_photo, { lfp_chat, lfp_string, lfp_none }},
  {"load_photo", lq_load_photo, { lfp_msg, lfp_none }},
  {"load_video", lq_load_video, { lfp_msg, lfp_none }},
  {"load_video_thumb", lq_load_video_thumb, { lfp_msg, lfp_none }},
  {"load_audio", lq_load_audio, { lfp_msg, lfp_none }},
  {"load_document", lq_load_document, { lfp_msg, lfp_none }},
  {"load_document_thumb", lq_load_document_thumb, { lfp_msg, lfp_none }},
  {"reply_msg", lq_reply, { lfp_msg, lfp_string, lfp_none }},
  {"fwd_msg", lq_fwd, { lfp_peer, lfp_msg, lfp_none }},
  {"fwd_media", lq_fwd_media, { lfp_peer, lfp_msg, lfp_none }},
  {"chat_info", lq_chat_info, { lfp_chat, lfp_none }},
  {"channel_info", lq_channel_info, { lfp_channel, lfp_none }},
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
  {"import_chat_link", lq_import_chat_link, { lfp_string, lfp_none }},
  {"export_chat_link", lq_export_chat_link, { lfp_chat, lfp_none }},
  {"channel_invite_user", lq_channel_invite_user, { lfp_channel, lfp_user, lfp_none }},
  {"channel_kick_user", lq_channel_kick_user, { lfp_channel, lfp_user, lfp_none }},
  {"channel_get_admins", lq_channel_get_admins, { lfp_channel, lfp_none }},
  {"channel_get_users", lq_channel_get_users, { lfp_channel, lfp_none }},
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

  lua_ptr[pos ++].num = (p + 1);
  lua_ptr[pos ++].num = F->type;
  lua_ptr[pos ++].ptr = e;

  int sp = p;
  int ok = 1;
  int cc = 0;
  while (p > 0) {
    p --;
    cc ++;
    const char *s;
    long long num;
    double dval;
    tgl_peer_id_t peer_id;
    lua_ptr[pos + p].flags = 0;
    switch (F->params[p]) {
    case lfp_none:
      assert (0);
      break;
    case lfp_peer:
    case lfp_user:
    case lfp_chat:
    case lfp_channel:
    case lfp_secret_chat:
      s = lua_tostring (L, -cc);
      if (!s) {
        ok = 0;
        break;
      }
      
      if (F->params[p] == lfp_user) {
        peer_id = parse_input_peer_id (s, strlen (s), TGL_PEER_USER);
      } else if (F->params[p] == lfp_chat) {
        peer_id = parse_input_peer_id (s, strlen (s), TGL_PEER_CHAT);
      } else if (F->params[p] == lfp_secret_chat) {
        peer_id = parse_input_peer_id (s, strlen (s), TGL_PEER_ENCR_CHAT);
      } else if (F->params[p] == lfp_channel) {
        peer_id = parse_input_peer_id (s, strlen (s), TGL_PEER_CHANNEL);
      } else {
        peer_id = parse_input_peer_id (s, strlen (s), 0);
      }
      
      if (!peer_id.peer_type) {
        ok = 0;
        break;
      }
      
      lua_ptr[pos + p].peer_id = peer_id;
      break;

    case lfp_string:
      s = lua_tostring (L, -cc);
      if (!s) {
        ok = 0;
        break;
      }
      lua_ptr[pos + p].str = (void *)s;
      lua_ptr[pos + p].flags |= 1;
      break;

    case lfp_number:
      num = lua_tonumber (L, -cc);
      
      lua_ptr[pos + p].num = num;
      break;
    
    case lfp_double:
      dval = lua_tonumber (L, -cc);
      lua_ptr[pos + p].dnum = dval;
      break;

    case lfp_msg:
      s = lua_tostring (L, -cc);
      if (!s) {
        ok = 0;
        break;
      }
      lua_ptr[pos + p].msg_id = parse_input_msg_id (s, strlen (s));
      if (lua_ptr[pos + p].msg_id.peer_type == 0) {
        ok = 0;
        break;
      }
      break;
    
    case lfp_positive_number:
      num = lua_tonumber (L, -cc);
      if (num <= 0) {
        ok = 0;
        break;
      }
      
      lua_ptr[pos + p].num = num;
      break;
    
    case lfp_nonnegative_number:
      num = lua_tonumber (L, -cc);
      if (num < 0) {
        ok = 0;
        break;
      }
      
      lua_ptr[pos + p].num = num;
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
      lua_ptr[pos + p].str = tstrdup (lua_ptr[pos + p].str);
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
  
  int r = ps_lua_pcall (luaState, 1, 0, 0);

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
  
  struct timeval ts= {
    .tv_sec = (long)timeout,
    .tv_usec = (timeout - ((long)timeout)) * 1000000
  };
  event_add (ev, &ts);
  
  lua_pushboolean (L, 1);
  return 1;
}

extern int safe_quit;
static int safe_quit_from_lua (lua_State *L) {
  int n = lua_gettop (L);
  if (n != 0) {
    lua_pushboolean (L, 0);
    return 1;
  }
  safe_quit = 1;
  
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

enum command_argument {
  ca_none,
  ca_user,
  ca_chat,
  ca_secret_chat,
  ca_channel,
  ca_peer,
  ca_file_name,
  ca_file_name_end,
  ca_period,
  ca_number,
  ca_double,
  ca_string_end,
  ca_string,
  ca_modifier,
  ca_command,
  ca_extf,


  ca_optional = 256
};


struct arg {
  int flags;
  struct {
    tgl_peer_t *P;
    struct tgl_message *M;
    char *str;
    long long num;
    double dval;
  };
};

struct in_ev;
struct command {
  char *name;
  enum command_argument args[10];
  void (*fun)(struct command *command, int arg_num, struct arg args[], struct in_ev *ev);
  char *desc;
  void *arg;
};

#define NOT_FOUND (int)0x80000000

static void do_interface_from_lua (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  lua_settop (luaState, 0);
  my_lua_checkstack (luaState, 20);
  
  struct lua_query_extra *e = command->arg;  
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, e->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, e->param);
 
  int i;
  for (i = 0; i < arg_num; i ++) {
    int j = i;
    if (j > 9) { j = 9; }
    while (j >= 0) {
      if (command->args[j] == ca_period) { j --; continue; }
      if (command->args[j] == ca_none) { j --; continue; }
      break;
    }
    assert (j >= 0);

    switch (command->args[j] & 0xff) {
    case ca_none:
    case ca_period:
      assert (0);      
      break;
    case ca_user:
    case ca_chat:
    case ca_secret_chat:
    case ca_peer:
      if (args[i].P) {
        push_peer (args[i].P->id, args[i].P);
      } else {
        lua_pushnil (luaState);
      }
      break;
    case ca_file_name:
    case ca_file_name_end:
    case ca_string_end:
    case ca_string:
      if (args[i].str) {
        lua_pushstring (luaState, args[i].str);
      } else {
        lua_pushnil (luaState);
      }
      break;
    case ca_number:
      if (args[i].num != NOT_FOUND) {
        lua_pushnumber (luaState, args[i].num);
      } else {
        lua_pushnil (luaState);
      }
      break;
    case ca_double:
      if (args[i].dval != NOT_FOUND) {
        lua_pushnumber (luaState, args[i].dval);
      } else {
        lua_pushnil (luaState);
      }
      break;
    }
  }
  

  
  int r = ps_lua_pcall (luaState, 1 + arg_num, 0, 0);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

void register_new_command (struct command *cmd);
static int register_interface_from_lua (lua_State *L) {
  int n = lua_gettop (L);
  if (n <= 4 || n >= 13) {
    lua_pushboolean (L, 0);
    return 1;
  }

  static struct command cmd;
  memset (&cmd, 0, sizeof (struct command));

  int i;
  for (i = 0; i < n - 4; i++) {
    const char *s = lua_tostring (L, -1);
    lua_pop (L, 1);
    
    if (!s || !strlen (s)) {
      lua_pushboolean (L, 0);
      return 1;
    }

    int len = strlen (s);
    int optional = 0;
    if (len > 9 && !strcmp (s + len - 9, " optional")) {
      optional = ca_optional;
      len -= 9;
    }

    int ok = 0;
    #define VARIANT(name) \
      if (len == strlen (#name) && !strncmp (s, #name, len)) {\
        cmd.args[n - 5 - i] = ca_ ## name | optional; \
        ok = 1; \
      }

    VARIANT (user)
    VARIANT (chat)
    VARIANT (secret_chat)
    VARIANT (peer)
    VARIANT (file_name)
    VARIANT (file_name_end)
    VARIANT (period)
    VARIANT (number)
    VARIANT (double)
    VARIANT (string_end)
    VARIANT (string)
    
    #undef VARTIANT

    if (!ok) {
      lua_pushboolean (L, 0);
      return 1;
    }
  }
  
  const char *s = lua_tostring (L, -1);
  lua_pop (L, 1);
  
  cmd.desc = s ? tstrdup (s) : tstrdup ("no help provided");
  
  int a1 = luaL_ref (L, LUA_REGISTRYINDEX);
  int a2 = luaL_ref (L, LUA_REGISTRYINDEX);

  struct lua_query_extra *e = malloc (sizeof (*e));
  assert (e);
  e->func = a2;
  e->param = a1;

  cmd.arg = e;
    
  cmd.fun = do_interface_from_lua;
  
  s = lua_tostring (L, -1);
  lua_pop (L, 1);

  cmd.name = tstrdup (s ? s : "none");

  register_new_command (&cmd);

  lua_pushboolean (L, 1);
  return 1;
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
  
  lua_register (luaState, "postpone", postpone_from_lua);
  lua_register (luaState, "safe_quit", safe_quit_from_lua);
  lua_register (luaState, "register_interface_function", register_interface_from_lua);

  print_start ();
  int r = luaL_dofile (luaState, file);
  print_end ();

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
    exit (1);
  }
}

#endif
