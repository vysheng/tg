#ifndef __LUA_TG_H__
#define __LUA_TG_H__

#include <string.h>
#include "structures.h"

void lua_init (const char *file);
void lua_new_msg (struct message *M);
void lua_our_id (int id);
void lua_secret_chat_created (struct secret_chat *U);
void lua_user_update (struct user *U);
void lua_chat_update (struct chat *C);
void lua_binlog_end (void);
void lua_diff_end (void);
void lua_do_all (void);
#define lua_secret_chat_update(x)
#endif
