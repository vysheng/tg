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

#include "mtproto-common.h"
#include <string.h>

static int cur_token_len;
static char *cur_token;
static int cur_token_real_len;
static int cur_token_quoted;
static int multiline_output;
static int multiline_offset;
static int multiline_offset_size = 2;

static int disable_field_names;

#define expect_token(token,len) \
  if (len != cur_token_len || memcmp (cur_token, token, cur_token_len)) { return -1; } \
  local_next_token ();

#define expect_token_ptr(token,len) \
  if (len != cur_token_len || memcmp (cur_token, token, cur_token_len)) { return 0; } \
  local_next_token ();

#define expect_token_autocomplete(token,len) \
  if (cur_token_len == -3 && len >= cur_token_real_len && !memcmp (cur_token, token, cur_token_real_len)) { set_autocomplete_string (token); return -1; }\
  if (len != cur_token_len || memcmp (cur_token, token, cur_token_len)) { return -1; } \
  local_next_token ();

#define expect_token_ptr_autocomplete(token,len) \
  if (cur_token_len == -3 && len >= cur_token_real_len && !memcmp (cur_token, token, cur_token_real_len)) { set_autocomplete_string (token); return 0; }\
  if (len != cur_token_len || memcmp (cur_token, token, cur_token_len)) { return 0; } \
  local_next_token ();


static int autocomplete_mode;
static char *autocomplete_string;
static int (*autocomplete_fun)(const char *, int, int, char **);

static void set_autocomplete_string (const char *s) {
  if (autocomplete_string) { free (autocomplete_string); }
  autocomplete_string = strdup (s);
  autocomplete_mode = 1;
}

static void set_autocomplete_type (int (*f)(const char *, int, int, char **)) {
  autocomplete_fun = f;
  autocomplete_mode = 2;
}

static int is_int (void) {
  if (cur_token_len <= 0) { return 0; }
  char c = cur_token[cur_token_len];
  cur_token[cur_token_len] = 0;
  char *p = 0;

  strtoll (cur_token, &p, 10);
  cur_token[cur_token_len] = c;

  return p == cur_token + cur_token_len;
}

static long long get_int (void) {
  if (cur_token_len <= 0) { return 0; }
  char c = cur_token[cur_token_len];
  cur_token[cur_token_len] = 0;
  char *p = 0;

  long long val = strtoll (cur_token, &p, 0);
  cur_token[cur_token_len] = c;

  return val;
}

static int is_double (void) {
  if (cur_token_len <= 0) { return 0; }
  char c = cur_token[cur_token_len];
  cur_token[cur_token_len] = 0;
  char *p = 0;

  strtod (cur_token, &p);
  cur_token[cur_token_len] = c;

  return p == cur_token + cur_token_len;
}

static double get_double (void) {
  if (cur_token_len <= 0) { return 0; }
  char c = cur_token[cur_token_len];
  cur_token[cur_token_len] = 0;
  char *p = 0;

  double val = strtod (cur_token, &p);
  cur_token[cur_token_len] = c;

  return val;
}

static struct paramed_type *paramed_type_dup (struct paramed_type *P) {
  if (ODDP (P)) { return P; }
  struct paramed_type *R = malloc (sizeof (*R));
  R->type = malloc (sizeof (*R->type));
  memcpy (R->type, P->type, sizeof (*P->type)); 
  R->type->id = strdup (P->type->id);

  if (P->type->params_num) {
    R->params = malloc (sizeof (void *) * P->type->params_num);
    int i;
    for (i = 0; i < P->type->params_num; i++) {
      R->params[i] = paramed_type_dup (P->params[i]);
    }
  }
  return R;
}

static void print_offset (void) {
  int i;
  for (i = 0; i < multiline_offset; i++) {
    printf (" ");
  }
}

static char *buffer_pos, *buffer_end;

static int is_wspc (char c) {
  return c <= 32 && c > 0;
}

static void skip_wspc (void) {
  while (buffer_pos < buffer_end && is_wspc (*buffer_pos)) {
    buffer_pos ++;
  }
}

static int is_letter (char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}


static void local_next_token (void) {
  skip_wspc ();
  cur_token_quoted = 0;
  if (buffer_pos >= buffer_end) {
    cur_token_len = -3;
    cur_token_real_len = 0;
    return;
  }
  char c = *buffer_pos;
  if (is_letter (c)) {
    cur_token = buffer_pos;
    while (buffer_pos < buffer_end && is_letter (*buffer_pos)) {
      buffer_pos ++;
    }
    if (buffer_pos < buffer_end) {
      cur_token_len = buffer_pos - cur_token;
    } else {
      cur_token_real_len = buffer_pos - cur_token;
      cur_token_len = -3;
    }
    return;
  } else if (c == '"') {
    cur_token_quoted = 1;
    cur_token = buffer_pos ++;
    while (buffer_pos < buffer_end && *buffer_pos != '"') {
      buffer_pos ++;
    }
    if (*buffer_pos == '"') {
      buffer_pos ++;
      cur_token_len = buffer_pos - cur_token - 2;
      cur_token ++;
    } else {
      cur_token_len = -2;
    }
    return;
  } else {
    if (c) {
      cur_token = buffer_pos ++;
      cur_token_len = 1;
    } else {
      cur_token_len = -3;
      cur_token_real_len = 0;
    }
  }
}

int tglf_extf_autocomplete (const char *text, int text_len, int index, char **R, char *data, int data_len) {
  if (index == -1) {
    buffer_pos = data;
    buffer_end = data + data_len;
    autocomplete_mode = 0;
    local_next_token ();
    autocomplete_function_any ();
  }
  if (autocomplete_mode == 0) { return -1; }
  int len = strlen (text);
  if (autocomplete_mode == 1) {
    if (index >= 0) { return -1; }
    index = 0;
    if (!strncmp (text, autocomplete_string, len)) {
      *R = strdup (autocomplete_string);
      return index;
    } else {
      return -1;
    }
  } else {
    return autocomplete_fun (text, len, index, R);
  }
}

struct paramed_type *tglf_extf_store (const char *data, int data_len) {
  buffer_pos = (char *)data;
  buffer_end = (char *)(data + data_len);
  local_next_token ();
  return store_function_any ();
}

#define OUT_BUF_SIZE (1 << 25)
static char out_buf[OUT_BUF_SIZE];
static int out_buf_pos;

#define eprintf(...) \
  do { \
    out_buf_pos += snprintf (out_buf + out_buf_pos, OUT_BUF_SIZE - out_buf_pos, __VA_ARGS__);\
    assert (out_buf_pos < OUT_BUF_SIZE);\
  } while (0)\

char *tglf_extf_fetch (struct paramed_type *T) {
  out_buf_pos = 0;
  fetch_type_any (T);
  return out_buf;
}
