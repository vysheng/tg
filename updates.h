#ifndef __UPDATES_H__
#define __UPDATES_H__
struct connection;
void tglu_work_update (struct connection *c, long long msg_id);
void tglu_work_updates_to_long (struct connection *c, long long msg_id);
void tglu_work_update_short_chat_message (struct connection *c, long long msg_id);
void tglu_work_update_short_message (struct connection *c, long long msg_id);
void tglu_work_update_short (struct connection *c, long long msg_id);
void tglu_work_updates (struct connection *c, long long msg_id);

void tglu_fetch_pts (void);
void tglu_fetch_qts (void);
void tglu_fetch_seq (void);
void tglu_fetch_date (void);
#endif
