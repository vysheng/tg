#ifndef __LOOP_H__
#define __LOOP_H__
int loop (void);
void net_loop (int flags, int (*end)(void));
void write_auth_file (void);
#endif
