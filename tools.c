/*
    This file is part of telegram-client.

    Telegram-client is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Telegram-client is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this telegram-client.  If not, see <http://www.gnu.org/licenses/>.

    Copyright Vitaly Valtman 2013
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/err.h>

#include "interface.h"
#include "tools.h"

static void out_of_memory (void) {
  logprintf ("Out of memory\n");
  assert (0 && "Out of memory");
}

void *trealloc (void *ptr, size_t size) {
  void *p = realloc (ptr, size);
  ensure_ptr (p);
  return p;
}

void *talloc (size_t size) {
  void *p = malloc (size);
  ensure_ptr (p);
  return p;
}

void *talloc0 (size_t size) {
  void *p = talloc (size);
  memset (p, 0, size);
  return p;
}

char *tstrdup (const char *s) {
  char *p = strdup (s);
  if (p == NULL) {
    out_of_memory ();
  }
  return p;
}

void ensure (int r) {
  if (!r) {
    logprintf ("Open SSL error\n");
    ERR_print_errors_fp (stderr);
    assert (0);
  }
}

void ensure_ptr (void *p) {
  if (p == NULL) {
    out_of_memory ();
  }
}
