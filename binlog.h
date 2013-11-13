#ifndef __BINLOG_H__
#define __BINLOG_H__

void *alloc_log_event (int l);
void replay_log (void);
#endif
