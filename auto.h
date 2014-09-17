/* 
    This file is part of tgl-library

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

    Copyright Vitaly Valtman 2014
*/
#ifndef __AUTO_H__
#define __AUTO_H__

struct tl_type_descr {
  unsigned name;
  char *id;
  int params_num;
  long long params_types;
};

struct paramed_type {
  struct tl_type_descr *type;
  struct paramed_type **params;
};

#define NAME_ARRAY 0x89932ad9

#define TYPE_TO_PARAM(NAME) (&(struct paramed_type) {.type = &tl_type_## NAME, .params=0})
#define TYPE_TO_PARAM_1(NAME,PARAM1) (&(struct paramed_type) {.type = &tl_type_## NAME, .params=(struct paramed_type *[1]){PARAM1}})
#define ODDP(x) (((long)(x)) & 1)
#define EVENP(x) (!ODDP(x))
#define INT2PTR(x) (void *)(long)(((long)x) * 2 + 1)
#define PTR2INT(x) ((((long)x) - 1) / 2)

void tgl_paramed_type_free (struct paramed_type *P);

#include "auto/auto-header.h"

#endif
