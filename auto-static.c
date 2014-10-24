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
#include "config.h"
#include <string.h>

#ifndef DISABLE_EXTF
static int cur_token_len;
static char *cur_token;
static int cur_token_real_len;
static int cur_token_quoted;
static int multiline_output = 1;
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
  assert (autocomplete_string);
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

  if (strtoll (cur_token, &p, 10)) {}
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

  if (strtod (cur_token, &p)) {}
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
  assert (R);
  R->type = malloc (sizeof (*R->type));
  assert (R->type);
  memcpy (R->type, P->type, sizeof (*P->type)); 
  R->type->id = strdup (P->type->id);
  assert (R->type->id);

  if (P->type->params_num) {
    R->params = malloc (sizeof (void *) * P->type->params_num);
    assert (R->params);
    int i;
    for (i = 0; i < P->type->params_num; i++) {
      R->params[i] = paramed_type_dup (P->params[i]);
    }
  }
  return R;
}

void tgl_paramed_type_free (struct paramed_type *P) {
  if (ODDP (P)) { return; }
  if (P->type->params_num) {
    int i;
    for (i = 0; i < P->type->params_num; i++) {
      tgl_paramed_type_free (P->params[i]);
    }
    free (P->params);
  }
  free (P->type->id);
  free (P->type);
  free (P);
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


static char exp_buffer[1 << 25];;
static int exp_buffer_pos;

static inline int is_hex (char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static inline int hex2dec (char c) {
  if (c >= '0' && c <= '9') { return c - '0'; }
  else { return c - 'a' + 10; }
}

static void expand_backslashed (char *s, int len) {
  int backslashed = 0;
  exp_buffer_pos = 0;
  int i = 0;
  while (i < len) {
    assert (i + 3 <= (1 << 25));
    if (backslashed) {
      backslashed = 0;
      switch (s[i ++]) {
      case 'n':
        exp_buffer[exp_buffer_pos ++] = '\n';
        break;
      case 'r':
        exp_buffer[exp_buffer_pos ++] = '\r';
        break;
      case 't':
        exp_buffer[exp_buffer_pos ++] = '\t';
        break;
      case 'b':
        exp_buffer[exp_buffer_pos ++] = '\b';
        break;
      case 'a':
        exp_buffer[exp_buffer_pos ++] = '\a';
        break;
      case '\\':
        exp_buffer[exp_buffer_pos ++] = '\\';
        break;
      case 'x':
        if (i + 2 > len || !is_hex (s[i]) || !is_hex (s[i + 1])) {
          exp_buffer_pos = -1;
          return;
        }
        exp_buffer[exp_buffer_pos ++] = hex2dec (s[i]) * 16 + hex2dec (s[i + 1]);
        i += 2;
        break;
      default:
        break;
      }
    } else {
      if (s[i] == '\\') { 
        backslashed = 1; 
        i ++;
      } else {
        exp_buffer[exp_buffer_pos ++] = s[i ++];
      }
    }
  }
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
    int backslashed = 0;
    while (buffer_pos < buffer_end && (*buffer_pos != '"' || backslashed)) {
      if (*buffer_pos == '\\') {
        backslashed ^= 1;
      } else {
        backslashed = 0;
      }
      buffer_pos ++;
    }
    if (*buffer_pos == '"') {
      buffer_pos ++;
      expand_backslashed (cur_token + 1, buffer_pos - cur_token - 2);
      if (exp_buffer_pos < 0) {
        cur_token_len = -2;
      } else {
        cur_token_len = exp_buffer_pos;
        cur_token = exp_buffer;
      }
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

#define MAX_FVARS 100
static struct paramed_type *fvars[MAX_FVARS];
static int fvars_pos;

static void add_var_to_be_freed (struct paramed_type *P) {
  assert (fvars_pos < MAX_FVARS);
  fvars[fvars_pos ++] = P;
}

static void free_vars_to_be_freed (void) {
  int i;
  for (i = 0; i < fvars_pos; i++) {
    tgl_paramed_type_free (fvars[i]);
  }
  fvars_pos = 0;
}

int tglf_extf_autocomplete (struct tgl_state *TLS, const char *text, int text_len, int index, char **R, char *data, int data_len) {
  if (index == -1) {
    buffer_pos = data;
    buffer_end = data + data_len;
    autocomplete_mode = 0;
    local_next_token ();
    struct paramed_type *P = autocomplete_function_any ();
    free_vars_to_be_freed ();
    if (P) { tgl_paramed_type_free (P); }
  }
  if (autocomplete_mode == 0) { return -1; }
  int len = strlen (text);
  if (autocomplete_mode == 1) {
    if (index >= 0) { return -1; }
    index = 0;
    if (!strncmp (text, autocomplete_string, len)) {
      *R = strdup (autocomplete_string);
      assert (*R);
      return index;
    } else {
      return -1;
    }
  } else {
    return autocomplete_fun (text, len, index, R);
  }
}

struct paramed_type *tglf_extf_store (struct tgl_state *TLS, const char *data, int data_len) { 
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

static int valid_utf8_char (const char *str) {
  unsigned char c = (unsigned char) *str;
  int n = 0;
 
  if ((c & 0x80) == 0x00) {
    n = 0;
  } else if ((c & 0xe0) == 0xc0) {
    n = 1;
  } else if ((c & 0xf0) == 0xe0) {
    n = 2;
  } else if ((c & 0xf8) == 0xf0) {
    n = 3;
  } else if ((c & 0xfc) == 0xf8) {
    n = 4;
  } else if ((c & 0xfe) == 0xfc) {
    n = 5;
  } else {
    return -1;
  }

  int i;
  for (i = 0; i < n; i ++) {
    if ((((unsigned char)(str[i + 1])) & 0xc0) != 0x80) {
      return -1;
    }
  }
  return n + 1;
}

static void print_escaped_string (const char *str, int len) {
  eprintf ("\"");
  const char *end = str + len;
  while (str < end) {
    int n = valid_utf8_char (str);
    if (n < 0) {
      eprintf ("\\x%02x", (int)(unsigned char)*str);
      str ++;
    } else if (n >= 2) {
      int i;
      for (i = 0; i < n; i++) {
        eprintf ("%c", *(str ++));
      }
    } else if (((unsigned char)*str) >= ' ' && *str != '"' && *str != '\\') {
      eprintf ("%c", *str);      
      str ++;
    } else {
      switch (*str) {
      case '\n':
        eprintf("\\n");
        break;
      case '\r':
        eprintf("\\r");
        break;
      case '\t':
        eprintf("\\t");
        break;
      case '\b':
        eprintf("\\b");
        break;
      case '\a':
        eprintf("\\a");
        break;
      case '\\':
        eprintf ("\\\\");
        break;
      case '"':
        eprintf ("\\\"");
        break;
      default:
        eprintf ("\\x%02x", (int)(unsigned char)*str);
        break;
      }
      str ++;
    }
  }
  eprintf ("\"");
}

static void print_offset (void) {
  int i;
  for (i = 0; i < multiline_offset; i++) {
    eprintf (" ");
  }
}

char *tglf_extf_fetch (struct tgl_state *TLS, struct paramed_type *T) {
  out_buf_pos = 0;
  if (fetch_type_any (T) < 0) { return 0; }
  return out_buf;
}
#endif
