#ifndef __MTPROTO_CLIENT_H__
#define __MTPROTO_CLIENT_H__
#include "net.h"
void on_start (void);
long long encrypt_send_message (struct connection *c, int *msg, int msg_ints, int useful);
void dc_authorize (struct dc *DC);
#endif
