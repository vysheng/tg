#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "tgl.h"
#include "tools.h"
#include "mtproto-client.h"

#include <event2/event.h>
struct tgl_state tgl_state;

    
void tgl_set_binlog_mode (int mode) {
  tgl_state.binlog_enabled = mode;
}

void tgl_set_binlog_path (const char *path) {
  tgl_state.binlog_name = tstrdup (path);
}
    
void tgl_set_auth_file_path (const char *path) {
  tgl_state.auth_file = tstrdup (path);
}

void tgl_set_download_directory (const char *path) {
  tgl_state.downloads_directory = tstrdup (path);
}

void tgl_set_callback (struct tgl_update_callback *cb) {
  tgl_state.callback = *cb;
}

void tgl_set_rsa_key (const char *key) {
  tgl_state.rsa_key = tstrdup (key);
}

void tgl_init (void) {
  tgl_state.ev_base = event_base_new ();
  tglmp_on_start (tgl_state.rsa_key);
}
