#ifndef __AUTO_H__
#define __AUTO_H__

struct tl_type {
  unsigned name;
  char *id;
};

struct paramed_type {
  struct tl_type *type;
  struct paramed_type **params;
};

#define NAME_ARRAY 0x89932ad9

#define TYPE_TO_PARAM(NAME) (&(struct paramed_type) {.type = &tl_type_## NAME, .params=0})
#define ODDP(x) (((long)(x)) & 1)
#define EVENP(x) (!ODDP(x))
#define INT2PTR(x) (void *)(long)(((long)x) * 2 + 1)
#define PTR2INT(x) ((((long)x) - 1) / 2)

#include "auto-header.h"

#endif
