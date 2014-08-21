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
#include <event2/event.h>
lua_State *luaState;

//#include "interface.h"
//#include "auto/constants.h"
#include "tgl.h"
#include "interface.h"

#include <assert.h>
extern int verbosity;

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
}

void push_encr_chat (tgl_peer_t *P) {
  my_lua_checkstack (luaState, 4);
  lua_pushstring (luaState, "user");
  push_peer (TGL_MK_USER (P->encr_chat.user_id), tgl_peer_get (TGL_MK_USER (P->encr_chat.user_id)));
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
    lua_pushstring (luaState, "photo");
    break;
  case tgl_message_media_video:
  case tgl_message_media_video_encr:
    break;
  case tgl_message_media_audio:
  case tgl_message_media_audio_encr:
    lua_pushstring (luaState, "audio");
    break;
  case tgl_message_media_document:
  case tgl_message_media_document_encr:
    lua_pushstring (luaState, "document");
    break;
  case tgl_message_media_unsupported:
    lua_pushstring (luaState, "unsupported");
    break;
  case tgl_message_media_geo:
    lua_newtable (luaState);
    lua_add_num_field ("longitude", M->geo.longitude);
    lua_add_num_field ("latitude", M->geo.latitude);
    break;
  case tgl_message_media_contact:
    lua_newtable (luaState);
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
    push_peer (M->fwd_from_id, tgl_peer_get (M->fwd_from_id));
    lua_settable (luaState, -3); // fwd_from

    lua_add_num_field ("fwd_date", M->fwd_date);
  }
  
  lua_pushstring (luaState, "from");
  push_peer (M->from_id, tgl_peer_get (M->from_id));
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "to");
  push_peer (M->to_id, tgl_peer_get (M->to_id));
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

static tgl_peer_t *get_peer (const char *s) { 
  return tgl_peer_get_by_name (s);
}

void lua_do_all (void) {
  int p = 0;
  while (p < pos) {
    int l = (long)lua_ptr[p ++];
    assert (p + l + 1 <= pos);
    int f = (long)lua_ptr[p ++];
    switch (f) {
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
      #ifdef DEBUG
        texists (lua_ptr[p], sizeof (tgl_peer_t));
      #endif
      tgl_do_mark_read (((tgl_peer_t *)lua_ptr[p])->id, 0, 0);
      p += 1;
      break;
    default:
      assert (0);
    }
  }
  pos = 0;
}


static int send_msg_from_lua (lua_State *L) {
  if (MAX_LUA_COMMANDS - pos < 4) {
    lua_pushboolean (L, 0);
    return 1;
  }
  int n = lua_gettop (L);
  if (n != 2) {
    lua_pushboolean (L, 0);
    return 1;
  }
  const char *s = lua_tostring (L, -2);
  if (!s) {
    lua_pushboolean (L, 0);
    return 1;
  }
  const char *msg = lua_tostring (L, -1);
  
  tgl_peer_t *P = get_peer (s);
  if (!P) {
    lua_pushboolean (L, 0);
    return 1;
  }
  
  lua_ptr[pos ++] = (void *)2l;
  lua_ptr[pos ++] = (void *)0l;
  lua_ptr[pos ++] = P;
  lua_ptr[pos ++] = strdup (msg);
  logprintf ("msg = %s\n", msg);
  
  lua_pushboolean (L, 1);
  return 1;
}

static int fwd_msg_from_lua (lua_State *L) {
  if (MAX_LUA_COMMANDS - pos < 4) {
    lua_pushboolean (L, 0);
    return 1;
  }
  int n = lua_gettop (L);
  if (n != 2) {
    lua_pushboolean (L, 0);
    return 1;
  }
  const char *s = lua_tostring (L, -2);
  long long num = atoll (lua_tostring (L, -1));
  if (!s) {
    lua_pushboolean (L, 0);
    return 1;
  }
  tgl_peer_t *P = get_peer (s);
  if (!P) {
    lua_pushboolean (L, 0);
    return 1;
  }
  
  lua_ptr[pos ++] = (void *)2l;
  lua_ptr[pos ++] = (void *)1l;
  lua_ptr[pos ++] = P;
  lua_ptr[pos ++] = (void *)(long)num;
  lua_pushboolean (L, 1);
  return 1;
}

static int mark_read_from_lua (lua_State *L) {
  if (MAX_LUA_COMMANDS - pos < 4) {
    lua_pushboolean (L, 0);
    return 1;
  }
  int n = lua_gettop (L);
  if (n != 1) {
    lua_pushboolean (L, 0);
    return 1;
  }
  const char *s = lua_tostring (L, -1);
  if (!s) {
    lua_pushboolean (L, 0);
    return 1;
  }
  tgl_peer_t *P = get_peer (s);
  if (!P) {
    lua_pushboolean (L, 0);
    return 1;
  }
  
  lua_ptr[pos ++] = (void *)1l;
  lua_ptr[pos ++] = (void *)2l;
  lua_ptr[pos ++] = P;
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
  struct event *ev = evtimer_new (tgl_state.ev_base, lua_postpone_alarm, t);
  t[0] = a1;
  t[1] = a2;
  *(void **)(t + 2) = ev;
  
  struct timeval ts= { timeout, 0};
  event_add (ev, &ts);
  
  lua_pushboolean (L, 1);
  return 1;
}

void lua_init (const char *file) {
  if (!file) { return; }
  have_file = 1;
  luaState = luaL_newstate ();
  luaL_openlibs (luaState);

  lua_register (luaState, "send_msg", send_msg_from_lua);
  lua_register (luaState, "fwd_msg", fwd_msg_from_lua);
  lua_register (luaState, "mark_read", mark_read_from_lua);
  lua_register (luaState, "postpone", postpone_from_lua);

  int ret = luaL_dofile (luaState, file);
  if (ret) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
    exit (1);
  }
}

#endif
