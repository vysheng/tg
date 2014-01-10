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

    Copyright Nikolay Durov, Andrey Lopatin 2012-2013
    Copyright Vitaly Valtman 2013
*/
#define        _FILE_OFFSET_BITS        64

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef __FreeBSD__
#include <sys/endian.h>
#endif
#include <sys/types.h>
#include <aio.h>
#include <netdb.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>

#include "net.h"
#include "include.h"
#include "queries.h"
#include "loop.h"
#include "interface.h"
#include "structures.h"
#include "binlog.h"

#if defined(__FreeBSD__)
#define __builtin_bswap32(x) bswap32(x)
#endif

#define sha1 SHA1

#include "mtproto-common.h"

#define MAX_NET_RES        (1L << 16)
extern int log_level;

int verbosity;
int auth_success;
enum dc_state c_state;
char nonce[256];
char new_nonce[256];
char server_nonce[256];
extern int binlog_enabled;
extern int disable_auto_accept;
extern int allow_weak_random;

int total_packets_sent;
long long total_data_sent;


int rpc_execute (struct connection *c, int op, int len);
int rpc_becomes_ready (struct connection *c);
int rpc_close (struct connection *c);

struct connection_methods auth_methods = {
  .execute = rpc_execute,
  .ready = rpc_becomes_ready,
  .close = rpc_close
};

long long precise_time;
long long precise_time_rdtsc;
double get_utime (int clock_id) {
  struct timespec T;
  my_clock_gettime (clock_id, &T);
  double res = T.tv_sec + (double) T.tv_nsec * 1e-9;
  if (clock_id == CLOCK_REALTIME) {
    precise_time = (long long) (res * (1LL << 32));
    precise_time_rdtsc = rdtsc ();
  }
  return res;
}

void secure_random (void *s, int l) {
  if (RAND_bytes (s, l) < 0) {
    if (allow_weak_random) {
      RAND_pseudo_bytes (s, l);
    } else {
      assert (0 && "End of random. If you want, you can start with -w");
    }
  }
}


#define STATS_BUFF_SIZE        (64 << 10)
int stats_buff_len;
char stats_buff[STATS_BUFF_SIZE];

#define MAX_RESPONSE_SIZE        (1L << 24)

char Response[MAX_RESPONSE_SIZE];
int Response_len;

/*
 *
 *                STATE MACHINE
 *
 */

char *rsa_public_key_name = "tg.pub";
RSA *pubKey;
long long pk_fingerprint;

static int rsa_load_public_key (const char *public_key_name) {
  pubKey = NULL;
  FILE *f = fopen (public_key_name, "r");
  if (f == NULL) {
    logprintf ( "Couldn't open public key file: %s\n", public_key_name);
    return -1;
  }
  pubKey = PEM_read_RSAPublicKey (f, NULL, NULL, NULL);
  fclose (f);
  if (pubKey == NULL) {
    logprintf ( "PEM_read_RSAPublicKey returns NULL.\n");
    return -1;
  }

  return 0;
}





int auth_work_start (struct connection *c);

/*
 *
 *        UNAUTHORIZED (DH KEY EXCHANGE) PROTOCOL PART
 *
 */

BIGNUM dh_prime, dh_g, g_a, dh_power, auth_key_num;
char s_power [256];

struct {
  long long auth_key_id;
  long long out_msg_id;
  int msg_len;
} unenc_msg_header;


#define ENCRYPT_BUFFER_INTS        16384
int encrypt_buffer[ENCRYPT_BUFFER_INTS];

#define DECRYPT_BUFFER_INTS        16384
int decrypt_buffer[ENCRYPT_BUFFER_INTS];

int encrypt_packet_buffer (void) {
  return pad_rsa_encrypt ((char *) packet_buffer, (packet_ptr - packet_buffer) * 4, (char *) encrypt_buffer, ENCRYPT_BUFFER_INTS * 4, pubKey->n, pubKey->e);
}

int encrypt_packet_buffer_aes_unauth (const char server_nonce[16], const char hidden_client_nonce[32]) {
  init_aes_unauth (server_nonce, hidden_client_nonce, AES_ENCRYPT);
  return pad_aes_encrypt ((char *) packet_buffer, (packet_ptr - packet_buffer) * 4, (char *) encrypt_buffer, ENCRYPT_BUFFER_INTS * 4);
}


int rpc_send_packet (struct connection *c) {
  int len = (packet_ptr - packet_buffer) * 4;
  c->out_packet_num ++;
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
  if (total_len < 0x7f) {
    assert (write_out (c, &total_len, 1) == 1);
  } else {
    total_len = (total_len << 8) | 0x7f;
    assert (write_out (c, &total_len, 4) == 4);
  }
  write_out (c, &unenc_msg_header, 20);
  write_out (c, packet_buffer, len);
  flush_out (c);

  total_packets_sent ++;
  total_data_sent += total_len;
  return 1;
}

int rpc_send_message (struct connection *c, void *data, int len) {
  assert (len > 0 && !(len & 0xfc000003));
  int total_len = len >> 2;
  if (total_len < 0x7f) {
    assert (write_out (c, &total_len, 1) == 1);
  } else {
    total_len = (total_len << 8) | 0x7f;
    assert (write_out (c, &total_len, 4) == 4);
  }
  c->out_packet_num ++;
  assert (write_out (c, data, len) == len);
  flush_out (c);

  total_packets_sent ++;
  total_data_sent += total_len;
  return 1;
}

int send_req_pq_packet (struct connection *c) {
  assert (c_state == st_init);
  secure_random (nonce, 16);
  unenc_msg_header.out_msg_id = 0;
  clear_packet ();
  out_int (CODE_req_pq);
  out_ints ((int *)nonce, 4);
  rpc_send_packet (c);    
  c_state = st_reqpq_sent;
  return 1;
}


unsigned long long gcd (unsigned long long a, unsigned long long b) {
  return b ? gcd (b, a % b) : a;
}

//typedef unsigned int uint128_t __attribute__ ((mode(TI)));
unsigned long long what;
unsigned p1, p2;

int process_respq_answer (struct connection *c, char *packet, int len) {
  int i;
  if (verbosity) {
    logprintf ( "process_respq_answer(), len=%d\n", len);
  }
  assert (len >= 76);
  assert (!*(long long *) packet);
  assert (*(int *) (packet + 16) == len - 20);
  assert (!(len & 3));
  assert (*(int *) (packet + 20) == CODE_resPQ);
  assert (!memcmp (packet + 24, nonce, 16));
  memcpy (server_nonce, packet + 40, 16);
  char *from = packet + 56;
  int clen = *from++;
  assert (clen <= 8);
  what = 0;
  for (i = 0; i < clen; i++) {
    what = (what << 8) + (unsigned char)*from++;
  }

  while (((unsigned long)from) & 3) ++from;

  p1 = 0, p2 = 0;

  if (verbosity >= 2) {
    logprintf ( "%lld received\n", what);
  }

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
  

  if (verbosity) {
    logprintf ( "p1 = %d, p2 = %d, %d iterations\n", p1, p2, it);
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
    logprintf ( "fatal: don't have any matching keys (%016llx expected)\n", pk_fingerprint);
    exit (2);
  }
  // create inner part (P_Q_inner_data)
  clear_packet ();
  packet_ptr += 5;
  out_int (CODE_p_q_inner_data);
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
  out_ints ((int *) nonce, 4);
  out_ints ((int *) server_nonce, 4);
  secure_random (new_nonce, 32);
  out_ints ((int *) new_nonce, 8);
  sha1 ((unsigned char *) (packet_buffer + 5), (packet_ptr - packet_buffer - 5) * 4, (unsigned char *) packet_buffer);

  int l = encrypt_packet_buffer ();
  
  clear_packet ();
  out_int (CODE_req_DH_params);
  out_ints ((int *) nonce, 4);
  out_ints ((int *) server_nonce, 4);
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

  c_state = st_reqdh_sent;
  
  return rpc_send_packet (c);
}

int check_DH_params (BIGNUM *p, int g) {
  if (g < 2 || g > 7) { return -1; }
  BIGNUM t;
  BN_init (&t);

  BN_init (&dh_g);
  ensure (BN_set_word (&dh_g, 4 * g));

  ensure (BN_mod (&t, p, &dh_g, BN_ctx));
  int x = BN_get_word (&t);
  assert (x >= 0 && x < 4 * g);

  BN_clear (&dh_g);

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

  if (!BN_is_prime (p, BN_prime_checks, 0, BN_ctx, 0)) { return -1; }

  BIGNUM b;
  BN_init (&b);
  ensure (BN_set_word (&b, 2));
  ensure (BN_div (&t, 0, p, &b, BN_ctx));
  if (!BN_is_prime (&t, BN_prime_checks, 0, BN_ctx, 0)) { return -1; }
  BN_clear (&b);
  BN_clear (&t);
  return 0;
}

int check_g (unsigned char p[256], BIGNUM *g) {
  static unsigned char s[256];
  memset (s, 0, 256);
  assert (BN_num_bytes (g) <= 256);
  BN_bn2bin (g, s);
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
      logprintf ("i = %d (%d %d)\n", i, (int)s[i], (int)p[i]);
      return -1;
    }
  }
  if (!ok) { return -1; }
  return 0;
}

int check_g_bn (BIGNUM *p, BIGNUM *g) {
  static unsigned char s[256];
  memset (s, 0, 256);
  assert (BN_num_bytes (p) <= 256);
  BN_bn2bin (p, s);
  return check_g (s, g);
}

int process_dh_answer (struct connection *c, char *packet, int len) {
  if (verbosity) {
    logprintf ( "process_dh_answer(), len=%d\n", len);
  }
  if (len < 116) {
    logprintf ( "%u * %u = %llu", p1, p2, what);
  }
  assert (len >= 116);
  assert (!*(long long *) packet);
  assert (*(int *) (packet + 16) == len - 20);
  assert (!(len & 3));
  assert (*(int *) (packet + 20) == (int)CODE_server_DH_params_ok);
  assert (!memcmp (packet + 24, nonce, 16));
  assert (!memcmp (packet + 40, server_nonce, 16));
  init_aes_unauth (server_nonce, new_nonce, AES_DECRYPT);
  in_ptr = (int *)(packet + 56);
  in_end = (int *)(packet + len);
  int l = prefetch_strlen ();
  assert (l > 0);
  l = pad_aes_decrypt (fetch_str (l), l, (char *) decrypt_buffer, DECRYPT_BUFFER_INTS * 4 - 16);
  assert (in_ptr == in_end);
  assert (l >= 60);
  assert (decrypt_buffer[5] == (int)CODE_server_DH_inner_data);
  assert (!memcmp (decrypt_buffer + 6, nonce, 16));
  assert (!memcmp (decrypt_buffer + 10, server_nonce, 16));
  int g = decrypt_buffer[14];
  in_ptr = decrypt_buffer + 15;
  in_end = decrypt_buffer + (l >> 2);
  BN_init (&dh_prime);
  BN_init (&g_a);
  assert (fetch_bignum (&dh_prime) > 0);
  assert (fetch_bignum (&g_a) > 0);
  assert (check_g_bn (&dh_prime, &g_a) >= 0);
  int server_time = *in_ptr++;
  assert (in_ptr <= in_end);

  assert (check_DH_params (&dh_prime, g) >= 0);

  static char sha1_buffer[20];
  sha1 ((unsigned char *) decrypt_buffer + 20, (in_ptr - decrypt_buffer - 5) * 4, (unsigned char *) sha1_buffer);
  assert (!memcmp (decrypt_buffer, sha1_buffer, 20));
  assert ((char *) in_end - (char *) in_ptr < 16);

  GET_DC(c)->server_time_delta = server_time - time (0);
  GET_DC(c)->server_time_udelta = server_time - get_utime (CLOCK_MONOTONIC);
  //logprintf ( "server time is %d, delta = %d\n", server_time, server_time_delta);

  // Build set_client_DH_params answer
  clear_packet ();
  packet_ptr += 5;
  out_int (CODE_client_DH_inner_data);
  out_ints ((int *) nonce, 4);
  out_ints ((int *) server_nonce, 4);
  out_long (0LL);
  
  BN_init (&dh_g);
  ensure (BN_set_word (&dh_g, g));

  secure_random (s_power, 256);
  BIGNUM *dh_power = BN_bin2bn ((unsigned char *)s_power, 256, 0);
  ensure_ptr (dh_power);

  BIGNUM *y = BN_new ();
  ensure_ptr (y);
  ensure (BN_mod_exp (y, &dh_g, dh_power, &dh_prime, BN_ctx));
  out_bignum (y);
  BN_free (y);

  BN_init (&auth_key_num);
  ensure (BN_mod_exp (&auth_key_num, &g_a, dh_power, &dh_prime, BN_ctx));
  l = BN_num_bytes (&auth_key_num);
  assert (l >= 250 && l <= 256);
  assert (BN_bn2bin (&auth_key_num, (unsigned char *)GET_DC(c)->auth_key));
  memset (GET_DC(c)->auth_key + l, 0, 256 - l);
  BN_free (dh_power);
  BN_free (&auth_key_num);
  BN_free (&dh_g);
  BN_free (&g_a);
  BN_free (&dh_prime);

  //hexdump (auth_key, auth_key + 256);
 
  sha1 ((unsigned char *) (packet_buffer + 5), (packet_ptr - packet_buffer - 5) * 4, (unsigned char *) packet_buffer);

  //hexdump ((char *)packet_buffer, (char *)packet_ptr);

  l = encrypt_packet_buffer_aes_unauth (server_nonce, new_nonce);

  clear_packet ();
  out_int (CODE_set_client_DH_params);
  out_ints ((int *) nonce, 4);
  out_ints ((int *) server_nonce, 4);
  out_cstring ((char *) encrypt_buffer, l);

  c_state = st_client_dh_sent;

  return rpc_send_packet (c);
}


int process_auth_complete (struct connection *c UU, char *packet, int len) {
  if (verbosity) {
    logprintf ( "process_dh_answer(), len=%d\n", len);
  }
  assert (len == 72);
  assert (!*(long long *) packet);
  assert (*(int *) (packet + 16) == len - 20);
  assert (!(len & 3));
  assert (*(int *) (packet + 20) == CODE_dh_gen_ok);
  assert (!memcmp (packet + 24, nonce, 16));
  assert (!memcmp (packet + 40, server_nonce, 16));
  static unsigned char tmp[44], sha1_buffer[20];
  memcpy (tmp, new_nonce, 32);
  tmp[32] = 1;
  //GET_DC(c)->auth_key_id = *(long long *)(sha1_buffer + 12);

  bl_do_set_auth_key_id (GET_DC(c)->id, (unsigned char *)GET_DC(c)->auth_key);
  sha1 ((unsigned char *)GET_DC(c)->auth_key, 256, sha1_buffer);

  memcpy (tmp + 33, sha1_buffer, 8);
  sha1 (tmp, 41, sha1_buffer);
  assert (!memcmp (packet + 56, sha1_buffer + 4, 16));
  GET_DC(c)->server_salt = *(long long *)server_nonce ^ *(long long *)new_nonce;
  
  if (verbosity >= 3) {
    logprintf ( "auth_key_id=%016llx\n", GET_DC(c)->auth_key_id);
  }
  //kprintf ("OK\n");

  //c->status = conn_error;
  //sleep (1);

  c_state = st_authorized;
  //return 1;
  if (verbosity) {
    logprintf ( "Auth success\n");
  }
  auth_success ++;
  GET_DC(c)->flags |= 1;
  write_auth_file ();
  
  return 1;
}

/*
 *
 *                AUTHORIZED (MAIN) PROTOCOL PART
 *
 */

struct encrypted_message enc_msg;

long long client_last_msg_id, server_last_msg_id;

double get_server_time (struct dc *DC) {
  if (!DC->server_time_udelta) {
    DC->server_time_udelta = get_utime (CLOCK_REALTIME) - get_utime (CLOCK_MONOTONIC);
  }
  return get_utime (CLOCK_MONOTONIC) + DC->server_time_udelta;
}

long long generate_next_msg_id (struct dc *DC) {
  long long next_id = (long long) (get_server_time (DC) * (1LL << 32)) & -4;
  if (next_id <= client_last_msg_id) {
    next_id = client_last_msg_id += 4;
  } else {
    client_last_msg_id = next_id;
  }
  return next_id;
}

void init_enc_msg (struct session *S, int useful) {
  struct dc *DC = S->dc;
  assert (DC->auth_key_id);
  enc_msg.auth_key_id = DC->auth_key_id;
//  assert (DC->server_salt);
  enc_msg.server_salt = DC->server_salt;
  if (!S->session_id) {
    secure_random (&S->session_id, 8);
  }
  enc_msg.session_id = S->session_id;
  //enc_msg.auth_key_id2 = auth_key_id;
  enc_msg.msg_id = generate_next_msg_id (DC);
  //enc_msg.msg_id -= 0x10000000LL * (lrand48 () & 15);
  //kprintf ("message id %016llx\n", enc_msg.msg_id);
  enc_msg.seq_no = S->seq_no;
  if (useful) {
    enc_msg.seq_no |= 1;
  }
  S->seq_no += 2;
};

int aes_encrypt_message (struct dc *DC, struct encrypted_message *enc) {
  unsigned char sha1_buffer[20];
  const int MINSZ = offsetof (struct encrypted_message, message);
  const int UNENCSZ = offsetof (struct encrypted_message, server_salt);
  int enc_len = (MINSZ - UNENCSZ) + enc->msg_len;
  assert (enc->msg_len >= 0 && enc->msg_len <= MAX_MESSAGE_INTS * 4 - 16 && !(enc->msg_len & 3));
  sha1 ((unsigned char *) &enc->server_salt, enc_len, sha1_buffer);
  //printf ("enc_len is %d\n", enc_len);
  if (verbosity >= 2) {
    logprintf ( "sending message with sha1 %08x\n", *(int *)sha1_buffer);
  }
  memcpy (enc->msg_key, sha1_buffer + 4, 16);
  init_aes_auth (DC->auth_key, enc->msg_key, AES_ENCRYPT);
  //hexdump ((char *)enc, (char *)enc + enc_len + 24);
  return pad_aes_encrypt ((char *) &enc->server_salt, enc_len, (char *) &enc->server_salt, MAX_MESSAGE_INTS * 4 + (MINSZ - UNENCSZ));
}

long long encrypt_send_message (struct connection *c, int *msg, int msg_ints, int useful) {
  struct dc *DC = GET_DC(c);
  struct session *S = c->session;
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
  init_enc_msg (S, useful);

  //hexdump ((char *)msg, (char *)msg + (msg_ints * 4));
  int l = aes_encrypt_message (DC, &enc_msg);
  //hexdump ((char *)&enc_msg, (char *)&enc_msg + l  + 24);
  assert (l > 0);
  rpc_send_message (c, &enc_msg, l + UNENCSZ);
  
  return client_last_msg_id;
}

int longpoll_count, good_messages;

int auth_work_start (struct connection *c UU) {
  return 1;
}

void rpc_execute_answer (struct connection *c, long long msg_id UU);

int unread_messages;
int our_id;
int pts;
int qts;
int last_date;
int seq;

void fetch_pts (void) {
  int p = fetch_int ();
  if (p <= pts) { return; }
  if (p != pts + 1) {
    if (pts) {
      //logprintf ("Hole in pts p = %d, pts = %d\n", p, pts);

      // get difference should be here
      pts = p;
    } else {
      pts = p;
    }
  } else {
    pts ++;
  }
  bl_do_set_pts (pts);
}

void fetch_qts (void) {
  int p = fetch_int ();
  if (p <= qts) { return; }
  if (p != qts + 1) {
    if (qts) {
      //logprintf ("Hole in qts\n");
      // get difference should be here
      qts = p;
    } else {
      qts = p;
    }
  } else {
    qts ++;
  }
  bl_do_set_qts (qts);
}

void fetch_date (void) {
  int p = fetch_int ();
  if (p > last_date) {
    last_date = p;
    bl_do_set_date (last_date);
  }
}

void fetch_seq (void) {
  int x = fetch_int ();
  if (x > seq + 1) {
    logprintf ("Hole in seq: seq = %d, x = %d\n", seq, x);
    //do_get_difference ();
    //seq = x;
  } else if (x == seq + 1) {
    seq = x;
    bl_do_set_seq (seq);
  }
}

void work_update_binlog (void) {
  unsigned op = fetch_int ();
  switch (op) {
  case CODE_update_user_name:
    {
      peer_id_t user_id = MK_USER (fetch_int ());
      peer_t *UC = user_chat_get (user_id);
      if (UC) {
        struct user *U = &UC->user;
        if (U->first_name) { tfree_str (U->first_name); }
        if (U->last_name) { tfree_str (U->last_name); }
        if (U->print_name) { tfree_str (U->print_name); }
        U->first_name = fetch_str_dup ();
        U->last_name = fetch_str_dup ();
        U->print_name = create_print_name (U->id, U->first_name, U->last_name, 0, 0);
      } else {
        fetch_skip_str ();
        fetch_skip_str ();
      }
    }
    break;
  case CODE_update_user_photo:
    {
      peer_id_t user_id = MK_USER (fetch_int ());
      peer_t *UC = user_chat_get (user_id);
      fetch_date ();
      if (UC) {
        struct user *U = &UC->user;
        
        unsigned y = fetch_int ();
        if (y == CODE_user_profile_photo_empty) {
          U->photo_id = 0;
          U->photo_big.dc = -2;
          U->photo_small.dc = -2;
        } else {
          assert (y == CODE_user_profile_photo);
          U->photo_id = fetch_long ();
          fetch_file_location (&U->photo_small);
          fetch_file_location (&U->photo_big);
        }
      } else {
        struct file_location t;
        unsigned y = fetch_int ();
        if (y == CODE_user_profile_photo_empty) {
        } else {
          assert (y == CODE_user_profile_photo);
          fetch_long (); // photo_id
          fetch_file_location (&t);
          fetch_file_location (&t);
        }
      }
      fetch_bool ();
    }
    break;
  default:
    assert (0);
  }
}

void work_update (struct connection *c UU, long long msg_id UU) {
  unsigned op = fetch_int ();
  switch (op) {
  case CODE_update_new_message:
    {
      struct message *M = fetch_alloc_message ();
      assert (M);
      fetch_pts ();
      unread_messages ++;
      print_message (M);
      update_prompt ();
      break;
    };
  case CODE_update_message_i_d:
    {
      int id = fetch_int (); // id
      int new = fetch_long (); // random_id
      struct message *M = message_get (new);
      if (M) {
        bl_do_set_msg_id (M, id);
      }
    }
    break;
  case CODE_update_read_messages:
    {
      assert (fetch_int () == (int)CODE_vector);
      int n = fetch_int ();
      int i;
      for (i = 0; i < n; i++) {
        int id = fetch_int ();
        struct message *M = message_get (id);
        if (M) {
          bl_do_set_unread (M, 0);
        }
      }
      fetch_pts ();
      if (log_level >= 1) {
        print_start ();
        push_color (COLOR_YELLOW);
        print_date (time (0));
        printf (" %d messages marked as read\n", n);
        pop_color ();
        print_end ();
      }
    }
    break;
  case CODE_update_user_typing:
    {
      peer_id_t id = MK_USER (fetch_int ());
      peer_t *U = user_chat_get (id);
      if (log_level >= 2) {
        print_start ();
        push_color (COLOR_YELLOW);
        print_date (time (0));
        printf (" User ");
        print_user_name (id, U);
        printf (" is typing....\n");
        pop_color ();
        print_end ();
      }
    }
    break;
  case CODE_update_chat_user_typing:
    {
      peer_id_t chat_id = MK_CHAT (fetch_int ());
      peer_id_t id = MK_USER (fetch_int ());
      peer_t *C = user_chat_get (chat_id);
      peer_t *U = user_chat_get (id);
      if (log_level >= 2) {
        print_start ();
        push_color (COLOR_YELLOW);
        print_date (time (0));
        printf (" User ");
        print_user_name (id, U);
        printf (" is typing in chat ");
        print_chat_name (chat_id, C);
        printf ("....\n");
        pop_color ();
        print_end ();
      }
    }
    break;
  case CODE_update_user_status:
    {
      peer_id_t user_id = MK_USER (fetch_int ());
      peer_t *U = user_chat_get (user_id);
      if (U) {
        fetch_user_status (&U->user.status);
        if (log_level >= 3) {
          print_start ();
          push_color (COLOR_YELLOW);
          print_date (time (0));
          printf (" User ");
          print_user_name (user_id, U);
          printf (" is now ");
          printf ("%s\n", (U->user.status.online > 0) ? "online" : "offline");
          pop_color ();
          print_end ();
        }
      } else {
        struct user_status t;
        fetch_user_status (&t);
      }
    }
    break;
  case CODE_update_user_name:
    {
      peer_id_t user_id = MK_USER (fetch_int ());
      peer_t *UC = user_chat_get (user_id);
      if (UC && (UC->flags & FLAG_CREATED)) {
        int l1 = prefetch_strlen ();
        char *f = fetch_str (l1);
        int l2 = prefetch_strlen ();
        char *l = fetch_str (l2);
        struct user *U = &UC->user;
        bl_do_set_user_real_name (U, f, l1, l, l2);
        print_start ();
        push_color (COLOR_YELLOW);
        print_date (time (0));
        printf (" User ");
        print_user_name (user_id, UC);
        printf (" changed name to ");
        print_user_name (user_id, UC);
        printf ("\n");
        pop_color ();
        print_end ();
      } else {
        fetch_skip_str ();
        fetch_skip_str ();
      }
    }
    break;
  case CODE_update_user_photo:
    {
      peer_id_t user_id = MK_USER (fetch_int ());
      peer_t *UC = user_chat_get (user_id);
      fetch_date ();
      if (UC && (UC->flags & FLAG_CREATED)) {
        struct user *U = &UC->user;
        unsigned y = fetch_int ();
        long long photo_id;
        struct file_location big;
        struct file_location small;
        memset (&big, 0, sizeof (big));
        memset (&small, 0, sizeof (small));
        if (y == CODE_user_profile_photo_empty) {
          photo_id = 0;
          big.dc = -2;
          small.dc = -2;
        } else {
          assert (y == CODE_user_profile_photo);
          photo_id = fetch_long ();
          fetch_file_location (&small);
          fetch_file_location (&big);
        }
        bl_do_set_user_profile_photo (U, photo_id, &big, &small);
        
        print_start ();
        push_color (COLOR_YELLOW);
        print_date (time (0));
        printf (" User ");
        print_user_name (user_id, UC);
        printf (" updated profile photo\n");
        pop_color ();
        print_end ();
      } else {
        struct file_location t;
        unsigned y = fetch_int ();
        if (y == CODE_user_profile_photo_empty) {
        } else {
          assert (y == CODE_user_profile_photo);
          fetch_long (); // photo_id
          fetch_file_location (&t);
          fetch_file_location (&t);
        }
      }
      fetch_bool ();
    }
    break;
  case CODE_update_restore_messages:
    {
      assert (fetch_int () == CODE_vector);
      int n = fetch_int ();
      print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" Restored %d messages\n", n);
      pop_color ();
      print_end ();
      fetch_skip (n);
      fetch_pts ();
    }
    break;
  case CODE_update_delete_messages:
    {
      assert (fetch_int () == CODE_vector);
      int n = fetch_int ();
      print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" Deleted %d messages\n", n);
      pop_color ();
      print_end ();
      fetch_skip (n);
      fetch_pts ();
    }
    break;
  case CODE_update_chat_participants:
    {
      unsigned x = fetch_int ();
      assert (x == CODE_chat_participants || x == CODE_chat_participants_forbidden);
      peer_id_t chat_id = MK_CHAT (fetch_int ());
      int n = 0;
      peer_t *C = user_chat_get (chat_id);
      if (C && (C->flags & FLAG_CREATED)) {
        if (x == CODE_chat_participants) {
          bl_do_set_chat_admin (&C->chat, fetch_int ());
          assert (fetch_int () == CODE_vector);
          n = fetch_int ();
          struct chat_user *users = talloc (12 * n);
          int i;
          for (i = 0; i < n; i++) {
            assert (fetch_int () == (int)CODE_chat_participant);
            users[i].user_id = fetch_int ();
            users[i].inviter_id = fetch_int ();
            users[i].date = fetch_int ();
          }
          int version = fetch_int (); 
          bl_do_set_chat_participants (&C->chat, version, n, users);
        }
      } else {
        if (x == CODE_chat_participants) {
          fetch_int (); // admin_id
          assert (fetch_int () == CODE_vector);
          n = fetch_int ();
          fetch_skip (n * 4);
          fetch_int (); // version
        }
      }
      print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" Chat ");
      print_chat_name (chat_id, C);
      if (x == CODE_chat_participants) {
        printf (" changed list: now %d members\n", n);
      } else {
        printf (" changed list, but we are forbidden to know about it (Why this update even was sent to us?\n");
      }
      pop_color ();
      print_end ();
    }
    break;
  case CODE_update_contact_registered:
    {
      peer_id_t user_id = MK_USER (fetch_int ());
      peer_t *U = user_chat_get (user_id);
      fetch_int (); // date
      print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" User ");
      print_user_name (user_id, U);
      printf (" registered\n");
      pop_color ();
      print_end ();
    }
    break;
  case CODE_update_contact_link:
    {
      peer_id_t user_id = MK_USER (fetch_int ());
      peer_t *U = user_chat_get (user_id);
      print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" Updated link with user ");
      print_user_name (user_id, U);
      printf ("\n");
      pop_color ();
      print_end ();
      unsigned t = fetch_int ();
      assert (t == CODE_contacts_my_link_empty || t == CODE_contacts_my_link_requested || t == CODE_contacts_my_link_contact);
      if (t == CODE_contacts_my_link_requested) {
        fetch_bool (); // has_phone
      }
      t = fetch_int ();
      assert (t == CODE_contacts_foreign_link_unknown || t == CODE_contacts_foreign_link_requested || t == CODE_contacts_foreign_link_mutual);
      if (t == CODE_contacts_foreign_link_requested) {
        fetch_bool (); // has_phone
      }
    }
    break;
  case CODE_update_activation:
    {
      peer_id_t user_id = MK_USER (fetch_int ());
      peer_t *U = user_chat_get (user_id);
      print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" User ");
      print_user_name (user_id, U);
      printf (" activated\n");
      pop_color ();
      print_end ();
    }
    break;
  case CODE_update_new_authorization:
    {
      fetch_long (); // auth_key_id
      fetch_int (); // date
      char *s = fetch_str_dup ();
      char *location = fetch_str_dup ();
      print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" New autorization: device='%s' location='%s'\n",
        s, location);
      pop_color ();
      print_end ();
      tfree_str (s);
      tfree_str (location);
    }
    break;
  case CODE_update_new_geo_chat_message:
    {
      struct message *M = fetch_alloc_geo_message ();
      unread_messages ++;
      print_message (M);
      update_prompt ();
    }
    break;
  case CODE_update_new_encrypted_message:
    {
      struct message *M = fetch_alloc_encrypted_message ();
      unread_messages ++;
      print_message (M);
      update_prompt ();
      fetch_qts ();
    }
    break;
  case CODE_update_encryption:
    {
      struct secret_chat *E = fetch_alloc_encrypted_chat ();
      if (verbosity >= 2) {
        logprintf ("Secret chat state = %d\n", E->state);
      }
      print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      switch (E->state) {
      case sc_none:
        break;
      case sc_waiting:
        printf (" Encrypted chat ");
        print_encr_chat_name (E->id, (void *)E);
        printf (" is now in wait state\n");
        break;
      case sc_request:
        printf (" Encrypted chat ");
        print_encr_chat_name (E->id, (void *)E);
        printf (" is now in request state. Sending request ok\n");
        break;
      case sc_ok:
        printf (" Encrypted chat ");
        print_encr_chat_name (E->id, (void *)E);
        printf (" is now in ok state\n");
        break;
      case sc_deleted:
        printf (" Encrypted chat ");
        print_encr_chat_name (E->id, (void *)E);
        printf (" is now in deleted state\n");
        break;
      }
      pop_color ();
      print_end ();
      if (E->state == sc_request && !disable_auto_accept) {
        do_accept_encr_chat_request (E);
      }
      fetch_int (); // date
    }
    break;
  case CODE_update_encrypted_chat_typing:
    {
      peer_id_t id = MK_ENCR_CHAT (fetch_int ());
      peer_t *P = user_chat_get (id);
      print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      if (P) {
        printf (" User ");
        peer_id_t user_id = MK_USER (P->encr_chat.user_id);
        print_user_name (user_id, user_chat_get (user_id));
        printf (" typing in secret chat ");
        print_encr_chat_name (id, P);
        printf ("\n");
      } else {
        printf (" Some user is typing in unknown secret chat\n");
      }
      pop_color ();
      print_end ();
    }
    break;
  case CODE_update_encrypted_messages_read:
    {
      peer_id_t id = MK_ENCR_CHAT (fetch_int ()); // chat_id
      fetch_int (); // max_date
      fetch_int (); // date
      peer_t *P = user_chat_get (id);
      int x = -1;
      if (P && P->last) {
        x = 0;
        struct message *M = P->last;
        while (M && (!M->out || M->unread)) {
          if (M->out) {
            M->unread = 0;
            x ++;
          }
          M = M->next;
        }
      }
      if (log_level >= 1) {
        print_start ();
        push_color (COLOR_YELLOW);
        print_date (time (0));
        printf (" Encrypted chat ");
        print_encr_chat_name_full (id, user_chat_get (id));
        printf (": %d messages marked read \n", x);
        pop_color ();
        print_end ();
      }
    }
    break;
  case CODE_update_chat_participant_add:
    {
      peer_id_t chat_id = MK_CHAT (fetch_int ());
      peer_id_t user_id = MK_USER (fetch_int ());
      peer_id_t inviter_id = MK_USER (fetch_int ());
      int  version = fetch_int (); 
      
      peer_t *C = user_chat_get (chat_id);
      if (C && (C->flags & FLAG_CREATED)) {
        bl_do_chat_add_user (&C->chat, version, get_peer_id (user_id), get_peer_id (inviter_id), time (0));
      }

      print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" Chat ");
      print_chat_name (chat_id, user_chat_get (chat_id));
      printf (": user ");
      print_user_name (user_id, user_chat_get (user_id));
      printf (" added by user ");
      print_user_name (inviter_id, user_chat_get (inviter_id));
      printf ("\n");
      pop_color ();
      print_end ();
    }
    break;
  case CODE_update_chat_participant_delete:
    {
      peer_id_t chat_id = MK_CHAT (fetch_int ());
      peer_id_t user_id = MK_USER (fetch_int ());
      int version = fetch_int ();
      
      peer_t *C = user_chat_get (chat_id);
      if (C && (C->flags & FLAG_CREATED)) {
        bl_do_chat_del_user (&C->chat, version, get_peer_id (user_id));
      }

      print_start ();
      push_color (COLOR_YELLOW);
      print_date (time (0));
      printf (" Chat ");
      print_chat_name (chat_id, user_chat_get (chat_id));
      printf (": user ");
      print_user_name (user_id, user_chat_get (user_id));
      printf (" deleted\n");
      pop_color ();
      print_end ();
    }
    break;
  default:
    logprintf ("Unknown update type %08x\n", op);
    ;
  }
}

void work_update_short (struct connection *c, long long msg_id) {
  assert (fetch_int () == CODE_update_short);
  work_update (c, msg_id);
  fetch_date ();
}

void work_updates (struct connection *c, long long msg_id) {
  assert (fetch_int () == CODE_updates);
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  int i;
  for (i = 0; i < n; i++) {
    work_update (c, msg_id);
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_user ();
  }
  assert (fetch_int () == CODE_vector);
  n = fetch_int ();
  for (i = 0; i < n; i++) {
    fetch_alloc_chat ();
  }
  bl_do_set_date (fetch_int ());
  bl_do_set_seq (fetch_int ());
}

void work_update_short_message (struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == (int)CODE_update_short_message);
  struct message *M = fetch_alloc_message_short ();  
  unread_messages ++;
  print_message (M);
  update_prompt ();
  if (M->date > last_date) {
    last_date = M->date;
  }
}

void work_update_short_chat_message (struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == CODE_update_short_chat_message);
  struct message *M = fetch_alloc_message_short_chat ();  
  unread_messages ++;
  print_message (M);
  update_prompt ();
  if (M->date > last_date) {
    last_date = M->date;
  }
}

void work_container (struct connection *c, long long msg_id UU) {
  if (verbosity) {
    logprintf ( "work_container: msg_id = %lld\n", msg_id);
  }
  assert (fetch_int () == CODE_msg_container);
  int n = fetch_int ();
  int i;
  for (i = 0; i < n; i++) {
    long long id = fetch_long (); 
    //int seqno = fetch_int (); 
    fetch_int (); // seq_no
    if (id & 1) {
      insert_msg_id (c->session, id);
    }
    int bytes = fetch_int ();
    int *t = in_end;
    in_end = in_ptr + (bytes / 4);
    rpc_execute_answer (c, id);
    assert (in_ptr == in_end);
    in_end = t;
  }
}

void work_new_session_created (struct connection *c, long long msg_id UU) {
  if (verbosity) {
    logprintf ( "work_new_session_created: msg_id = %lld\n", msg_id);
  }
  assert (fetch_int () == (int)CODE_new_session_created);
  fetch_long (); // first message id
  //DC->session_id = fetch_long ();
  fetch_long (); // unique_id
  GET_DC(c)->server_salt = fetch_long ();
  
}

void work_msgs_ack (struct connection *c UU, long long msg_id UU) {
  if (verbosity) {
    logprintf ( "work_msgs_ack: msg_id = %lld\n", msg_id);
  }
  assert (fetch_int () == CODE_msgs_ack);
  assert (fetch_int () == CODE_vector);
  int n = fetch_int ();
  int i;
  for (i = 0; i < n; i++) {
    long long id = fetch_long ();
    if (verbosity) {
      logprintf ("ack for %lld\n", id);
    }
    query_ack (id);
  }
}

void work_rpc_result (struct connection *c UU, long long msg_id UU) {
  if (verbosity) {
    logprintf ( "work_rpc_result: msg_id = %lld\n", msg_id);
  }
  assert (fetch_int () == (int)CODE_rpc_result);
  long long id = fetch_long ();
  int op = prefetch_int ();
  if (op == CODE_rpc_error) {
    query_error (id);
  } else {
    query_result (id);
  }
}

#define MAX_PACKED_SIZE (1 << 24)
void work_packed (struct connection *c, long long msg_id) {
  assert (fetch_int () == CODE_gzip_packed);
  static int in_gzip;
  static int buf[MAX_PACKED_SIZE >> 2];
  assert (!in_gzip);
  in_gzip = 1;
    
  int l = prefetch_strlen ();
  char *s = fetch_str (l);

  int total_out = tinflate (s, l, buf, MAX_PACKED_SIZE);
  int *end = in_ptr;
  int *eend = in_end;
  //assert (total_out % 4 == 0);
  in_ptr = buf;
  in_end = in_ptr + total_out / 4;
  if (verbosity >= 4) {
    logprintf ( "Unzipped data: ");
    hexdump_in ();
  }
  rpc_execute_answer (c, msg_id);
  in_ptr = end;
  in_end = eend;
  in_gzip = 0;
}

void work_bad_server_salt (struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == (int)CODE_bad_server_salt);
  long long id = fetch_long ();
  query_restart (id);
  fetch_int (); // seq_no
  fetch_int (); // error_code
  long long new_server_salt = fetch_long ();
  GET_DC(c)->server_salt = new_server_salt;
}

void work_pong (struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == CODE_pong);
  fetch_long (); // msg_id
  fetch_long (); // ping_id
}

void work_detailed_info (struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == CODE_msg_detailed_info);
  fetch_long (); // msg_id
  fetch_long (); // answer_msg_id
  fetch_int (); // bytes
  fetch_int (); // status
}

void work_new_detailed_info (struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == (int)CODE_msg_new_detailed_info);
  fetch_long (); // answer_msg_id
  fetch_int (); // bytes
  fetch_int (); // status
}

void work_updates_to_long (struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == (int)CODE_updates_too_long);
  logprintf ("updates to long... Getting difference\n");
  do_get_difference ();
}

void work_bad_msg_notification (struct connection *c UU, long long msg_id UU) {
  assert (fetch_int () == (int)CODE_bad_msg_notification);
  long long m1 = fetch_long ();
  int s = fetch_int ();
  int e = fetch_int ();
  logprintf ("bad_msg_notification: msg_id = %lld, seq = %d, error = %d\n", m1, s, e);
}

void rpc_execute_answer (struct connection *c, long long msg_id UU) {
  if (verbosity >= 5) {
    logprintf ("rpc_execute_answer: fd=%d\n", c->fd);
    hexdump_in ();
  }
  int op = prefetch_int ();
  switch (op) {
  case CODE_msg_container:
    work_container (c, msg_id);
    return;
  case CODE_new_session_created:
    work_new_session_created (c, msg_id);
    return;
  case CODE_msgs_ack:
    work_msgs_ack (c, msg_id);
    return;
  case CODE_rpc_result:
    work_rpc_result (c, msg_id);
    return;
  case CODE_update_short:
    work_update_short (c, msg_id);
    return;
  case CODE_updates:
    work_updates (c, msg_id);
    return;
  case CODE_update_short_message:
    work_update_short_message (c, msg_id);
    return;
  case CODE_update_short_chat_message:
    work_update_short_chat_message (c, msg_id);
    return;
  case CODE_gzip_packed:
    work_packed (c, msg_id);
    return;
  case CODE_bad_server_salt:
    work_bad_server_salt (c, msg_id);
    return;
  case CODE_pong:
    work_pong (c, msg_id);
    return;
  case CODE_msg_detailed_info:
    work_detailed_info (c, msg_id);
    return;
  case CODE_msg_new_detailed_info:
    work_new_detailed_info (c, msg_id);
    return;
  case CODE_updates_too_long:
    work_updates_to_long (c, msg_id);
    return;
  case CODE_bad_msg_notification:
    work_bad_msg_notification (c, msg_id);
    return;
  }
  logprintf ( "Unknown message: \n");
  hexdump_in ();
  in_ptr = in_end; // Will not fail due to assertion in_ptr == in_end
}

int process_rpc_message (struct connection *c UU, struct encrypted_message *enc, int len) {
  const int MINSZ = offsetof (struct encrypted_message, message);
  const int UNENCSZ = offsetof (struct encrypted_message, server_salt);
  if (verbosity) {
    logprintf ( "process_rpc_message(), len=%d\n", len);  
  }
  assert (len >= MINSZ && (len & 15) == (UNENCSZ & 15));
  struct dc *DC = GET_DC(c);
  assert (enc->auth_key_id == DC->auth_key_id);
  assert (DC->auth_key_id);
  init_aes_auth (DC->auth_key + 8, enc->msg_key, AES_DECRYPT);
  int l = pad_aes_decrypt ((char *)&enc->server_salt, len - UNENCSZ, (char *)&enc->server_salt, len - UNENCSZ);
  assert (l == len - UNENCSZ);
  //assert (enc->auth_key_id2 == enc->auth_key_id);
  assert (!(enc->msg_len & 3) && enc->msg_len > 0 && enc->msg_len <= len - MINSZ && len - MINSZ - enc->msg_len <= 12);
  static unsigned char sha1_buffer[20];
  sha1 ((void *)&enc->server_salt, enc->msg_len + (MINSZ - UNENCSZ), sha1_buffer);
  assert (!memcmp (&enc->msg_key, sha1_buffer + 4, 16));
  //assert (enc->server_salt == server_salt); //in fact server salt can change
  if (DC->server_salt != enc->server_salt) {
    DC->server_salt = enc->server_salt;
    write_auth_file ();
  }
  
  int this_server_time = enc->msg_id >> 32LL;
  if (!DC->server_time_delta) {
    DC->server_time_delta = this_server_time - get_utime (CLOCK_REALTIME);
    DC->server_time_udelta = this_server_time - get_utime (CLOCK_MONOTONIC);
  }
  double st = get_server_time (DC);
  if (this_server_time < st - 300 || this_server_time > st + 30) {
    logprintf ("salt = %lld, session_id = %lld, msg_id = %lld, seq_no = %d, st = %lf, now = %lf\n", enc->server_salt, enc->session_id, enc->msg_id, enc->seq_no, st, get_utime (CLOCK_REALTIME));
    in_ptr = enc->message;
    in_end = in_ptr + (enc->msg_len / 4);
    hexdump_in ();
  }

  assert (this_server_time >= st - 300 && this_server_time <= st + 30);
  //assert (enc->msg_id > server_last_msg_id && (enc->msg_id & 3) == 1);
  if (verbosity >= 1) {
    logprintf ( "received mesage id %016llx\n", enc->msg_id);
    hexdump_in ();
  }
  server_last_msg_id = enc->msg_id;

  //*(long long *)(longpoll_query + 3) = *(long long *)((char *)(&enc->msg_id) + 0x3c);
  //*(long long *)(longpoll_query + 5) = *(long long *)((char *)(&enc->msg_id) + 0x3c);

  assert (l >= (MINSZ - UNENCSZ) + 8);
  //assert (enc->message[0] == CODE_rpc_result && *(long long *)(enc->message + 1) == client_last_msg_id);
  ++good_messages;
  
  in_ptr = enc->message;
  in_end = in_ptr + (enc->msg_len / 4);
 
  if (enc->msg_id & 1) {
    insert_msg_id (c->session, enc->msg_id);
  }
  assert (c->session->session_id == enc->session_id);
  rpc_execute_answer (c, enc->msg_id);
  assert (in_ptr == in_end);
  return 0;
}


int rpc_execute (struct connection *c, int op, int len) {
  if (verbosity) {
    logprintf ( "outbound rpc connection #%d : received rpc answer %d with %d content bytes\n", c->fd, op, len);
  }
/*  if (op < 0) {
    assert (read_in (c, Response, Response_len) == Response_len);
    return 0;
  }*/

  if (len >= MAX_RESPONSE_SIZE/* - 12*/ || len < 0/*12*/) {
    logprintf ( "answer too long (%d bytes), skipping\n", len);
    return 0;
  }

  int Response_len = len;

  if (verbosity >= 2) {
    logprintf ("Response_len = %d\n", Response_len);
  }
  assert (read_in (c, Response, Response_len) == Response_len);
  Response[Response_len] = 0;
  if (verbosity >= 2) {
    logprintf ( "have %d Response bytes\n", Response_len);
  }

#if !defined(__MACH__) && !defined(__FreeBSD__)
  setsockopt (c->fd, IPPROTO_TCP, TCP_QUICKACK, (int[]){0}, 4);
#endif
  int o = c_state;
  if (GET_DC(c)->flags & 1) { o = st_authorized;}
  switch (o) {
  case st_reqpq_sent:
    process_respq_answer (c, Response/* + 8*/, Response_len/* - 12*/);
#if !defined(__MACH__) && !defined(__FreeBSD__)
    setsockopt (c->fd, IPPROTO_TCP, TCP_QUICKACK, (int[]){0}, 4);
#endif
    return 0;
  case st_reqdh_sent:
    process_dh_answer (c, Response/* + 8*/, Response_len/* - 12*/);
#if !defined(__MACH__) && !defined(__FreeBSD__)
    setsockopt (c->fd, IPPROTO_TCP, TCP_QUICKACK, (int[]){0}, 4);
#endif
    return 0;
  case st_client_dh_sent:
    process_auth_complete (c, Response/* + 8*/, Response_len/* - 12*/);
#if !defined(__MACH__) && !defined(__FreeBSD__)
    setsockopt (c->fd, IPPROTO_TCP, TCP_QUICKACK, (int[]){0}, 4);
#endif
    return 0;
  case st_authorized:
    if (op < 0 && op >= -999) {
      logprintf ("Server error %d\n", op);
    } else {
      process_rpc_message (c, (void *)(Response/* + 8*/), Response_len/* - 12*/);
    }
#if !defined(__MACH__) && !defined(__FreeBSD__)
    setsockopt (c->fd, IPPROTO_TCP, TCP_QUICKACK, (int[]){0}, 4);
#endif
    return 0;
  default:
    logprintf ( "fatal: cannot receive answer in state %d\n", c_state);
    exit (2);
  }
 
  return 0;
}


int tc_close (struct connection *c, int who) {
  if (verbosity) {
    logprintf ( "outbound http connection #%d : closing by %d\n", c->fd, who);
  }
  return 0;
}

int tc_becomes_ready (struct connection *c) {
  if (verbosity) {
    logprintf ( "outbound connection #%d becomes ready\n", c->fd);
  }
  char byte = 0xef;
  assert (write_out (c, &byte, 1) == 1);
  flush_out (c);
  
#if !defined(__MACH__) && !defined(__FreeBSD__)
  setsockopt (c->fd, IPPROTO_TCP, TCP_QUICKACK, (int[]){0}, 4);
#endif
  int o = c_state;
  if (GET_DC(c)->flags & 1) { o = st_authorized; }
  switch (o) {
  case st_init:
    send_req_pq_packet (c);
    break;
  case st_authorized:
    auth_work_start (c);
    break;
  default:
    logprintf ( "c_state = %d\n", c_state);
    assert (0);
  }
  return 0;
}

int rpc_becomes_ready (struct connection *c) {
  return tc_becomes_ready (c);
}

int rpc_close (struct connection *c) {
  return tc_close (c, 0);
}

int auth_is_success (void) {
  return auth_success;
}

void on_start (void) {
  prng_seed (0, 0);

  if (rsa_load_public_key (rsa_public_key_name) < 0) {
    perror ("rsa_load_public_key");
    exit (1);
  }
  if (verbosity) {
    logprintf ( "public key '%s' loaded successfully\n", rsa_public_key_name);
  }
  pk_fingerprint = compute_rsa_key_fingerprint (pubKey);
}

int auth_ok (void) {
  return auth_success;
}

void dc_authorize (struct dc *DC) {
  c_state = 0;
  auth_success = 0;
  if (!DC->sessions[0]) {
    dc_create_session (DC);
  }
  if (verbosity) {
    logprintf ( "Starting authorization for DC #%d: %s:%d\n", DC->id, DC->ip, DC->port);
  }
  net_loop (0, auth_ok);
}
