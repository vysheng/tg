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

    Copyright Vitaly Valtman 2013-2014
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <zlib.h>
#include <time.h>
#include <sys/time.h>

//#include "interface.h"
#include "tools.h"

#define RES_PRE 8
#define RES_AFTER 8
#define MAX_BLOCKS 1000000
static void *blocks[MAX_BLOCKS];
static void *free_blocks[MAX_BLOCKS];
static int used_blocks;
static int free_blocks_cnt;

static long long total_allocated_bytes;

void logprintf (const char *format, ...) __attribute__ ((format (printf, 1, 2), weak));
void logprintf (const char *format, ...) {
  va_list ap;
  va_start (ap, format);
  vfprintf (stdout, format, ap);
  va_end (ap);
}

//extern int verbosity;

//static long long total_allocated_bytes;

int tgl_snprintf (char *buf, int len, const char *format, ...) {
  va_list ap;
  va_start (ap, format);
  int r = vsnprintf (buf, len, format, ap);
  va_end (ap);
  assert (r <= len && "tsnprintf buffer overflow");
  return r;
}

int tgl_asprintf (char **res, const char *format, ...) {
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

void tgl_free_debug (void *ptr, int size __attribute__ ((unused))) {
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
}

void tgl_free_release (void *ptr, int size) {
  free (ptr);
}



void *tgl_realloc_debug (void *ptr, size_t old_size __attribute__ ((unused)), size_t size) {
  void *p = talloc (size);
  memcpy (p, ptr, size >= old_size ? old_size : size); 
  tfree (ptr, old_size);
  return p;
}

void *tgl_realloc_release (void *ptr, size_t old_size __attribute__ ((unused)), size_t size) {
  void *p = realloc (ptr, size);
  ensure_ptr (p);
  return p;
}

void *tgl_alloc_debug (size_t size) {
  total_allocated_bytes += size;
  void *p = malloc (size + RES_PRE + RES_AFTER);
  ensure_ptr (p);
  *(int *)p = size ^ 0xbedabeda;
  *(int *)(p + 4) = size;
  *(int *)(p + RES_PRE + size) = size ^ 0x7bed7bed;
  *(int *)(p + RES_AFTER + 4 + size) = used_blocks;
  blocks[used_blocks ++] = p;

  if (used_blocks - 1 == 24867) {
    assert (0);
  }
  tcheck ();
  return p + 8;
}

void *tgl_alloc_release (size_t size) {
  void *p = malloc (size);
  ensure_ptr (p);
  return p;
}

void *tgl_alloc0 (size_t size) {
  void *p = talloc (size);
  memset (p, 0, size);
  return p;
}

char *tgl_strdup (const char *s) {
  int l = strlen (s);
  char *p = talloc (l + 1);
  memcpy (p, s, l + 1);
  return p;
}

char *tgl_strndup (const char *s, size_t n) {
  size_t l = 0;
  for (l = 0; l < n && s[l]; l++) { }
  char *p = talloc (l + 1);
  memcpy (p, s, l);
  p[l] = 0;
  return p;
}


int tgl_inflate (void *input, int ilen, void *output, int olen) {
  z_stream strm;
  memset (&strm, 0, sizeof (strm));
  assert (inflateInit2 (&strm, 16 + MAX_WBITS) == Z_OK);
  strm.avail_in = ilen;
  strm.next_in = input;
  strm.avail_out = olen ;
  strm.next_out = output;
  int err = inflate (&strm, Z_FINISH); 
  int total_out = strm.total_out;

  if (err != Z_OK && err != Z_STREAM_END) {
    logprintf ( "inflate error = %d\n", err);
    logprintf ( "inflated %d bytes\n", (int) strm.total_out);
    total_out = 0;
  }
  inflateEnd (&strm);
  return total_out;
}

void tgl_check_debug (void) {
  int i;
  for (i = 0; i < used_blocks; i++) {
    void *ptr = blocks[i];
    int size = (*(int *)ptr) ^ 0xbedabeda;
    if (!(*(int *)(ptr + 4) == size) || 
        !(*(int *)(ptr + RES_PRE + size) == (size ^ 0x7bed7bed)) ||
        !(*(int *)(ptr + RES_PRE + 4 + size) == i)) {
      logprintf ("Bad block at address %p (size %d, num %d)\n", ptr, size, i);
      assert (0 && "Bad block");
    }
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
  //logprintf ("ok. Used_blocks = %d. Free blocks = %d\n", used_blocks, free_blocks_cnt);
}

void tgl_exists_debug (void *ptr, int size) {
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
}

void tgl_exists_release (void *ptr, int size) {}
void tgl_check_release (void) {}

void tgl_my_clock_gettime (int clock_id, struct timespec *T) {
#ifdef __MACH__
  // We are ignoring MONOTONIC and hope time doesn't go back too often
  clock_serv_t cclock;
  mach_timespec_t mts;
  host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
  clock_get_time(cclock, &mts);
  mach_port_deallocate(mach_task_self(), cclock);
  T->tv_sec = mts.tv_sec;
  T->tv_nsec = mts.tv_nsec;
#else
  assert (clock_gettime(clock_id, T) >= 0);
#endif
}

double tglt_get_double_time (void) {
  struct timespec tv;
  tgl_my_clock_gettime (CLOCK_REALTIME, &tv);
  return tv.tv_sec + 1e-9 * tv.tv_nsec;
}

void tglt_secure_random (void *s, int l) {
  if (RAND_bytes (s, l) < 0) {
    /*if (allow_weak_random) {
      RAND_pseudo_bytes (s, l);
    } else {*/
      assert (0 && "End of random. If you want, you can start with -w");
    //}
  }
}

struct tgl_allocator tgl_allocator_debug = {
  .alloc = tgl_alloc_debug,
  .realloc = tgl_realloc_debug,
  .free = tgl_free_debug,
  .check = tgl_check_debug,
  .exists = tgl_exists_debug
};

struct tgl_allocator tgl_allocator_release = {
  .alloc = tgl_alloc_release,
  .realloc = tgl_realloc_release,
  .free = tgl_free_release,
  .check = tgl_check_release,
  .exists = tgl_exists_release
};

struct tgl_allocator *tgl_allocator = &tgl_allocator_release;
