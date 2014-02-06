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
lua_State *luaState;

#include "structures.h"
#include "interface.h"
#include "constants.h"
#include "tools.h"
#include "queries.h"
#include "net.h"

extern int verbosity;

static int have_file;

#define my_lua_checkstack(L,x) assert (lua_checkstack (L, x))
void push_user (peer_t *P UU);
void push_peer (peer_id_t id, peer_t *P);

void lua_add_string_field (const char *name, const char *value) {
  assert (name && strlen (name));
  if (!value || !strlen (value)) { return; }
  my_lua_checkstack (luaState, 3);
  lua_pushstring (luaState, name);
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

void push_peer_type (int x) {
  switch (x) {
  case PEER_USER:
    lua_pushstring (luaState, "user");
    break;
  case PEER_CHAT:
    lua_pushstring (luaState, "chat");
    break;
  case PEER_ENCR_CHAT:
    lua_pushstring (luaState, "encr_chat");
    break;
  default:
    assert (0);
  }
}

void push_user (peer_t *P UU) {
  my_lua_checkstack (luaState, 4);
  lua_add_string_field ("first_name", P->user.first_name);
  lua_add_string_field ("last_name", P->user.last_name);
  lua_add_string_field ("real_first_name", P->user.real_first_name);
  lua_add_string_field ("real_last_name", P->user.real_last_name);
  lua_add_string_field ("phone", P->user.phone);
}

void push_chat (peer_t *P) {
  my_lua_checkstack (luaState, 4);
  assert (P->chat.title);
  lua_add_string_field ("title", P->chat.title);
  lua_add_num_field ("members_num", P->chat.users_num);
}

void push_encr_chat (peer_t *P) {
  my_lua_checkstack (luaState, 4);
  lua_pushstring (luaState, "user");
  push_peer (MK_USER (P->encr_chat.user_id), user_chat_get (MK_USER (P->encr_chat.user_id)));
  lua_settable (luaState, -3);
}

void push_peer (peer_id_t id, peer_t *P) {
  lua_newtable (luaState);
 
  lua_add_num_field ("id", get_peer_id (id));
  lua_pushstring (luaState, "type");
  push_peer_type (get_peer_type (id));
  lua_settable (luaState, -3);


  if (!P || !(P->flags & FLAG_CREATED)) {
    lua_pushstring (luaState, "print_name"); 
    static char s[100];
    switch (get_peer_type (id)) {
    case PEER_USER:
      sprintf (s, "user#%d", get_peer_id (id));
      break;
    case PEER_CHAT:
      sprintf (s, "chat#%d", get_peer_id (id));
      break;
    case PEER_ENCR_CHAT:
      sprintf (s, "encr_chat#%d", get_peer_id (id));
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
  
  switch (get_peer_type (id)) {
  case PEER_USER:
    push_user (P);
    break;
  case PEER_CHAT:
    push_chat (P);
    break;
  case PEER_ENCR_CHAT:
    push_encr_chat (P);
    break;
  default:
    assert (0);
  }
}

void push_media (struct message_media *M) {
  my_lua_checkstack (luaState, 4);

  switch (M->type) {
  case CODE_message_media_photo:
  case CODE_decrypted_message_media_photo:
    lua_pushstring (luaState, "photo");
    break;
  case CODE_message_media_video:
  case CODE_decrypted_message_media_video:
    lua_pushstring (luaState, "video");
    break;
  case CODE_message_media_audio:
  case CODE_decrypted_message_media_audio:
    lua_pushstring (luaState, "audio");
    break;
  case CODE_message_media_document:
  case CODE_decrypted_message_media_document:
    lua_pushstring (luaState, "document");
    break;
  case CODE_message_media_unsupported:
    lua_pushstring (luaState, "unsupported");
    break;
  case CODE_message_media_geo:
    lua_newtable (luaState);
    lua_add_num_field ("longitude", M->geo.longitude);
    lua_add_num_field ("latitude", M->geo.latitude);
    break;
  case CODE_message_media_contact:
  case CODE_decrypted_message_media_contact:
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

void push_message (struct message *M) {
  assert (M);
  my_lua_checkstack (luaState, 10);
  lua_newtable (luaState);

  static char s[30];
  tsnprintf (s, 30, "%lld", M->id);
  lua_add_string_field ("id", s);
  lua_add_num_field ("flags", M->flags);
  
  if (get_peer_type (M->fwd_from_id)) {
    lua_pushstring (luaState, "fwd_from");
    push_peer (M->fwd_from_id, user_chat_get (M->fwd_from_id));
    lua_settable (luaState, -3); // fwd_from

    lua_add_num_field ("fwd_date", M->fwd_date);
  }
  
  lua_pushstring (luaState, "from");
  push_peer (M->from_id, user_chat_get (M->from_id));
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "to");
  push_peer (M->to_id, user_chat_get (M->to_id));
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
    if (M->media.type  && M->media.type != CODE_message_media_empty && M->media.type != CODE_decrypted_message_media_empty) {
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

void lua_new_msg (struct message *M UU) {
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

void lua_secret_chat_created (struct secret_chat *C) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_secret_chat_created");
  push_peer (C->id, (void *)C);
  assert (lua_gettop (luaState) == 2);

  int r = lua_pcall (luaState, 1, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

void lua_user_update (struct user *U) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_user_update");
  push_peer (U->id, (void *)U);
  assert (lua_gettop (luaState) == 2);

  int r = lua_pcall (luaState, 1, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

void lua_chat_update (struct chat *C) {
  if (!have_file) { return; }
  lua_settop (luaState, 0);
  //lua_checkstack (luaState, 20);
  my_lua_checkstack (luaState, 20);
  lua_getglobal (luaState, "on_chat_update");
  push_peer (C->id, (void *)C);
  assert (lua_gettop (luaState) == 2);

  int r = lua_pcall (luaState, 1, 0, 0);
  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

extern peer_t *Peers[];
extern int peer_num;

#define MAX_LUA_COMMANDS 1000
void *lua_ptr[MAX_LUA_COMMANDS];
static int pos;

static peer_t *get_peer (const char *s) { 
  int index = 0;
  while (index < peer_num && (!Peers[index]->print_name || strcmp (Peers[index]->print_name, s))) {
    index ++;
  }
  return index == peer_num ? 0 : Peers[index];
}

void lua_do_all (void) {
  int p = 0;
  while (p < pos) {
    int l = (long)lua_ptr[p ++];
    assert (p + l + 1 <= pos);
    int f = (long)lua_ptr[p ++];
    switch (f) {
    case 0:
      do_send_message (((peer_t *)lua_ptr[p])->id, lua_ptr[p + 1], strlen (lua_ptr[p + 1]));
      tfree_str (lua_ptr[p + 1]);
      p += 2;
      break;
    case 1:
      do_forward_message (((peer_t *)lua_ptr[p])->id, (long)lua_ptr[p + 1]);
      p += 2;
      break;
    case 2:
      #ifdef DEBUG
        texists (lua_ptr[p], sizeof (peer_t));
      #endif
      do_mark_read (((peer_t *)lua_ptr[p])->id);
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
  
  peer_t *P = get_peer (s);
  if (!P) {
    lua_pushboolean (L, 0);
    return 1;
  }
  
  lua_ptr[pos ++] = (void *)2l;
  lua_ptr[pos ++] = (void *)0l;
  lua_ptr[pos ++] = P;
  lua_ptr[pos ++] = tstrdup (msg);
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
  peer_t *P = get_peer (s);
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
  peer_t *P = get_peer (s);
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

int lua_postpone_alarm (void *self) {
  int *t = self;
  
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
  tfree (*(void **)(t + 2), sizeof (struct event_timer));
  tfree (t, 16);
  return 0;
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

  struct event_timer *ev = talloc (sizeof (*ev));
  int *t = talloc (16);
  t[0] = a1;
  t[1] = a2;
  *(void **)(t + 2) = ev;
  
  ev->timeout = get_double_time () + timeout;
  ev->alarm = (void *)lua_postpone_alarm;
  ev->self = t;
  insert_event_timer (ev);
  
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
