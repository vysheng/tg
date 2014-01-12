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
#include <zlib.h>

#include "interface.h"
#include "tools.h"

#ifdef DEBUG
#define MAX_BLOCKS 1000000
void *blocks[MAX_BLOCKS];
void *free_blocks[MAX_BLOCKS];
int used_blocks;
int free_blocks_cnt;
#endif

#ifdef DEBUG
#define RES_PRE 8
#define RES_AFTER 8
#endif

extern int verbosity;

long long total_allocated_bytes;

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

void print_backtrace (void);
void tfree (void *ptr, int size __attribute__ ((unused))) {
#ifdef DEBUG
  total_allocated_bytes -= size;
  ptr -= RES_PRE;
  if (size != (int)((*(int *)ptr) ^ 0xbedabeda)) {
    logprintf ("size = %d, ptr = %d\n", size, (*(int *)ptr) ^ 0xbedabeda);
  }
  assert (*(int *)ptr == (int)((size) ^ 0xbedabeda));
  assert (*(int *)(ptr + RES_PRE + size) == (int)((size) ^ 0x7bed7bed));
  assert (*(int *)(ptr + 4) == size);
  int block_num = *(int *)(ptr + 4 + RES_PRE + size);
  if (block_num >= used_blocks) {
    logprintf ("block_num = %d, used = %d\n", block_num, used_blocks);
  }
  assert (block_num < used_blocks);
  if (block_num < used_blocks - 1) {
    void *p = blocks[used_blocks - 1];
    int s = (*(int *)p) ^ 0xbedabeda;
    *(int *)(p + 4 + RES_PRE + s) = block_num;
    blocks[block_num] = p;
  }
  blocks[--used_blocks] = 0;
  memset (ptr, 0, size + RES_PRE + RES_AFTER);
  *(int *)ptr = size + 12;
  free_blocks[free_blocks_cnt ++] = ptr;
#else
  free (ptr);
#endif
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
  total_allocated_bytes += size;
  void *p = malloc (size + RES_PRE + RES_AFTER);
  ensure_ptr (p);
  *(int *)p = size ^ 0xbedabeda;
  *(int *)(p + 4) = size;
  *(int *)(p + RES_PRE + size) = size ^ 0x7bed7bed;
  *(int *)(p + RES_AFTER + 4 + size) = used_blocks;
  blocks[used_blocks ++] = p;
  return p + 8;
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

int tinflate (void *input, int ilen, void *output, int olen) {
  z_stream strm;
  memset (&strm, 0, sizeof (strm));
  assert (inflateInit2 (&strm, 16 + MAX_WBITS) == Z_OK);
  strm.avail_in = ilen;
  strm.next_in = input;
  strm.avail_out = olen ;
  strm.next_out = output;
  int err = inflate (&strm, Z_FINISH), total_out = 0;
  if (err == Z_OK || err == Z_STREAM_END) {
    total_out = (int) strm.total_out;
    if (err == Z_STREAM_END && verbosity >= 2) {
      logprintf ( "inflated %d bytes\n", (int) strm.total_out);
    }
  }
  if (verbosity && err != Z_STREAM_END) {
    logprintf ( "inflate error = %d\n", err);
    logprintf ( "inflated %d bytes\n", (int) strm.total_out);
  }
  inflateEnd (&strm);
  return total_out;
}

#ifdef DEBUG
void tcheck (void) {
  int i;
  for (i = 0; i < used_blocks; i++) {
    void *ptr = blocks[i];
    int size = (*(int *)ptr) ^ 0xbedabeda;
    assert (*(int *)(ptr + 4) == size);
    assert (*(int *)(ptr + RES_PRE + size) == (size ^ 0x7bed7bed));
    assert (*(int *)(ptr + RES_PRE + 4 + size) == i);
  }
  for (i = 0; i < free_blocks_cnt; i++) {
    void *ptr = free_blocks[i];
    int l = *(int *)ptr;
    int j = 0;
    for (j = 0; j < l; j++) {
      if (*(char *)(ptr + 4 + j)) {
        hexdump (ptr + 8, ptr + 8 + l + ((-l) & 3)); 
        logprintf ("Used freed memory size = %d. ptr = %p\n", l + 4 - RES_PRE - RES_AFTER, ptr);
        assert (0);
      }
    }
  }
  logprintf ("ok. Used_blocks = %d. Free blocks = %d\n", used_blocks, free_blocks_cnt);
}
#endif
