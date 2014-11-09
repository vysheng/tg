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

    Copyright Nikolay Durov, Andrey Lopatin 2012-2013
              Vitaly Valtman 2013-2014
*/
#ifndef __MTPROTO_COMMON_H__
#define __MTPROTO_COMMON_H__

#include <string.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <openssl/aes.h>
#include <stdio.h>
#include <assert.h>

//#include "interface.h"
#include "tools.h"
#include "auto/constants.h"

#include "tgl.h"
#include "tgl-inner.h"
/* DH key exchange protocol data structures */
#define	CODE_req_pq			0x60469778
#define CODE_resPQ			0x05162463
#define CODE_req_DH_params		0xd712e4be
#define CODE_p_q_inner_data		0x83c95aec
#define CODE_p_q_inner_data_temp		0x3c6a84d4
#define CODE_server_DH_inner_data	0xb5890dba
#define CODE_server_DH_params_fail	0x79cb045d
#define CODE_server_DH_params_ok	0xd0e8075c
#define CODE_set_client_DH_params	0xf5045f1f
#define CODE_client_DH_inner_data	0x6643b654
#define CODE_dh_gen_ok			0x3bcbf734
#define CODE_dh_gen_retry		0x46dc1fb9
#define CODE_dh_gen_fail		0xa69dae02 

#define CODE_bind_auth_key_inner 0x75a3f765

/* service messages */
#define CODE_rpc_result			0xf35c6d01
#define CODE_rpc_error			0x2144ca19
#define CODE_msg_container		0x73f1f8dc
#define CODE_msg_copy			0xe06046b2
#define CODE_msgs_ack			0x62d6b459
#define CODE_bad_msg_notification	0xa7eff811
#define	CODE_bad_server_salt		0xedab447b
#define CODE_msgs_state_req		0xda69fb52
#define CODE_msgs_state_info		0x04deb57d
#define CODE_msgs_all_info		0x8cc0d131
#define CODE_new_session_created	0x9ec20908
#define CODE_msg_resend_req		0x7d861a08
#define CODE_ping			0x7abe77ec
#define CODE_pong			0x347773c5
#define CODE_destroy_session		0xe7512126
#define CODE_destroy_session_ok		0xe22045fc
#define CODE_destroy_session_none      	0x62d350c9
#define CODE_destroy_sessions		0x9a6face8
#define CODE_destroy_sessions_res	0xa8164668
#define	CODE_get_future_salts		0xb921bd04
#define	CODE_future_salt		0x0949d9dc
#define	CODE_future_salts		0xae500895
#define	CODE_rpc_drop_answer		0x58e4a740
#define CODE_rpc_answer_unknown		0x5e2ad36e
#define CODE_rpc_answer_dropped_running	0xcd78e586
#define CODE_rpc_answer_dropped		0xa43ad8b7
#define	CODE_msg_detailed_info		0x276d3ec6
#define	CODE_msg_new_detailed_info	0x809db6df
#define CODE_ping_delay_disconnect	0xf3427b8c
#define CODE_gzip_packed 0x3072cfa1

#define CODE_input_peer_notify_settings_old 0x3cf4b1be
#define CODE_peer_notify_settings_old 0xddbcd4a5
#define CODE_user_profile_photo_old 0x990d1493
#define CODE_config_old 0x232d5905

#define CODE_msg_new_detailed_info 0x809db6df

#define CODE_msg_detailed_info 0x276d3ec6
/* not really a limit, for struct encrypted_message only */
// #define MAX_MESSAGE_INTS	16384
#define MAX_MESSAGE_INTS	1048576
#define MAX_PROTO_MESSAGE_INTS	1048576

#define PACKET_BUFFER_SIZE	(16384 * 100 + 16) // temp fix
#pragma pack(push,4)
struct encrypted_message {
  // unencrypted header
  long long auth_key_id;
  char msg_key[16];
  // encrypted part, starts with encrypted header
  long long server_salt;
  long long session_id;
  // long long auth_key_id2; // removed
  // first message follows
  long long msg_id;
  int seq_no;
  int msg_len;   // divisible by 4
  int message[MAX_MESSAGE_INTS];
};

#pragma pack(pop)

//BN_CTX *BN_ctx;

void tgl_prng_seed (struct tgl_state *TLS, const char *password_filename, int password_length);
int tgl_serialize_bignum (BIGNUM *b, char *buffer, int maxlen);
long long tgl_do_compute_rsa_key_fingerprint (RSA *key);

#define packet_buffer tgl_packet_buffer
#define packet_ptr tgl_packet_ptr

extern int *tgl_packet_buffer;
extern int *tgl_packet_ptr;

static inline void out_ints (const int *what, int len) {
  assert (packet_ptr + len <= packet_buffer + PACKET_BUFFER_SIZE);
  memcpy (packet_ptr, what, len * 4);
  packet_ptr += len;
}


static inline void out_int (int x) {
  assert (packet_ptr + 1 <= packet_buffer + PACKET_BUFFER_SIZE);
  *packet_ptr++ = x;
}


static inline void out_long (long long x) {
  assert (packet_ptr + 2 <= packet_buffer + PACKET_BUFFER_SIZE);
  *(long long *)packet_ptr = x;
  packet_ptr += 2;
}

static inline void out_double (double x) {
  assert (packet_ptr + 2 <= packet_buffer + PACKET_BUFFER_SIZE);
  *(double *)packet_ptr = x;
  packet_ptr += 2;
}

static inline void clear_packet (void) {
  packet_ptr = packet_buffer;
}

void tgl_out_cstring (const char *str, long len);
void tgl_out_cstring_careful (const char *str, long len);
void tgl_out_data (const void *data, long len);

#define out_cstring tgl_out_cstring
#define out_cstring_careful tgl_out_cstring_careful
#define out_data tgl_out_data

static inline void out_string (const char *str) {
  out_cstring (str, strlen (str));
}

static inline void out_bignum (BIGNUM *n) {
  int l = tgl_serialize_bignum (n, (char *)packet_ptr, (PACKET_BUFFER_SIZE - (packet_ptr - packet_buffer)) * 4);
  assert (l > 0);
  packet_ptr += l >> 2;
}

#define in_ptr tgl_in_ptr
#define in_end tgl_in_end
extern int *tgl_in_ptr, *tgl_in_end;


//void fetch_pts (void);
//void fetch_qts (void);
//void fetch_date (void);
//void fetch_seq (void);
static inline int prefetch_strlen (void) {
  if (in_ptr >= in_end) { 
    return -1; 
  }
  unsigned l = *in_ptr;
  if ((l & 0xff) < 0xfe) { 
    l &= 0xff;
    return (in_end >= in_ptr + (l >> 2) + 1) ? (int)l : -1;
  } else if ((l & 0xff) == 0xfe) {
    l >>= 8;
    return (l >= 254 && in_end >= in_ptr + ((l + 7) >> 2)) ? (int)l : -1;
  } else {
    return -1;
  }
}

static inline char *fetch_str (int len) {
  assert (len >= 0);
  if (len < 254) {
    char *str = (char *) in_ptr + 1;
    in_ptr += 1 + (len >> 2);
    return str;
  } else {
    char *str = (char *) in_ptr + 4;
    in_ptr += (len + 7) >> 2;
    return str;
  }
} 

static inline char *fetch_str_dup (void) {
  int l = prefetch_strlen ();
  assert (l >= 0);
  int i;
  char *s = fetch_str (l);
  for (i = 0; i < l; i++) {
    if (!s[i]) { break; }
  }
  char *r = talloc (i + 1);
  memcpy (r, s, i);
  r[i] = 0;
  return r;
}

static inline int fetch_update_str (char **s) {
  if (!*s) {
    *s = fetch_str_dup ();
    return 1;
  }
  int l = prefetch_strlen ();
  char *r = fetch_str (l);
  if (memcmp (*s, r, l) || (*s)[l]) {
    tfree_str (*s);
    *s = talloc (l + 1);
    memcpy (*s, r, l);
    (*s)[l] = 0;
    return 1;
  }
  return 0;
}

static inline int fetch_update_int (int *value) {
  if (*value == *in_ptr) {
    in_ptr ++;
    return 0;
  } else {
    *value = *(in_ptr ++);
    return 1;
  }
}

static inline int fetch_update_long (long long *value) {
  if (*value == *(long long *)in_ptr) {
    in_ptr += 2;
    return 0;
  } else {
    *value = *(long long *)(in_ptr);
    in_ptr += 2;
    return 1;
  }
}

static inline int set_update_int (int *value, int new_value) {
  if (*value == new_value) {
    return 0;
  } else {
    *value = new_value;
    return 1;
  }
}

static inline void fetch_skip (int n) {
  in_ptr += n;
  assert (in_ptr <= in_end);
}

static inline void fetch_skip_str (void) {
  int l = prefetch_strlen ();
  assert (l >= 0);
  fetch_str (l);
}

static inline long have_prefetch_ints (void) {
  return in_end - in_ptr;
}

int tgl_fetch_bignum (BIGNUM *x);
#define fetch_bignum tgl_fetch_bignum

static inline int fetch_int (void) {
  assert (in_ptr + 1 <= in_end);
  return *(in_ptr ++);
}

static inline int fetch_bool (void) {
  assert (in_ptr + 1 <= in_end);
  assert (*(in_ptr) == (int)CODE_bool_true || *(in_ptr) == (int)CODE_bool_false);
  return *(in_ptr ++) == (int)CODE_bool_true;
}

static inline int prefetch_int (void) {
  assert (in_ptr < in_end);
  return *(in_ptr);
}

static inline void prefetch_data (void *data, int size) {
  assert (in_ptr + (size >> 2) <= in_end);
  memcpy (data, in_ptr, size);
}

static inline void fetch_data (void *data, int size) {
  assert (in_ptr + (size >> 2) <= in_end);
  memcpy (data, in_ptr, size);
  assert (!(size & 3));
  in_ptr += (size >> 2);
}

static inline long long fetch_long (void) {
  assert (in_ptr + 2 <= in_end);
  long long r = *(long long *)in_ptr;
  in_ptr += 2;
  return r;
}

static inline double fetch_double (void) {
  assert (in_ptr + 2 <= in_end);
  double r = *(double *)in_ptr;
  in_ptr += 2;
  return r;
}

static inline void fetch_ints (void *data, int count) {
  assert (in_ptr + count <= in_end);
  memcpy (data, in_ptr, 4 * count);
  in_ptr += count;
}
    
static inline void fetch256 (void *buf) {
  int l = prefetch_strlen ();
  assert (l >= 0);
  char *s = fetch_str (l);
  if (l < 256) {
    memcpy (buf + 256 - l, s, l);
  } else {
    memcpy (buf, s + (l - 256), 256);
  }
}

static inline int in_remaining (void) {
  return 4 * (in_end - in_ptr);
}

//int get_random_bytes (unsigned char *buf, int n);

int tgl_pad_rsa_encrypt (struct tgl_state *TLS, char *from, int from_len, char *to, int size, BIGNUM *N, BIGNUM *E);
int tgl_pad_rsa_decrypt (struct tgl_state *TLS, char *from, int from_len, char *to, int size, BIGNUM *N, BIGNUM *D);

//extern long long rsa_encrypted_chunks, rsa_decrypted_chunks;

//extern unsigned char aes_key_raw[32], aes_iv[32];
//extern AES_KEY aes_key;

void tgl_init_aes_unauth (const char server_nonce[16], const char hidden_client_nonce[32], int encrypt);
void tgl_init_aes_auth (char auth_key[192], char msg_key[16], int encrypt);
int tgl_pad_aes_encrypt (char *from, int from_len, char *to, int size);
int tgl_pad_aes_decrypt (char *from, int from_len, char *to, int size);
/*
static inline void hexdump_in (void) {
  hexdump (in_ptr, in_end);
}

static inline void hexdump_out (void) {
  hexdump (packet_buffer, packet_ptr);
}*/

#ifdef __MACH__
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#endif
#endif
