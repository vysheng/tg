#ifndef __TGL_H__
#define __TGL_H__

// Do not modify this structure, unless you know what you do
struct tgl_state {
  int our_id; // ID of logged in user
};
extern struct tgl_state tgl_state;

// Should be set before first use of lib
struct tgl_params {
  int test_mode; // Connect to test DC
  int verbosity; // May be modified any moment
};
extern struct tgl_params tgl_params;

#endif
