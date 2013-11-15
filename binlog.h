#ifndef __BINLOG_H__
#define __BINLOG_H__

#define LOG_START 0x8948329a
#define LOG_AUTH_KEY 0x984932aa
#define LOG_DEFAULT_DC 0x95382908
#define LOG_OUR_ID 0x8943211a
#define LOG_DC_SIGNED 0x234f9893
#define LOG_DC_SALT 0x92192ffa
#define LOG_DH_CONFIG 0x8983402b
#define LOG_ENCR_CHAT_KEY 0x894320aa
#define LOG_ENCR_CHAT_SEND_ACCEPT 0x12ab01c4
#define LOG_ENCR_CHAT_SEND_CREATE 0xab091e24
#define LOG_ENCR_CHAT_DELETED 0x99481230
#define LOG_ENCR_CHAT_WAITING 0x7102100a
#define LOG_ENCR_CHAT_REQUESTED 0x9011011a
#define LOG_ENCR_CHAT_OK 0x7612ce13

void *alloc_log_event (int l);
void replay_log (void);
void add_log_event (const int *data, int l);
void write_binlog (void);
#endif
