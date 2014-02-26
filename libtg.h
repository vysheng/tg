#ifndef LIBTG_H
#define LIBTG_H

#include "tools.h"

typedef int (*fn_ask_username_callback)(void* context, char** username);
typedef int (*fn_ask_code_callback)(void* context, char** code);
typedef int (*fn_ask_code_register_callback)(void* context, char** code, char** first_name, char** last_name);
typedef void (*fn_connected_callback)(void* context);

struct configuration {
  void* context;
  int verbosity;
  fn_ask_username_callback pfn_ask_username;
  fn_ask_code_callback pfn_ask_code;
  fn_ask_code_register_callback pfn_ask_code_register;
  fn_connected_callback pfn_connected;
};

void initialize_lib_tg (struct configuration* config);

#endif // LIBTG_H
