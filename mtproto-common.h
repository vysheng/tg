#ifndef __MTPROTO_COMMON_H__
#define __MTPROTO_COMMON_H__

#include <string.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <openssl/aes.h>
#include <stdio.h>

/* DH key exchange protocol data structures */
#define	CODE_req_pq			0x60469778
#define CODE_resPQ			0x05162463
#define CODE_req_DH_params		0xd712e4be
#define CODE_p_q_inner_data		0x83c95aec
#define CODE_server_DH_inner_data	0xb5890dba
#define CODE_server_DH_params_fail	0x79cb045d
#define CODE_server_DH_params_ok	0xd0e8075c
#define CODE_set_client_DH_params	0xf5045f1f
#define CODE_client_DH_inner_data	0x6643b654
#define CODE_dh_gen_ok			0x3bcbf734
#define CODE_dh_gen_retry		0x46dc1fb9
#define CODE_dh_gen_fail		0xa69dae02 

/* generic data structures */
#define CODE_vector_long		0xc734a64e
#define CODE_vector_int			0xa03855ae
#define CODE_vector_Object		0xa351ae8e
#define	CODE_vector			0x1cb5c415

/* service messages */
#define CODE_rpc_result			0xf35c6d01
#define CODE_rpc_error			0x2144ca19
#define CODE_msg_container		0x73f1f8dc
#define CODE_msg_copy			0xe06046b2
#define CODE_http_wait			0x9299359f
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

/* sample rpc query/response structures */
#define	CODE_getUser			0xb0f732d5
#define	CODE_getUsers			0x2d84d5f5
#define CODE_user			0xd23c81a3
#define CODE_no_user			0xc67599d1

#define	CODE_msgs_random		0x12345678
#define	CODE_random_msg			0x87654321

#define RPC_INVOKE_REQ			0x2374df3d
#define RPC_INVOKE_KPHP_REQ		0x99a37fda
#define RPC_REQ_RUNNING			0x346d5efa
#define RPC_REQ_ERROR			0x7ae432f5
#define RPC_REQ_RESULT			0x63aeda4e
#define	RPC_READY			0x6a34cac7
#define	RPC_STOP_READY			0x59d86654
#define	RPC_SEND_SESSION_MSG		0x1ed5a3cc
#define RPC_RESPONSE_INDIRECT		0x2194f56e

/* RPC for workers */
#define CODE_send_session_msg		0x81bb412c
#define CODE_sendMsgOk			0x29841ee2
#define CODE_sendMsgNoSession		0x2b2b9e78
#define CODE_sendMsgFailed		0x4b0cbd57
#define CODE_get_auth_sessions		0x611f7845
#define CODE_authKeyNone		0x8a8bc1f3
#define CODE_authKeySessions		0x6b7f026c
#define	CODE_add_session_box		0xe707e295
#define	CODE_set_session_box		0x193d4231
#define	CODE_replace_session_box	0xcb101b49
#define	CODE_replace_session_box_cas	0xb2bbfa78
#define	CODE_delete_session_box		0x01b78d81
#define	CODE_delete_session_box_cas	0xb3fdc3c5
#define	CODE_session_box_no_session	0x43f46c33
#define	CODE_session_box_created	0xe1dd5d40
#define	CODE_session_box_replaced	0xbd9cb6b2
#define	CODE_session_box_deleted	0xaf8fd05e
#define	CODE_session_box_not_found	0xb3560a7f
#define	CODE_session_box_found		0x560fe356
#define	CODE_session_box_changed	0x014b31b8
#define	CODE_get_session_box		0x8793a924
#define	CODE_get_session_box_cond	0x7888fab6
#define	CODE_session_box_session_absent	0x9e234062
#define	CODE_session_box_absent		0xa1a106eb
#define	CODE_session_box		0x7956cd97
#define	CODE_session_box_large		0xb568d189
#define	CODE_get_sessions_activity	0x059dc5f6
#define	CODE_sessions_activities	0x60ce5b1d
#define	CODE_get_session_activity	0x96dbac11
#define	CODE_session_activity		0xe175e8e0

/* RPC for front/proxy */
#define	RPC_FRONT		0x27a456f3
#define	RPC_FRONT_ACK		0x624abd23
#define	RPC_FRONT_ERR		0x71dda175
#define	RPC_PROXY_REQ		0x36cef1ee
#define	RPC_PROXY_ANS		0x4403da0d
#define	RPC_CLOSE_CONN		0x1fcf425d
#define	RPC_CLOSE_EXT		0x5eb634a2
#define	RPC_SIMPLE_ACK		0x3bac409b



#define CODE_auth_send_code 0xd16ff372
#define CODE_auth_sent_code 0x2215bcbd
#define CODE_help_get_config 0xc4f9186b
#define CODE_config 0x232d5905
#define CODE_dc_option 0x2ec2a43c
#define CODE_bool_false 0xbc799737
#define CODE_bool_true 0x997275b5
#define CODE_user_self 0x720535ec
#define CODE_auth_authorization 0xf6b673a4
#define CODE_user_profile_photo_empty 0x4f11bae1
#define CODE_user_profile_photo 0x990d1493
#define CODE_user_status_empty 0x9d05049
#define CODE_user_status_online 0xedb93949
#define CODE_user_status_offline 0x8c703f
#define CODE_sign_in 0xbcd51581
#define CODE_file_location 0x53d69076
#define CODE_file_location_unavailable 0x7c596b46
#define CODE_contacts_get_contacts 0x22c6aa08
#define CODE_contacts_contacts 0x6f8b8cb2
#define CODE_contact 0xf911c994
#define CODE_user_empty 0x200250ba
#define CODE_user_contact 0xf2fb8319
#define CODE_user_request 0x22e8ceb0
#define CODE_user_foreign 0x5214c89d
#define CODE_user_deleted 0xb29ad7cc
#define CODE_gzip_packed 0x3072cfa1


/* not really a limit, for struct encrypted_message only */
// #define MAX_MESSAGE_INTS	16384
#define MAX_MESSAGE_INTS	1048576
#define MAX_PROTO_MESSAGE_INTS	1048576

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

struct worker_descr {
  int addr;
  int port;
  int pid;
  int start_time;
  int id;
};

struct rpc_ready_packet {
  int len;
  int seq_num;
  int type;
  struct worker_descr worker;
  int worker_ready_cnt; 
  int crc32;
};


struct front_descr {
  int addr;
  int port;
  int pid;
  int start_time;
  int id;
};

struct rpc_front_packet {
  int len;
  int seq_num;
  int type;
  struct front_descr front;
  long long hash_mult;
  int rem, mod;
  int crc32;
};

struct middle_descr {
  int addr;
  int port;
  int pid;
  int start_time;
  int id;
};

struct rpc_front_ack {
  int len;
  int seq_num;
  int type;
  struct middle_descr middle;
  int crc32;
};

struct rpc_front_err {
  int len;
  int seq_num;
  int type;
  int errcode;
  struct middle_descr middle;
  long long hash_mult;
  int rem, mod;
  int crc32;
};

struct rpc_proxy_req {
  int len;
  int seq_num;
  int type;
  int flags;
  long long ext_conn_id;
  unsigned char remote_ipv6[16];
  int remote_port;
  unsigned char our_ipv6[16];
  int our_port;
  int data[];
};

#define	PROXY_HDR(__x)	((struct rpc_proxy_req *)((__x) - offsetof(struct rpc_proxy_req, data)))

struct rpc_proxy_ans {
  int len;
  int seq_num;
  int type;
  int flags;	// +16 = small error packet, +8 = flush immediately
  long long ext_conn_id;
  int data[];
};

struct rpc_close_conn {
  int len;
  int seq_num;
  int type;
  long long ext_conn_id;
  int crc32;
};

struct rpc_close_ext {
  int len;
  int seq_num;
  int type;
  long long ext_conn_id;
  int crc32;
};

struct rpc_simple_ack {
  int len;
  int seq_num;
  int type;
  long long ext_conn_id;
  int confirm_key;
  int crc32;
};

#pragma pack(pop)

BN_CTX *BN_ctx;

void prng_seed (const char *password_filename, int password_length);
int serialize_bignum (BIGNUM *b, char *buffer, int maxlen);
long long compute_rsa_key_fingerprint (RSA *key);

#define PACKET_BUFFER_SIZE	(16384 * 100) // temp fix
int packet_buffer[PACKET_BUFFER_SIZE], *packet_ptr;

static inline void out_ints (int *what, int len) {
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

static inline void clear_packet (void) {
  packet_ptr = packet_buffer;
}

void out_cstring (const char *str, long len);
void out_cstring_careful (const char *str, long len);
void out_data (const char *data, long len);

static inline void out_string (const char *str) {
  out_cstring (str, strlen (str));
}

static inline void out_bignum (BIGNUM *n) {
  int l = serialize_bignum (n, (char *)packet_ptr, (PACKET_BUFFER_SIZE - (packet_ptr - packet_buffer)) * 4);
  assert (l > 0);
  packet_ptr += l >> 2;
}

extern int *in_ptr, *in_end;

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
  return strndup (fetch_str (l), l);
}

static __inline__ unsigned long long rdtsc(void) {
  unsigned hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

static inline long have_prefetch_ints (void) {
  return in_end - in_ptr;
}

int fetch_bignum (BIGNUM *x);

static inline int fetch_int (void) {
  return *(in_ptr ++);
}

static inline int prefetch_int (void) {
  return *(in_ptr);
}

static inline long long fetch_long (void) {
  long long r = *(long long *)in_ptr;
  in_ptr += 2;
  return r;
}

int get_random_bytes (void *buf, int n);

int pad_rsa_encrypt (char *from, int from_len, char *to, int size, BIGNUM *N, BIGNUM *E);
int pad_rsa_decrypt (char *from, int from_len, char *to, int size, BIGNUM *N, BIGNUM *D);

extern long long rsa_encrypted_chunks, rsa_decrypted_chunks;

extern unsigned char aes_key_raw[32], aes_iv[32];
extern AES_KEY aes_key;

void init_aes_unauth (const char server_nonce[16], const char hidden_client_nonce[32], int encrypt);
void init_aes_auth (char auth_key[192], char msg_key[16], int encrypt);
int pad_aes_encrypt (char *from, int from_len, char *to, int size);
int pad_aes_decrypt (char *from, int from_len, char *to, int size);

static inline void hexdump_in (void) {
  int *ptr = in_ptr;
  while (ptr < in_end) { fprintf (stderr, " %08x", *(ptr ++)); }
  fprintf (stderr, "\n");
}
#endif
