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

#define _GNU_SOURCE

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

int tsnprintf (char *buf, int len, const char *format, ...) {
  va_list ap;
  va_start (ap, format);
  int r = vsnprintf (buf, len, format, ap);
  va_end (ap);
  assert (r <= len && "tsnprintf buffer overflow");
  return r;
}

int tasprintf (char **res, const char *format, ...) {
  va_list ap;
  va_start (ap, format);
  int r = vasprintf (res, format, ap);
  assert (r >= 0);
  va_end (ap);
  void *rs = talloc (strlen (*res) + 1);
  memcpy (rs, *res, strlen (*res) + 1);
  free (*res);
  *res = rs;
  return r;
}

void tfree (void *ptr, int size __attribute__ ((unused))) {
#ifdef DEBUG
  ptr -= 4;
  assert (*(int *)ptr == (int)((size) ^ 0xbedabeda));
  assert (*(int *)(ptr + 4 + size) == (int)((size) ^ 0x0bed0bed));
  memset (ptr, 0, size + 8);
#endif
  free (ptr);
}

void tfree_str (void *ptr) {
  if (!ptr) { return; }
  tfree (ptr, strlen (ptr) + 1);
}

void tfree_secure (void *ptr, int size) {
  memset (ptr, 0, size);
  tfree (ptr, size);
}

void *trealloc (void *ptr, size_t old_size __attribute__ ((unused)), size_t size) {
#ifdef DEBUG
  void *p = talloc (size);
  memcpy (p, ptr, size >= old_size ? old_size : size); 
  tfree (ptr, old_size);
  return p;
#else
  void *p = realloc (ptr, size);
  ensure_ptr (p);
  return p;
#endif
}

void *talloc (size_t size) {
#ifdef DEBUG
  void *p = malloc (size + 8);
  ensure_ptr (p);
  *(int *)p = size ^ 0xbedabeda;
  *(int *)(p + 4 + size) = size ^ 0x0bed0bed;
  return p + 4;
#else
  void *p = malloc (size);
  ensure_ptr (p);
  return p;
#endif
}

void *talloc0 (size_t size) {
  void *p = talloc (size);
  memset (p, 0, size);
  return p;
}

char *tstrdup (const char *s) {
#ifdef DEBUG
  int l = strlen (s);
  char *p = talloc (l + 1);
  memcpy (p, s, l + 1);
  return p;
#else
  char *p = strdup (s);
  if (p == NULL) {
    out_of_memory ();
  }
  return p;
#endif
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
