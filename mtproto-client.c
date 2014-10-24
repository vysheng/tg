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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define        _FILE_OFFSET_BITS        64

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#if defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/endian.h>
#endif
#include <sys/types.h>
#include <netdb.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>

//#include "telegram.h"
#include "include.h"
#include "queries.h"
//#include "loop.h"
#include "structures.h"
#include "binlog.h"
#include "auto.h"
#include "tgl.h"
#include "mtproto-client.h"
#include "tools.h"
#include "tree.h"
#include "updates.h"

#if defined(__FreeBSD__)
#define __builtin_bswap32(x) bswap32(x)
#endif

#if defined(__OpenBSD__)
#define __builtin_bswap32(x) __swap32gen(x)
#endif

#define sha1 SHA1

#include "mtproto-common.h"

#define MAX_NET_RES        (1L << 16)
//extern int log_level;

#if !defined(HAVE___BUILTIN_BSWAP32) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
static inline unsigned __builtin_bswap32(unsigned x) {
  return ((x << 24) & 0xff000000 ) |
  ((x << 8) & 0x00ff0000 ) |
  ((x >> 8) & 0x0000ff00 ) |
  ((x >> 24) & 0x000000ff );
}
#endif

//int verbosity;
//static enum tgl_dc_state c_state;
//extern int binlog_enabled;
//extern int disable_auto_accept;
//extern int allow_weak_random;

static int total_packets_sent;
static long long total_data_sent;


static int rpc_execute (struct tgl_state *TLS, struct connection *c, int op, int len);
static int rpc_becomes_ready (struct tgl_state *TLS, struct connection *c);
static int rpc_close (struct tgl_state *TLS, struct connection *c);

static long long precise_time;

static double get_utime (int clock_id) {
  struct timespec T;
  tgl_my_clock_gettime (clock_id, &T);
  double res = T.tv_sec + (double) T.tv_nsec * 1e-9;
  if (clock_id == CLOCK_REALTIME) {
    precise_time = (long long) (res * (1LL << 32));
  }
  return res;
}


//#define STATS_BUFF_SIZE        (64 << 10)
//static int stats_buff_len;
//static char stats_buff[STATS_BUFF_SIZE];

#define MAX_RESPONSE_SIZE        (1L << 24)

//static int Response_len;

/*
 *
 *                STATE MACHINE
 *
 */

#define TG_SERVER_PUBKEY_FILENAME     "tg-server.pub"
//static char *rsa_public_key_name; // = TG_SERVER_PUBKEY_FILENAME;
//static RSA *pubKey;
static long long pk_fingerprint;

static int rsa_load_public_key (struct tgl_state *TLS, const char *public_key_name) {
  TLS->pubKey = NULL;
  FILE *f = fopen (public_key_name, "r");
  if (f == NULL) {
    vlogprintf (E_WARNING, "Couldn't open public key file: %s\n", public_key_name);
    return -1;
  }
  TLS->pubKey = PEM_read_RSAPublicKey (f, NULL, NULL, NULL);
  fclose (f);
  if (TLS->pubKey == NULL) {
    vlogprintf (E_WARNING, "PEM_read_RSAPublicKey returns NULL.\n");
    return -1;
  }

  vlogprintf (E_WARNING, "public key '%s' loaded successfully\n", public_key_name);

  return 0;
}





/*
 *
 *        UNAUTHORIZED (DH KEY EXCHANGE) PROTOCOL PART
 *
 */

static BIGNUM dh_prime, dh_g, g_a, auth_key_num;
static char s_power [256];

static struct {
  long long auth_key_id;
  long long out_msg_id;
  int msg_len;
} unenc_msg_header;


#define ENCRYPT_BUFFER_INTS        16384
static int encrypt_buffer[ENCRYPT_BUFFER_INTS];

#define DECRYPT_BUFFER_INTS        16384
static int decrypt_buffer[ENCRYPT_BUFFER_INTS];

static int encrypt_packet_buffer (struct tgl_state *TLS) {
  return tgl_pad_rsa_encrypt (TLS, (char *) packet_buffer, (packet_ptr - packet_buffer) * 4, (char *) encrypt_buffer, ENCRYPT_BUFFER_INTS * 4, ((RSA *)TLS->pubKey)->n, ((RSA *)TLS->pubKey)->e);
}

static int encrypt_packet_buffer_aes_unauth (const char server_nonce[16], const char hidden_client_nonce[32]) {
  tgl_init_aes_unauth (server_nonce, hidden_client_nonce, AES_ENCRYPT);
  return tgl_pad_aes_encrypt ((char *) packet_buffer, (packet_ptr - packet_buffer) * 4, (char *) encrypt_buffer, ENCRYPT_BUFFER_INTS * 4);
}


static int rpc_send_packet (struct tgl_state *TLS, struct connection *c) {
  int len = (packet_ptr - packet_buffer) * 4;
  //c->out_packet_num ++;
  TLS->net_methods->incr_out_packet_num (c);
  long long next_msg_id = (long long) ((1LL << 32) * get_utime (CLOCK_REALTIME)) & -4;
  if (next_msg_id <= unenc_msg_header.out_msg_id) {
    unenc_msg_header.out_msg_id += 4;
  } else {
    unenc_msg_header.out_msg_id = next_msg_id;
  }
  unenc_msg_header.msg_len = len;

  int total_len = len + 20;
  assert (total_len > 0 && !(total_len & 0xfc000003));
  total_len >>= 2;
  vlogprintf (E_DEBUG, "writing packet: total_len = %d, len = %d\n", total_len, len);
  if (total_len < 0x7f) {
    assert (TLS->net_methods->write_out (c, &total_len, 1) == 1);
  } else {
    total_len = (total_len << 8) | 0x7f;
    assert (TLS->net_methods->write_out (c, &total_len, 4) == 4);
  }
  TLS->net_methods->write_out (c, &unenc_msg_header, 20);
  TLS->net_methods->write_out (c, packet_buffer, len);
  TLS->net_methods->flush_out (c);

  total_packets_sent ++;
  total_data_sent += total_len;
  return 1;
}

static int rpc_send_message (struct tgl_state *TLS, struct connection *c, void *data, int len) {
  assert (len > 0 && !(len & 0xfc000003));
  int total_len = len >> 2;
  if (total_len < 0x7f) {
    assert (TLS->net_methods->write_out (c, &total_len, 1) == 1);
  } else {
    total_len = (total_len << 8) | 0x7f;
    assert (TLS->net_methods->write_out (c, &total_len, 4) == 4);
  }
  
  TLS->net_methods->incr_out_packet_num (c);
  assert (TLS->net_methods->write_out (c, data, len) == len);
  TLS->net_methods->flush_out (c);

  total_packets_sent ++;
  total_data_sent += total_len;
  return 1;
}

static int send_req_pq_packet (struct tgl_state *TLS, struct connection *c) {
  struct tgl_dc *D = TLS->net_methods->get_dc (c);
  assert (D->state == st_init);

  tglt_secure_random (D->nonce, 16);
  unenc_msg_header.out_msg_id = 0;
  clear_packet ();
  out_int (CODE_req_pq);
  out_ints ((int *)D->nonce, 4);
  rpc_send_packet (TLS, c);    
  
  D->state = st_reqpq_sent;
  return 1;
}

static int send_req_pq_temp_packet (struct tgl_state *TLS, struct connection *c) {
  struct tgl_dc *D = TLS->net_methods->get_dc (c);
  assert (D->state == st_authorized);

  tglt_secure_random (D->nonce, 16);
  unenc_msg_header.out_msg_id = 0;
  clear_packet ();
  out_int (CODE_req_pq);
  out_ints ((int *)D->nonce, 4);
  rpc_send_packet (TLS, c);    
  
  D->state = st_reqpq_sent_temp;
  return 1;
}


static unsigned long long gcd (unsigned long long a, unsigned long long b) {
  return b ? gcd (b, a % b) : a;
}

//typedef unsigned int uint128_t __attribute__ ((mode(TI)));

static int process_respq_answer (struct tgl_state *TLS, struct connection *c, char *packet, int len, int temp_key) {
  struct tgl_dc *D = TLS->net_methods->get_dc (c);
  unsigned long long what;
  unsigned p1, p2;
  int i;
  
  long long packet_auth_key_id = *(long long *)packet;
  if (packet_auth_key_id) {
    assert (temp_key);
    vlogprintf (E_WARNING, "received packet during creation of temp auth key. Probably answer on old query. Drop\n");
    return 0;
  }
  vlogprintf (E_DEBUG, "process_respq_answer(), len=%d, op=0x%08x\n", len, *(int *)(packet + 20));
  assert (len >= 76);
  assert (!*(long long *) packet);
  assert (*(int *) (packet + 16) == len - 20);
  assert (!(len & 3));
  assert (*(int *) (packet + 20) == CODE_resPQ);
  assert (!memcmp (packet + 24, D->nonce, 16));
  memcpy (D->server_nonce, packet + 40, 16);
  char *from = packet + 56;
  int clen = *from++;
  assert (clen <= 8);
  what = 0;
  for (i = 0; i < clen; i++) {
    what = (what << 8) + (unsigned char)*from++;
  }

  while (((unsigned long)from) & 3) ++from;

  p1 = 0, p2 = 0;

  int it = 0;
  unsigned long long g = 0;
  for (i = 0; i < 3 || it < 1000; i++) {
    int q = ((lrand48() & 15) + 17) % what;
    unsigned long long x = (long long)lrand48 () % (what - 1) + 1, y = x;
    int lim = 1 << (i + 18);
    int j;
    for (j = 1; j < lim; j++) {
      ++it;
      unsigned long long a = x, b = x, c = q;
      while (b) {
        if (b & 1) {
          c += a;
          if (c >= what) {
            c -= what;
          }
        }
        a += a;
        if (a >= what) {
          a -= what;
        }
        b >>= 1;
      }
      x = c;
      unsigned long long z = x < y ? what + x - y : x - y;
      g = gcd (z, what);
      if (g != 1) {
        break;
      }
      if (!(j & (j - 1))) {
        y = x;
      }
    }
    if (g > 1 && g < what) break;
  }

  assert (g > 1 && g < what);
  p1 = g;
  p2 = what / g;
  if (p1 > p2) {
    unsigned t = p1; p1 = p2; p2 = t;
  }
  

  /// ++p1; ///

  assert (*(int *) (from) == CODE_vector);
  int fingerprints_num = *(int *)(from + 4);
  assert (fingerprints_num >= 1 && fingerprints_num <= 64 && len == fingerprints_num * 8 + 8 + (from - packet));
  long long *fingerprints = (long long *) (from + 8);
  for (i = 0; i < fingerprints_num; i++) {
    if (fingerprints[i] == pk_fingerprint) {
      //logprintf ( "found our public key at position %d\n", i);
      break;
    }
  }
  if (i == fingerprints_num) {
    vlogprintf (E_ERROR, "fatal: don't have any matching keys (%016llx expected)\n", pk_fingerprint);
    exit (2);
  }
  // create inner part (P_Q_inner_data)
  clear_packet ();
  packet_ptr += 5;
  out_int (temp_key ? CODE_p_q_inner_data_temp : CODE_p_q_inner_data);
  out_cstring (packet + 57, clen);
  //out_int (0x0f01);  // pq=15

  if (p1 < 256) {
    clen = 1;
  } else if (p1 < 65536) {
    clen = 2;
  } else if (p1 < 16777216) {
    clen = 3;
  } else {
    clen = 4;
  } 
  p1 = __builtin_bswap32 (p1);
  out_cstring ((char *)&p1 + 4 - clen, clen);
  p1 = __builtin_bswap32 (p1);

  if (p2 < 256) {
    clen = 1;
  } else if (p2 < 65536) {
    clen = 2;
  } else if (p2 < 16777216) {
    clen = 3;
  } else {
    clen = 4;
  }
  p2 = __builtin_bswap32 (p2);
  out_cstring ((char *)&p2 + 4 - clen, clen);
  p2 = __builtin_bswap32 (p2);
    
  //out_int (0x0301);  // p=3
  //out_int (0x0501);  // q=5
  out_ints ((int *) D->nonce, 4);
  out_ints ((int *) D->server_nonce, 4);
  tglt_secure_random (D->new_nonce, 32);
  out_ints ((int *) D->new_nonce, 8);
  if (temp_key) {
    out_int (TLS->temp_key_expire_time);
  }
  sha1 ((unsigned char *) (packet_buffer + 5), (packet_ptr - packet_buffer - 5) * 4, (unsigned char *) packet_buffer);

  int l = encrypt_packet_buffer (TLS);
  
  clear_packet ();
  out_int (CODE_req_DH_params);
  out_ints ((int *) D->nonce, 4);
  out_ints ((int *) D->server_nonce, 4);
  //out_int (0x0301);  // p=3
  //out_int (0x0501);  // q=5
  if (p1 < 256) {
    clen = 1;
  } else if (p1 < 65536) {
    clen = 2;
  } else if (p1 < 16777216) {
    clen = 3;
  } else {
    clen = 4;
  } 
  p1 = __builtin_bswap32 (p1);
  out_cstring ((char *)&p1 + 4 - clen, clen);
  p1 = __builtin_bswap32 (p1);
  if (p2 < 256) {
    clen = 1;
  } else if (p2 < 65536) {
    clen = 2;
  } else if (p2 < 16777216) {
    clen = 3;
  } else {
    clen = 4;
  }
  p2 = __builtin_bswap32 (p2);
  out_cstring ((char *)&p2 + 4 - clen, clen);
  p2 = __builtin_bswap32 (p2);
    
  out_long (pk_fingerprint);
  out_cstring ((char *) encrypt_buffer, l);

  D->state = temp_key ? st_reqdh_sent_temp : st_reqdh_sent;
  
  return rpc_send_packet (TLS, c);
}

static int check_prime (struct tgl_state *TLS, BIGNUM *p) {
  int r = BN_is_prime (p, BN_prime_checks, 0, TLS->BN_ctx, 0);
  ensure (r >= 0);
  return r;
}

int tglmp_check_DH_params (struct tgl_state *TLS, BIGNUM *p, int g) {
  if (g < 2 || g > 7) { return -1; }
  if (BN_num_bits (p) != 2048) { return -1; }
  BIGNUM t;
  BN_init (&t);

  BN_init (&dh_g);
  ensure (BN_set_word (&dh_g, 4 * g));

  ensure (BN_mod (&t, p, &dh_g, TLS->BN_ctx));
  int x = BN_get_word (&t);
  assert (x >= 0 && x < 4 * g);

  BN_free (&dh_g);

  switch (g) {
  case 2:
    if (x != 7) { return -1; }
    break;
  case 3:
    if (x % 3 != 2 ) { return -1; }
    break;
  case 4:
    break;
  case 5:
    if (x % 5 != 1 && x % 5 != 4) { return -1; }
    break;
  case 6:
    if (x != 19 && x != 23) { return -1; }
    break;
  case 7:
    if (x % 7 != 3 && x % 7 != 5 && x % 7 != 6) { return -1; }
    break;
  }

  if (!check_prime (TLS, p)) { return -1; }

  BIGNUM b;
  BN_init (&b);
  ensure (BN_set_word (&b, 2));
  ensure (BN_div (&t, 0, p, &b, TLS->BN_ctx));
  if (!check_prime (TLS, &t)) { return -1; }
  BN_free (&b);
  BN_free (&t);
  return 0;
}

int tglmp_check_g (struct tgl_state *TLS, unsigned char p[256], BIGNUM *g) {
  static unsigned char s[256];
  memset (s, 0, 256);
  assert (BN_num_bytes (g) <= 256);
  BN_bn2bin (g, s + (256 - BN_num_bytes (g)));
  int ok = 0;
  int i;
  for (i = 0; i < 64; i++) {
    if (s[i]) { 
      ok = 1;
      break;
    }
  }
  if (!ok) { return -1; }
  ok = 0;
  for (i = 0; i < 64; i++) {
    if (s[255 - i]) { 
      ok = 1;
      break;
    }
  }
  if (!ok) { return -1; }
  ok = 0;
  for (i = 0; i < 64; i++) {
    if (s[i] < p[i]) { 
      ok = 1;
      break;
    } else if (s[i] > p[i]) {
      vlogprintf (E_WARNING, "i = %d (%d %d)\n", i, (int)s[i], (int)p[i]);
      return -1;
    }
  }
  if (!ok) { return -1; }
  return 0;
}

int tglmp_check_g_bn (struct tgl_state *TLS, BIGNUM *p, BIGNUM *g) {
  static unsigned char s[256];
  memset (s, 0, 256);
  assert (BN_num_bytes (p) == 256);
  BN_bn2bin (p, s);
  return tglmp_check_g (TLS, s, g);
}

static int process_dh_answer (struct tgl_state *TLS, struct connection *c, char *packet, int len, int temp_key) {
  struct tgl_dc *D = TLS->net_methods->get_dc (c);
  vlogprintf (E_DEBUG, "process_dh_answer(), len=%d\n", len);
  //if (len < 116) {
  //  vlogprintf (E_ERROR, "%u * %u = %llu", p1, p2, what);
  //}
  long long packet_auth_key_id = *(long long *)packet;
  if (packet_auth_key_id) {
    assert (temp_key);
    vlogprintf (E_WARNING, "received packet during creation of temp auth key. Probably answer on old query. Drop\n");
    return 0;
  }
  assert (len >= 116);
  assert (!*(long long *) packet);
  assert (*(int *) (packet + 16) == len - 20);
  assert (!(len & 3));
  assert (*(int *) (packet + 20) == (int)CODE_server_DH_params_ok);
  assert (!memcmp (packet + 24, D->nonce, 16));
  assert (!memcmp (packet + 40, D->server_nonce, 16));
  tgl_init_aes_unauth (D->server_nonce, D->new_nonce, AES_DECRYPT);
  in_ptr = (int *)(packet + 56);
  in_end = (int *)(packet + len);
  int l = prefetch_strlen ();
  assert (l > 0);
  l = tgl_pad_aes_decrypt (fetch_str (l), l, (char *) decrypt_buffer, DECRYPT_BUFFER_INTS * 4 - 16);
  assert (in_ptr == in_end);
  assert (l >= 60);
  assert (decrypt_buffer[5] == (int)CODE_server_DH_inner_data);
  assert (!memcmp (decrypt_buffer + 6, D->nonce, 16));
  assert (!memcmp (decrypt_buffer + 10, D->server_nonce, 16));
  int g = decrypt_buffer[14];
  in_ptr = decrypt_buffer + 15;
  in_end = decrypt_buffer + (l >> 2);
  BN_init (&dh_prime);
  BN_init (&g_a);
  assert (fetch_bignum (&dh_prime) > 0);
  assert (fetch_bignum (&g_a) > 0);
  assert (tglmp_check_g_bn (TLS, &dh_prime, &g_a) >= 0);
  int server_time = *in_ptr++;
  assert (in_ptr <= in_end);

  assert (tglmp_check_DH_params (TLS, &dh_prime, g) >= 0);

  static char sha1_buffer[20];
  sha1 ((unsigned char *) decrypt_buffer + 20, (in_ptr - decrypt_buffer - 5) * 4, (unsigned char *) sha1_buffer);
  assert (!memcmp (decrypt_buffer, sha1_buffer, 20));
  assert ((char *) in_end - (char *) in_ptr < 16);

  D->server_time_delta = server_time - time (0);
  D->server_time_udelta = server_time - get_utime (CLOCK_MONOTONIC);
  //logprintf ( "server time is %d, delta = %d\n", server_time, server_time_delta);

  // Build set_client_DH_params answer
  clear_packet ();
  packet_ptr += 5;
  out_int (CODE_client_DH_inner_data);
  out_ints ((int *) D->nonce, 4);
  out_ints ((int *) D->server_nonce, 4);
  out_long (0LL);
  
  BN_init (&dh_g);
  ensure (BN_set_word (&dh_g, g));

  tglt_secure_random (s_power, 256);
  BIGNUM *dh_power = BN_bin2bn ((unsigned char *)s_power, 256, 0);
  ensure_ptr (dh_power);

  BIGNUM *y = BN_new ();
  ensure_ptr (y);
  ensure (BN_mod_exp (y, &dh_g, dh_power, &dh_prime, TLS->BN_ctx));
  out_bignum (y);
  BN_free (y);

  BN_init (&auth_key_num);
  ensure (BN_mod_exp (&auth_key_num, &g_a, dh_power, &dh_prime, TLS->BN_ctx));
  l = BN_num_bytes (&auth_key_num);
  assert (l >= 250 && l <= 256);
  assert (BN_bn2bin (&auth_key_num, (unsigned char *)(temp_key ? D->temp_auth_key : D->auth_key)));
  if (l < 256) {
    char *key = temp_key ? D->temp_auth_key : D->auth_key;
    memmove (key + 256 - l, key, l);
    memset (key, 0, 256 - l);
  }

  BN_free (dh_power);
  BN_free (&auth_key_num);
  BN_free (&dh_g);
  BN_free (&g_a);
  BN_free (&dh_prime);

  //hexdump (auth_key, auth_key + 256);
 
  sha1 ((unsigned char *) (packet_buffer + 5), (packet_ptr - packet_buffer - 5) * 4, (unsigned char *) packet_buffer);

  //hexdump ((char *)packet_buffer, (char *)packet_ptr);

  l = encrypt_packet_buffer_aes_unauth (D->server_nonce, D->new_nonce);

  clear_packet ();
  out_int (CODE_set_client_DH_params);
  out_ints ((int *) D->nonce, 4);
  out_ints ((int *) D->server_nonce, 4);
  out_cstring ((char *) encrypt_buffer, l);

  D->state = temp_key ? st_client_dh_sent_temp : st_client_dh_sent;;

  return rpc_send_packet (TLS, c);
}

static void create_temp_auth_key (struct tgl_state *TLS, struct connection *c) {
  send_req_pq_temp_packet (TLS, c);
}

int tglmp_encrypt_inner_temp (struct tgl_state *TLS, struct connection *c, int *msg, int msg_ints, int useful, void *data, long long msg_id);
static long long generate_next_msg_id (struct tgl_state *TLS, struct tgl_dc *DC);
static long long msg_id_override;
static void mpc_on_get_config (struct tgl_state *TLS, void *extra, int success);
static int process_auth_complete (struct tgl_state *TLS, struct connection *c UU, char *packet, int len, int temp_key) {
  struct tgl_dc *D = TLS->net_methods->get_dc (c);
  vlogprintf (E_DEBUG - 1, "process_dh_answer(), len=%d\n", len);
  
  long long packet_auth_key_id = *(long long *)packet;
  if (packet_auth_key_id) {
    assert (temp_key);
    vlogprintf (E_WARNING, "received packet during creation of temp auth key. Probably answer on old query. Drop\n");
    return 0;
  }
  assert (len == 72);
  assert (!*(long long *) packet);
  assert (*(int *) (packet + 16) == len - 20);
  assert (!(len & 3));
  assert (*(int *) (packet + 20) == CODE_dh_gen_ok);
  assert (!memcmp (packet + 24, D->nonce, 16));
  assert (!memcmp (packet + 40, D->server_nonce, 16));
  static unsigned char tmp[44], sha1_buffer[20];
  memcpy (tmp, D->new_nonce, 32);
  tmp[32] = 1;
  //GET_DC(c)->auth_key_id = *(long long *)(sha1_buffer + 12);

  if (!temp_key) {
    bl_do_set_auth_key_id (TLS, D->id, (unsigned char *)D->auth_key);
    sha1 ((unsigned char *)D->auth_key, 256, sha1_buffer);
  } else {
    sha1 ((unsigned char *)D->temp_auth_key, 256, sha1_buffer);
    D->temp_auth_key_id = *(long long *)(sha1_buffer + 12);
  }

  memcpy (tmp + 33, sha1_buffer, 8);
  sha1 (tmp, 41, sha1_buffer);
  assert (!memcmp (packet + 56, sha1_buffer + 4, 16));
  D->server_salt = *(long long *)D->server_nonce ^ *(long long *)D->new_nonce;
  
  //kprintf ("OK\n");

  //c->status = conn_error;
  //sleep (1);

  D->state = st_authorized;
  //return 1;
  vlogprintf (E_DEBUG, "Auth success\n");
  if (temp_key) {
    //D->flags |= 2;
    
    long long msg_id = generate_next_msg_id (TLS, D);
    clear_packet ();
    out_int (CODE_bind_auth_key_inner);
    long long rand;
    tglt_secure_random (&rand, 8);
    out_long (rand);
    out_long (D->temp_auth_key_id);
    out_long (D->auth_key_id);

    struct tgl_session *S = TLS->net_methods->get_session (c);
    if (!S->session_id) {
      tglt_secure_random (&S->session_id, 8);
    }
    out_long (S->session_id);
    int expires = time (0) + D->server_time_delta + TLS->temp_key_expire_time;
    out_int (expires);

    static int data[1000];
    int len = tglmp_encrypt_inner_temp (TLS, c, packet_buffer, packet_ptr - packet_buffer, 0, data, msg_id);
    msg_id_override = msg_id;
    tgl_do_send_bind_temp_key (TLS, D, rand, expires, (void *)data, len, msg_id);
    msg_id_override = 0;
  } else {
    D->flags |= 1;
    if (TLS->enable_pfs) {
      create_temp_auth_key (TLS, c);
    } else {
      D->temp_auth_key_id = D->auth_key_id;
      memcpy (D->temp_auth_key, D->auth_key, 256);
      D->flags |= 2;
      if (!(D->flags & 4)) {
        tgl_do_help_get_config_dc (TLS, D, mpc_on_get_config, D);
      }
    }
  }
  
  //write_auth_file ();
  
  return 1;
}

/*
 *
 *                AUTHORIZED (MAIN) PROTOCOL PART
 *
 */

static struct encrypted_message enc_msg;

static long long client_last_msg_id, server_last_msg_id;

static double get_server_time (struct tgl_dc *DC) {
  if (!DC->server_time_udelta) {
    DC->server_time_udelta = get_utime (CLOCK_REALTIME) - get_utime (CLOCK_MONOTONIC);
  }
  return get_utime (CLOCK_MONOTONIC) + DC->server_time_udelta;
}

static long long generate_next_msg_id (struct tgl_state *TLS, struct tgl_dc *DC) {
  long long next_id = (long long) (get_server_time (DC) * (1LL << 32)) & -4;
  if (next_id <= client_last_msg_id) {
    next_id = client_last_msg_id += 4;
  } else {
    client_last_msg_id = next_id;
  }
  return next_id;
}

static void init_enc_msg (struct tgl_state *TLS, struct tgl_session *S, int useful) {
  struct tgl_dc *DC = S->dc;
  assert (DC->state == st_authorized);
  //assert (DC->flags & 2);
  assert (DC->temp_auth_key_id);
  vlogprintf (E_DEBUG, "temp_auth_key_id = 0x%016llx, auth_key_id = 0x%016llx\n", DC->temp_auth_key_id, DC->auth_key_id);
  enc_msg.auth_key_id = DC->temp_auth_key_id;
//  assert (DC->server_salt);
  enc_msg.server_salt = DC->server_salt;
  if (!S->session_id) {
    tglt_secure_random (&S->session_id, 8);
  }
  enc_msg.session_id = S->session_id;
  //enc_msg.auth_key_id2 = auth_key_id;
  enc_msg.msg_id = msg_id_override ? msg_id_override : generate_next_msg_id (TLS, DC);
  //enc_msg.msg_id -= 0x10000000LL * (lrand48 () & 15);
  //kprintf ("message id %016llx\n", enc_msg.msg_id);
  enc_msg.seq_no = S->seq_no;
  if (useful) {
    enc_msg.seq_no |= 1;
  }
  S->seq_no += 2;
};

static void init_enc_msg_inner_temp (struct tgl_dc *DC, long long msg_id) {
  enc_msg.auth_key_id = DC->auth_key_id;
  tglt_secure_random (&enc_msg.server_salt, 8);
  tglt_secure_random (&enc_msg.session_id, 8);
  enc_msg.msg_id = msg_id;
  enc_msg.seq_no = 0;
};


static int aes_encrypt_message (struct tgl_state *TLS, char *key, struct encrypted_message *enc) {
  unsigned char sha1_buffer[20];
  const int MINSZ = offsetof (struct encrypted_message, message);
  const int UNENCSZ = offsetof (struct encrypted_message, server_salt);
  int enc_len = (MINSZ - UNENCSZ) + enc->msg_len;
  assert (enc->msg_len >= 0 && enc->msg_len <= MAX_MESSAGE_INTS * 4 - 16 && !(enc->msg_len & 3));
  sha1 ((unsigned char *) &enc->server_salt, enc_len, sha1_buffer);
  //printf ("enc_len is %d\n", enc_len);
  vlogprintf (E_DEBUG, "sending message with sha1 %08x\n", *(int *)sha1_buffer);
  memcpy (enc->msg_key, sha1_buffer + 4, 16);
  tgl_init_aes_auth (key, enc->msg_key, AES_ENCRYPT);
  //hexdump ((char *)enc, (char *)enc + enc_len + 24);
  return tgl_pad_aes_encrypt ((char *) &enc->server_salt, enc_len, (char *) &enc->server_salt, MAX_MESSAGE_INTS * 4 + (MINSZ - UNENCSZ));
}

long long tglmp_encrypt_send_message (struct tgl_state *TLS, struct connection *c, int *msg, int msg_ints, int flags) {
  struct tgl_dc *DC = TLS->net_methods->get_dc (c);
  if (!(DC->flags & 4) && !(flags & 2)) {
    return generate_next_msg_id (TLS, DC);
  }
  struct tgl_session *S = TLS->net_methods->get_session (c);
  assert (S);

  const int UNENCSZ = offsetof (struct encrypted_message, server_salt);
  if (msg_ints <= 0 || msg_ints > MAX_MESSAGE_INTS - 4) {
    return -1;
  }
  if (msg) {
    memcpy (enc_msg.message, msg, msg_ints * 4);
    enc_msg.msg_len = msg_ints * 4;
  } else {
    if ((enc_msg.msg_len & 0x80000003) || enc_msg.msg_len > MAX_MESSAGE_INTS * 4 - 16) {
      return -1;
    }
  }
  init_enc_msg (TLS, S, flags & 1);

  //hexdump ((char *)msg, (char *)msg + (msg_ints * 4));
  int l = aes_encrypt_message (TLS, DC->temp_auth_key, &enc_msg);
  //hexdump ((char *)&enc_msg, (char *)&enc_msg + l  + 24);
  assert (l > 0);
  vlogprintf (E_DEBUG, "Sending message to DC%d: %s:%d with key_id=%lld\n", DC->id, DC->ip, DC->port, enc_msg.auth_key_id);
  rpc_send_message (TLS, c, &enc_msg, l + UNENCSZ);

  
  return client_last_msg_id;
}

int tglmp_encrypt_inner_temp (struct tgl_state *TLS, struct connection *c, int *msg, int msg_ints, int useful, void *data, long long msg_id) {
  struct tgl_dc *DC = TLS->net_methods->get_dc (c);
  struct tgl_session *S = TLS->net_methods->get_session (c);
  assert (S);

  const int UNENCSZ = offsetof (struct encrypted_message, server_salt);
  if (msg_ints <= 0 || msg_ints > MAX_MESSAGE_INTS - 4) {
    return -1;
  }
  memcpy (enc_msg.message, msg, msg_ints * 4);
  enc_msg.msg_len = msg_ints * 4;

  init_enc_msg_inner_temp (DC, msg_id);

  int l = aes_encrypt_message (TLS, DC->auth_key, &enc_msg);
  assert (l > 0);
  //rpc_send_message (c, &enc_msg, l + UNENCSZ);
  memcpy (data, &enc_msg, l + UNENCSZ);
  
  return l + UNENCSZ;
}

static int good_messages;

static void rpc_execute_answer (struct tgl_state *TLS, struct connection *c, long long msg_id UU);

//int unread_messages;
//int pts;
//int qts;
//int last_date;
//int seq;

static void work_container (struct tgl_state *TLS, struct connection *c, long long msg_id UU) {
  vlogprintf (E_DEBUG, "work_container: msg_id = %lld\n", msg_id);
  assert (fetch_int () == CODE_msg_container);
  int n = fetch_int ();
  int i;
  for (i = 0; i < n; i++) {
    long long id = fetch_long (); 
    //int seqno = fetch_int (); 
    fetch_int (); // seq_no
    if (id & 1) {
      tgln_insert_msg_id (TLS, TLS->net_methods->get_session (c), id);
    }
    int bytes = fetch_int ();
    int *t = in_end;
    in_end = in_ptr + (bytes / 4);
    rpc_execute_answer (TLS, c, id);
    assert (in_ptr == in_end);
    in_end = t;
  }
}

static void work_new_session_created (struct tgl_state *TLS, struct connection *c, long long msg_id UU) {
  vlogprintf (E_DEBUG, "work_new_session_created: msg_id = %lld\n", msg_id);
  assert (fetch_int () == (int)CODE_new_session_created);
  fetch_long (); // first message id
  //DC->session_id = fetch_long ();
  fetch_long (); // unique_id
  TLS->net_methods->get_dc (c)->server_salt = fetch_long ();
  if (TLS->started && !(TLS->locks & TGL_LOCK_DIFF)) {
    tgl_do_get_difference (TLS, 0, 0, 0);
  }
}

static void work_msgs_ack (struct tgl_state *TLS, struct connection *c UU, long long msg_id UU) {
  vlogprintf (E_DEBUG, "work_msgs_ack: msg_id = %lld\n", msg_id);
  assert (fetch_int () == CODE_msgs_ack);
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  int i;
  for (i = 0; i < n; i++) {
    long long id = fetch_long ();
    vlogprintf (E_DEBUG + 1, "ack for %lld\n", id);
    tglq_query_ack (TLS, id);
  }
}

static void work_rpc_result (struct tgl_state *TLS, struct connection *c UU, long long msg_id UU) {
  vlogprintf (E_DEBUG, "work_rpc_result: msg_id = %lld\n", msg_id);
  assert (fetch_int () == (int)CODE_rpc_result);
  long long id = fetch_long ();
  int op = prefetch_int ();
  if (op == CODE_rpc_error) {
    tglq_query_error (TLS, id);
  } else {
    tglq_query_result (TLS, id);
  }
}

#define MAX_PACKED_SIZE (1 << 24)
static void work_packed (struct tgl_state *TLS, struct connection *c, long long msg_id) {
  assert (fetch_int () == CODE_gzip_packed);
  static int in_gzip;
  static int buf[MAX_PACKED_SIZE >> 2];
  assert (!in_gzip);
  in_gzip = 1;
    
  int l = prefetch_strlen ();
  char *s = fetch_str (l);

  int total_out = tgl_inflate (s, l, buf, MAX_PACKED_SIZE);
  int *end = in_ptr;
  int *eend = in_end;
  //assert (total_out % 4 == 0);
  in_ptr = buf;
  in_end = in_ptr + total_out / 4;
  rpc_execute_answer (TLS, c, msg_id);
  in_ptr = end;
  in_end = eend;
  in_gzip = 0;
}

static void work_bad_server_salt (struct tgl_state *TLS, struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == (int)CODE_bad_server_salt);
  long long id = fetch_long ();
  tglq_query_restart (TLS, id);
  fetch_int (); // seq_no
  fetch_int (); // error_code
  long long new_server_salt = fetch_long ();
  TLS->net_methods->get_dc (c)->server_salt = new_server_salt;
}

static void work_pong (struct tgl_state *TLS, struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == CODE_pong);
  fetch_long (); // msg_id
  fetch_long (); // ping_id
}

static void work_detailed_info (struct tgl_state *TLS, struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == CODE_msg_detailed_info);
  fetch_long (); // msg_id
  fetch_long (); // answer_msg_id
  fetch_int (); // bytes
  fetch_int (); // status
}

static void work_new_detailed_info (struct tgl_state *TLS, struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == (int)CODE_msg_new_detailed_info);
  fetch_long (); // answer_msg_id
  fetch_int (); // bytes
  fetch_int (); // status
}

static void work_bad_msg_notification (struct tgl_state *TLS, struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == (int)CODE_bad_msg_notification);
  long long m1 = fetch_long ();
  int s = fetch_int ();
  int e = fetch_int ();
  vlogprintf (E_NOTICE, "bad_msg_notification: msg_id = %lld, seq = %d, error = %d\n", m1, s, e);
}

static void rpc_execute_answer (struct tgl_state *TLS, struct connection *c, long long msg_id UU) {
  int op = prefetch_int ();
  switch (op) {
  case CODE_msg_container:
    work_container (TLS, c, msg_id);
    return;
  case CODE_new_session_created:
    work_new_session_created (TLS, c, msg_id);
    return;
  case CODE_msgs_ack:
    work_msgs_ack (TLS, c, msg_id);
    return;
  case CODE_rpc_result:
    work_rpc_result (TLS, c, msg_id);
    return;
  case CODE_update_short:
    tglu_work_update_short (TLS, c, msg_id);
    return;
  case CODE_updates:
    tglu_work_updates (TLS, c, msg_id);
    return;
  case CODE_update_short_message:
    tglu_work_update_short_message (TLS, c, msg_id);
    return;
  case CODE_update_short_chat_message:
    tglu_work_update_short_chat_message (TLS, c, msg_id);
    return;
  case CODE_gzip_packed:
    work_packed (TLS, c, msg_id);
    return;
  case CODE_bad_server_salt:
    work_bad_server_salt (TLS, c, msg_id);
    return;
  case CODE_pong:
    work_pong (TLS, c, msg_id);
    return;
  case CODE_msg_detailed_info:
    work_detailed_info (TLS, c, msg_id);
    return;
  case CODE_msg_new_detailed_info:
    work_new_detailed_info (TLS, c, msg_id);
    return;
  case CODE_updates_too_long:
    tglu_work_updates_to_long (TLS, c, msg_id);
    return;
  case CODE_bad_msg_notification:
    work_bad_msg_notification (TLS, c, msg_id);
    return;
  }
  vlogprintf (E_WARNING, "Unknown message: %08x\n", op);
  in_ptr = in_end; // Will not fail due to assertion in_ptr == in_end
}

static int process_rpc_message (struct tgl_state *TLS, struct connection *c UU, struct encrypted_message *enc, int len) {
  const int MINSZ = offsetof (struct encrypted_message, message);
  const int UNENCSZ = offsetof (struct encrypted_message, server_salt);
  vlogprintf (E_DEBUG, "process_rpc_message(), len=%d\n", len);  
  assert (len >= MINSZ && (len & 15) == (UNENCSZ & 15));
  struct tgl_dc *DC = TLS->net_methods->get_dc (c);
  if (enc->auth_key_id != DC->temp_auth_key_id && enc->auth_key_id != DC->auth_key_id) {
    vlogprintf (E_WARNING, "received msg from dc %d with auth_key_id %lld (perm_auth_key_id %lld temp_auth_key_id %lld). Dropping\n",
    DC->id, enc->auth_key_id, DC->auth_key_id, DC->temp_auth_key_id);
    return 0;
  }
  if (enc->auth_key_id == DC->temp_auth_key_id) {
    assert (enc->auth_key_id == DC->temp_auth_key_id);
    assert (DC->temp_auth_key_id);
    tgl_init_aes_auth (DC->temp_auth_key + 8, enc->msg_key, AES_DECRYPT);
  } else {
    assert (enc->auth_key_id == DC->auth_key_id);
    assert (DC->auth_key_id);
    tgl_init_aes_auth (DC->auth_key + 8, enc->msg_key, AES_DECRYPT);
  }
  int l = tgl_pad_aes_decrypt ((char *)&enc->server_salt, len - UNENCSZ, (char *)&enc->server_salt, len - UNENCSZ);
  assert (l == len - UNENCSZ);
  //assert (enc->auth_key_id2 == enc->auth_key_id);
  assert (!(enc->msg_len & 3) && enc->msg_len > 0 && enc->msg_len <= len - MINSZ && len - MINSZ - enc->msg_len <= 12);
  static unsigned char sha1_buffer[20];
  sha1 ((void *)&enc->server_salt, enc->msg_len + (MINSZ - UNENCSZ), sha1_buffer);
  assert (!memcmp (&enc->msg_key, sha1_buffer + 4, 16));
  //assert (enc->server_salt == server_salt); //in fact server salt can change
  if (DC->server_salt != enc->server_salt) {
    DC->server_salt = enc->server_salt;
    //write_auth_file ();
  }
 
  
  int this_server_time = enc->msg_id >> 32LL;
  if (!DC->server_time_delta) {
    DC->server_time_delta = this_server_time - get_utime (CLOCK_REALTIME);
    DC->server_time_udelta = this_server_time - get_utime (CLOCK_MONOTONIC);
  }
  double st = get_server_time (DC);
  if (this_server_time < st - 300 || this_server_time > st + 30) {
    vlogprintf (E_WARNING, "salt = %lld, session_id = %lld, msg_id = %lld, seq_no = %d, st = %lf, now = %lf\n", enc->server_salt, enc->session_id, enc->msg_id, enc->seq_no, st, get_utime (CLOCK_REALTIME));
    return 0;
  }


  assert (this_server_time >= st - 300 && this_server_time <= st + 30);
  //assert (enc->msg_id > server_last_msg_id && (enc->msg_id & 3) == 1);
  vlogprintf (E_DEBUG, "received mesage id %016llx\n", enc->msg_id);
  server_last_msg_id = enc->msg_id;

  //*(long long *)(longpoll_query + 3) = *(long long *)((char *)(&enc->msg_id) + 0x3c);
  //*(long long *)(longpoll_query + 5) = *(long long *)((char *)(&enc->msg_id) + 0x3c);

  assert (l >= (MINSZ - UNENCSZ) + 8);
  //assert (enc->message[0] == CODE_rpc_result && *(long long *)(enc->message + 1) == client_last_msg_id);
  ++good_messages;
  
  in_ptr = enc->message;
  in_end = in_ptr + (enc->msg_len / 4);
  
  /*{
    assert (len <= 10000);
    static char s[1 << 20];
    int p = 0;
    int i;
    //static int buf[10000];
    //assert (TLS->net_methods->read_in_lookup (c, buf, len) == len);
    
    for (i = 0; i < in_end - in_ptr; i++) {
      p += sprintf (s + p, "%08x ", *(int *)(in_ptr + i));
    }
    vlogprintf (E_DEBUG, "%s\n", s);
  }*/
 
  struct tgl_session *S = TLS->net_methods->get_session (c);
  if (enc->msg_id & 1) {
    tgln_insert_msg_id (TLS, S, enc->msg_id);
  }
  assert (S->session_id == enc->session_id);
  rpc_execute_answer (TLS, c, enc->msg_id);
  assert (in_ptr == in_end);
  return 0;
}


static int rpc_execute (struct tgl_state *TLS, struct connection *c, int op, int len) {
  struct tgl_dc *D = TLS->net_methods->get_dc (c);
  vlogprintf (E_DEBUG, "outbound rpc connection from dc #%d (%s:%d) : received rpc answer %d with %d content bytes\n", D->id, D->ip, D->port, op, len);
  
  /*{
    assert (len <= 10000);
    static char s[1 << 20];
    int p = 0;
    int i;
    static int buf[10000];
    assert (TLS->net_methods->read_in_lookup (c, buf, len) == len);
    
    for (i = 0; i < len / 4; i++) {
      p += sprintf (s + p, "%08x ", *(int *)(buf + i));
    }
    vlogprintf (E_DEBUG, "%s\n", s);
  }*/
  /*  if (op < 0) {
    assert (TLS->net_methods->read_in (c, Response, Response_len) == Response_len);
    return 0;
  }*/

  if (len >= MAX_RESPONSE_SIZE/* - 12*/ || len < 0/*12*/) {
    vlogprintf (E_WARNING, "answer too long (%d bytes), skipping\n", len);
    return 0;
  }

  int Response_len = len;

  static char Response[MAX_RESPONSE_SIZE];
  vlogprintf (E_DEBUG, "Response_len = %d\n", Response_len);
  assert (TLS->net_methods->read_in (c, Response, Response_len) == Response_len);
  Response[Response_len] = 0;

#if !defined(__MACH__) && !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined (__CYGWIN__)
//  setsockopt (c->fd, IPPROTO_TCP, TCP_QUICKACK, (int[]){0}, 4);
#endif
  int o = D->state;
  //if (D->flags & 1) { o = st_authorized;}
  switch (o) {
  case st_reqpq_sent:
    process_respq_answer (TLS, c, Response/* + 8*/, Response_len/* - 12*/, 0);
    return 0;
  case st_reqdh_sent:
    process_dh_answer (TLS, c, Response/* + 8*/, Response_len/* - 12*/, 0);
    return 0;
  case st_client_dh_sent:
    process_auth_complete (TLS, c, Response/* + 8*/, Response_len/* - 12*/, 0);
    return 0;
  case st_reqpq_sent_temp:
    process_respq_answer (TLS, c, Response/* + 8*/, Response_len/* - 12*/, 1);
    return 0;
  case st_reqdh_sent_temp:
    process_dh_answer (TLS, c, Response/* + 8*/, Response_len/* - 12*/, 1);
    return 0;
  case st_client_dh_sent_temp:
    process_auth_complete (TLS, c, Response/* + 8*/, Response_len/* - 12*/, 1);
    return 0;
  case st_authorized:
    if (op < 0 && op >= -999) {
      vlogprintf (E_WARNING, "Server error %d\n", op);
    } else {
      process_rpc_message (TLS, c, (void *)(Response/* + 8*/), Response_len/* - 12*/);
    }
    return 0;
  default:
    vlogprintf (E_ERROR, "fatal: cannot receive answer in state %d\n", D->state);
    exit (2);
  }
 
  return 0;
}


static int tc_close (struct tgl_state *TLS, struct connection *c, int who) {
  vlogprintf (E_DEBUG, "outbound rpc connection from dc #%d : closing by %d\n", TLS->net_methods->get_dc(c)->id, who);
  return 0;
}

static void mpc_on_get_config (struct tgl_state *TLS, void *extra, int success) {
  assert (success);
  struct tgl_dc *D = extra;
  D->flags |= 4;
}

static int tc_becomes_ready (struct tgl_state *TLS, struct connection *c) {
  vlogprintf (E_DEBUG, "outbound rpc connection from dc #%d becomed ready\n", TLS->net_methods->get_dc(c)->id);
  //char byte = 0xef;
  //assert (TLS->net_methods->write_out (c, &byte, 1) == 1);
  //TLS->net_methods->flush_out (c);
  
  struct tgl_dc *D = TLS->net_methods->get_dc (c);
  if (D->flags & 1) { D->state = st_authorized; }
  int o = D->state;
  if (o == st_authorized && !TLS->enable_pfs) {
    D->temp_auth_key_id = D->auth_key_id;
    memcpy (D->temp_auth_key, D->auth_key, 256);
    D->flags |= 2;
  }
  switch (o) {
  case st_init:
    send_req_pq_packet (TLS, c);
    break;
  case st_authorized:
    if (!(D->flags & 2)) {
      assert (!D->temp_auth_key_id);
      create_temp_auth_key (TLS, c);
    } else if (!(D->flags & 4)) {
      tgl_do_help_get_config_dc (TLS, D, mpc_on_get_config, D);
    }
    break;
  default:
    vlogprintf (E_DEBUG, "c_state = %d\n", D->state);
    assert (0);
  }
  return 0;
}

static int rpc_becomes_ready (struct tgl_state *TLS, struct connection *c) {
  return tc_becomes_ready (TLS, c);
}

static int rpc_close (struct tgl_state *TLS, struct connection *c) {
  return tc_close (TLS, c, 0);
}


#define RANDSEED_PASSWORD_FILENAME     NULL
#define RANDSEED_PASSWORD_LENGTH       0
void tglmp_on_start (struct tgl_state *TLS) {
  tgl_prng_seed (TLS, RANDSEED_PASSWORD_FILENAME, RANDSEED_PASSWORD_LENGTH);

  int i;
  int ok = 0;
  for (i = 0; i < TLS->rsa_key_num; i++) {
    char *key = TLS->rsa_key_list[i];
    if (rsa_load_public_key (TLS, key) < 0) {
      vlogprintf (E_WARNING, "Can not load key %s\n", key);
    } else {
      ok = 1;
      break;
    }
  }

  if (!ok) {
    vlogprintf (E_ERROR, "No public keys found\n");
    exit (1);
  }
  
  pk_fingerprint = tgl_do_compute_rsa_key_fingerprint (TLS->pubKey);
}

void tgl_dc_authorize (struct tgl_state *TLS, struct tgl_dc *DC) {
  //c_state = 0;
  if (!DC->sessions[0]) {
    tglmp_dc_create_session (TLS, DC);
  }
  vlogprintf (E_DEBUG, "Starting authorization for DC #%d: %s:%d\n", DC->id, DC->ip, DC->port);
  //net_loop (0, auth_ok);
}

#define long_cmp(a,b) ((a) > (b) ? 1 : (a) == (b) ? 0 : -1)
DEFINE_TREE(long,long long,long_cmp,0)

static int send_all_acks (struct tgl_state *TLS, struct tgl_session *S) {
  clear_packet ();
  out_int (CODE_msgs_ack);
  out_int (CODE_vector);
  out_int (tree_count_long (S->ack_tree));
  while (S->ack_tree) {
    long long x = tree_get_min_long (S->ack_tree); 
    out_long (x);
    S->ack_tree = tree_delete_long (S->ack_tree, x);
  }
  tglmp_encrypt_send_message (TLS, S->c, packet_buffer, packet_ptr - packet_buffer, 0);
  return 0;
}

static void send_all_acks_gateway (struct tgl_state *TLS, void *arg) {
  send_all_acks (TLS, arg);
}


void tgln_insert_msg_id (struct tgl_state *TLS, struct tgl_session *S, long long id) {
  if (!S->ack_tree) {
    TLS->timer_methods->insert (S->ev, ACK_TIMEOUT); 
  }
  if (!tree_lookup_long (S->ack_tree, id)) {
    S->ack_tree = tree_insert_long (S->ack_tree, id, lrand48 ());
  }
}

//extern struct tgl_dc *DC_list[];


static void regen_temp_key_gw (struct tgl_state *TLS, void *arg) {
  tglmp_regenerate_temp_auth_key (TLS, arg);
}

struct tgl_dc *tglmp_alloc_dc (struct tgl_state *TLS, int id, char *ip, int port UU) {
  //assert (!TLS->DC_list[id]);
  if (!TLS->DC_list[id]) {
    struct tgl_dc *DC = talloc0 (sizeof (*DC));
    DC->id = id;
    DC->ip = ip;
    DC->port = port;
    TLS->DC_list[id] = DC;
    if (id > TLS->max_dc_num) {
      TLS->max_dc_num = id;
    }
    DC->ev = TLS->timer_methods->alloc (TLS, regen_temp_key_gw, DC);
    TLS->timer_methods->insert (DC->ev, 0);
    return DC;
  } else {
    struct tgl_dc *DC = TLS->DC_list[id];
    tfree_str (DC->ip);
    DC->ip = tstrdup (ip);
    return DC;
  }
}

static struct mtproto_methods mtproto_methods = {
  .execute = rpc_execute,
  .ready = rpc_becomes_ready,
  .close = rpc_close
};

void tglmp_dc_create_session (struct tgl_state *TLS, struct tgl_dc *DC) {
  struct tgl_session *S = talloc0 (sizeof (*S));
  assert (RAND_pseudo_bytes ((unsigned char *) &S->session_id, 8) >= 0);
  S->dc = DC;
  S->c = TLS->net_methods->create_connection (TLS, DC->ip, DC->port, S, DC, &mtproto_methods);
  if (!S->c) {
    vlogprintf (E_DEBUG, "Can not create connection to DC. Is network down?\n");
    exit (1);
  }
  S->ev = TLS->timer_methods->alloc (TLS, send_all_acks_gateway, S);
  assert (!DC->sessions[0]);
  DC->sessions[0] = S;
}

void tgl_do_send_ping (struct tgl_state *TLS, struct connection *c) {
  int x[3];
  x[0] = CODE_ping;
  *(long long *)(x + 1) = lrand48 () * (1ll << 32) + lrand48 ();
  tglmp_encrypt_send_message (TLS, c, x, 3, 0);
}

void tgl_dc_iterator (struct tgl_state *TLS, void (*iterator)(struct tgl_dc *DC)) {
  int i;
  for (i = 0; i <= TLS->max_dc_num; i++) {
    iterator (TLS->DC_list[i]);
  }
}

void tgl_dc_iterator_ex (struct tgl_state *TLS, void (*iterator)(struct tgl_dc *DC, void *extra), void *extra) {
  int i;
  for (i = 0; i <= TLS->max_dc_num; i++) {
    iterator (TLS->DC_list[i], extra);
  }
}


void tglmp_regenerate_temp_auth_key (struct tgl_state *TLS, struct tgl_dc *D) {
  D->flags &= ~6;
  D->temp_auth_key_id = 0;
  memset (D->temp_auth_key, 0, 256);

  if (!D->sessions[0]) { 
    tgl_dc_authorize (TLS, D);
    return;
  }


  struct tgl_session *S = D->sessions[0];
  tglt_secure_random (&S->session_id, 8);
  S->seq_no = 0;

  TLS->timer_methods->delete (S->ev);
  S->ack_tree = tree_clear_long (S->ack_tree);
  
  if (D->state != st_authorized) {
    return;
  }

  if (S->c) {
    create_temp_auth_key (TLS, S->c);
  }
}

void tgls_free_session (struct tgl_state *TLS, struct tgl_session *S) {
  S->ack_tree = tree_clear_long (S->ack_tree);
  if (S->ev) { TLS->timer_methods->free (S->ev); }
  if (S->c) {
    TLS->net_methods->free (S->c);
  }
  tfree (S, sizeof (*S));
}

void tgls_free_dc (struct tgl_state *TLS, struct tgl_dc *DC) {
  if (DC->ip) { tfree_str (DC->ip); }

  struct tgl_session *S = DC->sessions[0];
  if (S) { tgls_free_session (TLS, S); }

  if (DC->ev) { TLS->timer_methods->free (DC->ev); }
  tfree (DC, sizeof (*DC));
}

void tgls_free_pubkey (struct tgl_state *TLS) {
  RSA_free (TLS->pubKey);
}
