#ifndef __LUA_TG_H__
#define __LUA_TG_H__

#include <string.h>
#include "lua-tg.h"
#include "tgl.h"

void lua_init (const char *file);
void lua_new_msg (struct tgl_message *M);
void lua_our_id (int id);
void lua_secret_chat_update (struct tgl_secret_chat *U, unsigned flags);
void lua_user_update (struct tgl_user *U, unsigned flags);
void lua_chat_update (struct tgl_chat *C, unsigned flags);
void lua_binlog_end (void);
void lua_diff_end (void);
void lua_do_all (void);
#endif
