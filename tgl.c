#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "tgl.h"
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
