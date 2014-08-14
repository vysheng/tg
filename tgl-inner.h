#ifndef __TGL_INNER_H__
#define __TGL_INNER_H__

#define vlogprintf(verbosity_level,...) \
  do { \
    if (tgl_state.verbosity >= verbosity_level) { \
      tgl_state.callback.logprintf (__VA_ARGS__); \
    } \
  } while (0)

#endif
