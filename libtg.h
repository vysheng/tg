#ifndef LIBTG_H
#define LIBTG_H

#include "tools.h"
#include "queries.h"

typedef int (*fn_ask_username_callback)(void* context, char** username);
typedef int (*fn_ask_code_callback)(void* context, char** code);
typedef int (*fn_ask_code_register_callback)(void* context, char** code, char** first_name, char** last_name);
typedef void (*fn_connected_callback)(void* context);
typedef void (*fn_get_chats_callback)(void* context, peer_id_t id, peer_t *U);
typedef void (*fn_chat_info_callback)(void* context, struct chat* C);

struct libcfg {
  void* ctx;
  int verbosity;
  fn_ask_username_callback pfn_ask_username;
  fn_ask_code_callback pfn_ask_code;
  fn_ask_code_register_callback pfn_ask_code_register;
  fn_connected_callback pfn_connected;
  struct {
      void* object;
      fn_get_chats_callback function;
  } get_chats_callback;
  struct {
      void* object;
      fn_chat_info_callback function;
  } chat_info_callback;
};

void initialize_lib_tg (struct libcfg* config);

#endif // LIBTG_H
